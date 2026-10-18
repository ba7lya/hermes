// SPDX-License-Identifier: MIT
///
/// @file vertical.cxx
/// @author BA7LYA (1042140025@qq.com)
/// @brief The vertical state chart of Figure 22 and the session that enforces it.
/// @version 0.1
/// @date 2026-10-18
/// @copyright Copyright (c) 2026
///

#include "vertical.hxx"

#include <asio.hpp>
#include <string>
#include <utility>

#include "error.hxx"
#include "xml.hxx"

namespace ba7lya::hermes::detail {

namespace {

template<class F>
void guarded(F&& callback) noexcept {
    try {
        callback();
    }
    catch (...) {
    }
}

} // namespace

//==============================================================================
// the state chart
//==============================================================================

bool vertical_is_anytime_message(std::string_view tag) {
    return tag == message_name<notification> || tag == message_name<check_alive> || tag == message_name<command>
        || tag == message_name<set_configuration> || tag == message_name<get_configuration>
        || tag == message_name<current_configuration> || tag == message_name<board_arrived>
        || tag == message_name<board_departed> || tag == message_name<query_work_order_info>
        || tag == message_name<send_work_order_info> || tag == message_name<reply_work_order_info>
        || tag == message_name<query_hermes_capabilities> || tag == message_name<send_hermes_capabilities>;
}

std::optional<vertical_state> vertical_next_state(vertical_state current, std::string_view tag) {
    if (tag != message_name<supervisory_service_description>) { return std::nullopt; }

    switch (current) {
    case vertical_state::SOCKET_CONNECTED:                return vertical_state::SUPERVISORY_SERVICE_DESCRIPTION;
    case vertical_state::SUPERVISORY_SERVICE_DESCRIPTION: return vertical_state::CONNECTED;
    default:                                              return std::nullopt;
    }
}

bool vertical_role_may_send(vertical_role role, std::string_view tag) {
    if (tag == message_name<notification> || tag == message_name<check_alive> || tag == message_name<command>) {
        return true;
    }
    if (tag == message_name<supervisory_service_description>) { return true; }

    if (role == vertical_role::SERVICE) {
        return tag == message_name<current_configuration> || tag == message_name<board_arrived>
            || tag == message_name<board_departed> || tag == message_name<query_work_order_info>
            || tag == message_name<reply_work_order_info> || tag == message_name<send_hermes_capabilities>;
    }
    return tag == message_name<get_configuration> || tag == message_name<set_configuration>
        || tag == message_name<query_hermes_capabilities> || tag == message_name<send_work_order_info>;
}

bool vertical_role_may_receive(vertical_role role, std::string_view tag) {
    return vertical_role_may_send(role == vertical_role::SERVICE ? vertical_role::CLIENT : vertical_role::SERVICE, tag);
}

//==============================================================================
// session
//==============================================================================

vertical_session::vertical_session(
    asio::io_context& context,
    session_id id,
    vertical_role role,
    vertical_options options,
    connection_info info,
    handlers callbacks
)
    : context_(context)
    , id_(id)
    , role_(role)
    , options_(std::move(options))
    , info_(std::move(info))
    , handlers_(std::move(callbacks))
    , ping_timer_(context)
    , pong_timer_(context) {}

vertical_session::~vertical_session() {
    asio::error_code ignored;
    ping_timer_.cancel(ignored);
    pong_timer_.cancel(ignored);
}

std::shared_ptr<connection> vertical_session::connection_now() const {
    const std::lock_guard lock(mutex_);
    return connection_;
}

void vertical_session::attach(const std::shared_ptr<connection>& socket) {
    {
        const std::lock_guard lock(mutex_);
        connection_ = socket;
    }

    const auto weak = weak_from_this();
    socket->start(
        [weak](std::string document) {
            if (const auto self = weak.lock()) { self->on_frame(std::move(document)); }
        },
        [weak](const error_info& error) {
            if (const auto self = weak.lock()) { self->on_closed(error); }
        }
    );

    change_state(vertical_state::SOCKET_CONNECTED);
    if (handlers_.connected_) {
        guarded([&] { handlers_.connected_(id_, info_); });
    }

    if (role_ == vertical_role::CLIENT) { send_service_description(); }
}

void vertical_session::send_service_description() {
    supervisory_service_description value;
    value.system_id_ = options_.system_id_;
    value.version_ = std::string { PROTOCOL_VERSION };
    value.supported_features_ = options_.features_;
    write(any_message { std::move(value) });
}

void vertical_session::change_state(vertical_state next) {
    const auto previous = state_.exchange(next);
    if (previous == next) { return; }

    if (handlers_.state_) {
        guarded([&] { handlers_.state_(id_, next); });
    }

    if (next == vertical_state::CONNECTED) { start_check_alive(); }
}

void vertical_session::notify_message(vertical_state state, const any_message& message) {
    if (!handlers_.message_) { return; }
    guarded([&] { handlers_.message_(id_, state, message); });
}

void vertical_session::trace(trace_type type, std::string_view text) const noexcept {
    if (!handlers_.trace_) { return; }
    guarded([&] { handlers_.trace_(id_, type, text); });
}

void vertical_session::write(const any_message& message) {
    if (closed_.load(std::memory_order_relaxed) || closing_.load(std::memory_order_relaxed)) { return; }
    const auto socket = connection_now();
    if (!socket) { return; }

    const auto document = to_xml(envelope { timestamp_now(), message });
    trace(trace_type::SENT, document);

    if (const auto next = vertical_next_state(state(), message_name_of(message))) { change_state(*next); }
    socket->send(document);
}

void vertical_session::on_frame(std::string document) {
    if (closed_.load(std::memory_order_relaxed)) { return; }

    trace(trace_type::RECEIVED, document);

    auto envelope = parse(document);
    if (!envelope) {
        if (envelope.error().code_ == error_code::UNSUPPORTED) {
            trace(trace_type::WARNING, "ignored an unknown message: " + envelope.error().text_);
            return;
        }
        protocol_error("malformed document: " + envelope.error().text_);
        return;
    }

    handle(std::move(envelope->message_));
}

void vertical_session::handle(any_message message) {
    const auto tag = message_name_of(message);
    if (!vertical_role_may_receive(role_, tag)) {
        protocol_error("the message " + std::string { tag } + " is not allowed on this interface");
        return;
    }

    if (const auto* keep_alive = std::get_if<check_alive>(&message)) {
        handle_check_alive(*keep_alive);
        notify_message(state(), message);
        return;
    }
    if (const auto* description = std::get_if<supervisory_service_description>(&message)) {
        handle_service_description(*description, message);
        return;
    }
    if (std::holds_alternative<notification>(message) || std::holds_alternative<command>(message)) {
        notify_message(state(), message);
        return;
    }

    const auto current = state();
    if (current != vertical_state::CONNECTED) {
        protocol_error("the message " + std::string { tag } + " arrived before the handshake completed");
        return;
    }

    if (!vertical_is_anytime_message(tag)) {
        protocol_error("the message " + std::string { tag } + " is not allowed on the vertical channel");
        return;
    }

    notify_message(current, message);
}

void vertical_session::handle_service_description(
    const supervisory_service_description& value,
    const any_message& message
) {
    peer_supports_check_alive_response_ = value.supported_features_.check_alive_response_;
    options_.features_ = options_.features_; // the announced features stay ours

    switch (state()) {
    case vertical_state::SOCKET_CONNECTED:
        change_state(vertical_state::SUPERVISORY_SERVICE_DESCRIPTION);
        notify_message(vertical_state::SUPERVISORY_SERVICE_DESCRIPTION, message);
        if (role_ == vertical_role::SERVICE) { send_service_description(); }
        return;

    case vertical_state::SUPERVISORY_SERVICE_DESCRIPTION:
        change_state(vertical_state::CONNECTED);
        notify_message(vertical_state::CONNECTED, message);
        return;

    default: protocol_error("unexpected SupervisoryServiceDescription"); return;
    }
}

void vertical_session::handle_check_alive(const check_alive& value) {
    if (!value.type_) { return; }

    if (*value.type_ == check_alive_type::PING) {
        if (!options_.features_.check_alive_response_) { return; }

        check_alive pong;
        pong.type_ = check_alive_type::PONG;
        pong.id_ = value.id_;
        write(any_message { std::move(pong) });
        return;
    }

    // Any pong proves the peer is alive; the id only ties it to a ping for diagnostics.
    if (pending_ping_id_.empty()) { return; }

    pending_ping_id_.clear();
    asio::error_code ignored;
    pong_timer_.cancel(ignored);
}

void vertical_session::start_check_alive() {
    if (ping_started_) { return; }
    ping_started_ = true;

    if (options_.check_alive_interval_.count() <= 0) { return; }
    schedule_ping();
}

void vertical_session::schedule_ping() {
    ping_timer_.expires_after(options_.check_alive_interval_);
    const auto weak = weak_from_this();
    ping_timer_.async_wait([weak](const asio::error_code& error) {
        if (error) { return; }
        if (const auto self = weak.lock()) { self->on_ping(); }
    });
}

void vertical_session::on_ping() {
    if (closed_.load(std::memory_order_relaxed)) { return; }

    check_alive ping;
    ping.type_ = check_alive_type::PING;
    ping.id_ = std::to_string(++ping_counter_) + "-" + std::to_string(id_);

    // The timeout measures the oldest ping no pong answered yet; pings that follow it do not
    // restart the clock, or a peer that never answers would never time out.
    if (peer_supports_check_alive_response_ && pending_ping_id_.empty()) {
        pending_ping_id_ = *ping.id_;
        arm_pong_timeout();
    }

    write(any_message { std::move(ping) });
    schedule_ping();
}

void vertical_session::arm_pong_timeout() {
    if (options_.check_alive_timeout_.count() <= 0) { return; }

    pong_timer_.expires_after(options_.check_alive_timeout_);
    const auto weak = weak_from_this();
    pong_timer_.async_wait([weak](const asio::error_code& error) {
        if (error) { return; }
        if (const auto self = weak.lock()) { self->on_pong_timeout(); }
    });
}

void vertical_session::on_pong_timeout() {
    if (closed_.load(std::memory_order_relaxed) || pending_ping_id_.empty()) { return; }

    close(
        error_info { error_code::TIMEOUT,
                     "the peer did not answer a CheckAlive within "
                         + std::to_string(options_.check_alive_timeout_.count()) + " ms" }
    );
}

void vertical_session::protocol_error(std::string text) {
    if (closing_.exchange(true) || closed_.load(std::memory_order_relaxed)) { return; }

    trace(trace_type::FAILURE, text);

    notification note;
    note.notification_code_ = notification_code::PROTOCOL_ERROR;
    note.severity_ = severity::FATAL;
    note.description_ = text;

    const auto document = to_xml(envelope { timestamp_now(), any_message { std::move(note) } });
    trace(trace_type::SENT, document);

    if (const auto socket = connection_now()) {
        socket->send_and_close(document, error_info { error_code::PEER, std::move(text) });
    }
    else { on_closed(error_info { error_code::PEER, std::move(text) }); }
}

std::expected<void, error_info> vertical_session::send(const any_message& message) {
    if (closed_.load(std::memory_order_relaxed) || closing_.load(std::memory_order_relaxed)) {
        return std::unexpected(error_info { error_code::CLIENT, "the session is closed" });
    }

    const auto tag = message_name_of(message);
    if (!vertical_role_may_send(role_, tag)) {
        return std::unexpected(
            error_info { error_code::CLIENT, "the interface may not send a " + std::string { tag } }
        );
    }
    if (tag == message_name<supervisory_service_description>) {
        return std::unexpected(
            error_info { error_code::CLIENT, "the SupervisoryServiceDescription is sent automatically" }
        );
    }

    const auto current = state();
    if (current == vertical_state::NOT_CONNECTED || current == vertical_state::DISCONNECTED) {
        return std::unexpected(error_info { error_code::CLIENT, "the session is not connected" });
    }
    if (current != vertical_state::CONNECTED) {
        if (!(std::holds_alternative<notification>(message) || std::holds_alternative<check_alive>(message)
              || std::holds_alternative<command>(message))) {
            return std::unexpected(error_info { error_code::CLIENT, "the handshake has not completed yet" });
        }
    }

    if (!connection_now()) {
        return std::unexpected(error_info { error_code::CLIENT, "the session has no connection" });
    }

    const auto self = shared_from_this();
    asio::post(context_, [self, message]() { self->write(message); });
    return {};
}

std::expected<void, error_info> vertical_session::send_raw(std::string_view xml) {
    if (closed_.load(std::memory_order_relaxed) || closing_.load(std::memory_order_relaxed)) {
        return std::unexpected(error_info { error_code::CLIENT, "the session is closed" });
    }
    if (!connection_now()) {
        return std::unexpected(error_info { error_code::CLIENT, "the session has no connection" });
    }

    const auto self = shared_from_this();
    asio::post(context_, [self, document = std::string { xml }]() {
        self->trace(trace_type::SENT, document);
        if (const auto socket = self->connection_now()) { socket->send(document); }
    });
    return {};
}

void vertical_session::shutdown(const std::optional<notification>& notification) {
    closing_ = true;
    const auto self = shared_from_this();
    asio::post(context_, [self, notification]() {
        if (self->closed_.load(std::memory_order_relaxed)) { return; }

        const auto socket = self->connection_now();
        if (!socket) {
            self->on_closed(error_info {});
            return;
        }

        if (notification) {
            const auto document = to_xml(envelope { timestamp_now(), any_message { *notification } });
            self->trace(trace_type::SENT, document);
            socket->send_and_close(document, error_info {});
            return;
        }
        socket->close(error_info {});
    });
}

void vertical_session::close(const error_info& reason) {
    closing_ = true;
    if (const auto socket = connection_now()) {
        socket->close(reason);
        return;
    }
    on_closed(reason);
}

void vertical_session::on_closed(const error_info& error) {
    if (closed_.exchange(true)) { return; }

    closing_ = true;
    asio::error_code ignored;
    ping_timer_.cancel(ignored);
    pong_timer_.cancel(ignored);

    change_state(vertical_state::DISCONNECTED);

    if (handlers_.disconnected_) {
        guarded([&] { handlers_.disconnected_(id_, error); });
    }
}

} // namespace ba7lya::hermes::detail
