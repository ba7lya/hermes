// SPDX-License-Identifier: MIT
///
/// @file horizontal.cxx
/// @author BA7LYA (1042140025@qq.com)
/// @brief The horizontal state chart of Figure 19 and the session that enforces it.
/// @version 0.1
/// @date 2026-10-18
/// @copyright Copyright (c) 2026
///

#include "horizontal.hxx"

#include <asio.hpp>
#include <cstdint>
#include <string>
#include <utility>

#include "error.hxx"
#include "xml.hxx"

namespace ba7lya::hermes::detail {

namespace {

/// @brief Runs @p callback, swallowing what it throws: a broken callback must not kill the I/O thread.
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

bool is_anytime_message(std::string_view tag) {
    return tag == message_name<notification> || tag == message_name<check_alive> || tag == message_name<command>
        || tag == message_name<query_board_info> || tag == message_name<send_board_info>;
}

bool needs_transition(std::string_view tag) {
    return tag == message_name<service_description> || tag == message_name<board_available>
        || tag == message_name<revoke_board_available> || tag == message_name<machine_ready>
        || tag == message_name<revoke_machine_ready> || tag == message_name<start_transport>
        || tag == message_name<stop_transport> || tag == message_name<transport_finished>;
}

std::optional<channel_state> next_state(channel_state current, std::string_view tag) {
    switch (current) {
    case channel_state::SOCKET_CONNECTED:
        if (tag == message_name<service_description>) { return channel_state::SERVICE_DESCRIPTION_DOWNSTREAM; }
        break;

    case channel_state::SERVICE_DESCRIPTION_DOWNSTREAM:
        if (tag == message_name<service_description>) { return channel_state::NOT_AVAILABLE_NOT_READY; }
        break;

    case channel_state::NOT_AVAILABLE_NOT_READY:
        if (tag == message_name<board_available>) { return channel_state::BOARD_AVAILABLE; }
        if (tag == message_name<machine_ready>) { return channel_state::MACHINE_READY; }
        break;

    case channel_state::BOARD_AVAILABLE:
        if (tag == message_name<revoke_board_available>) { return channel_state::NOT_AVAILABLE_NOT_READY; }
        if (tag == message_name<machine_ready>) { return channel_state::AVAILABLE_AND_READY; }
        break;

    case channel_state::MACHINE_READY:
        if (tag == message_name<revoke_machine_ready>) { return channel_state::NOT_AVAILABLE_NOT_READY; }
        if (tag == message_name<board_available>) { return channel_state::AVAILABLE_AND_READY; }
        if (tag == message_name<start_transport>) { return channel_state::TRANSPORTING; }
        break;

    case channel_state::AVAILABLE_AND_READY:
        if (tag == message_name<revoke_board_available>) { return channel_state::MACHINE_READY; }
        if (tag == message_name<revoke_machine_ready>) { return channel_state::BOARD_AVAILABLE; }
        if (tag == message_name<start_transport>) { return channel_state::TRANSPORTING; }
        break;

    case channel_state::TRANSPORTING:
        // A revoke_board_available or a repeated board_available may cross a start_transport or a
        // stop_transport; the standard asks for that race to be tolerated (2.3.6).
        if (tag == message_name<transport_finished>) { return channel_state::TRANSPORT_FINISHED; }
        if (tag == message_name<stop_transport>) { return channel_state::TRANSPORT_STOPPED; }
        if (tag == message_name<board_available>) { return channel_state::TRANSPORTING; }
        if (tag == message_name<revoke_board_available>) { return channel_state::TRANSPORTING; }
        break;

    case channel_state::TRANSPORT_STOPPED:
        if (tag == message_name<transport_finished>) { return channel_state::NOT_AVAILABLE_NOT_READY; }
        if (tag == message_name<board_available>) { return channel_state::TRANSPORT_STOPPED; }
        if (tag == message_name<revoke_board_available>) { return channel_state::TRANSPORT_STOPPED; }
        break;

    case channel_state::TRANSPORT_FINISHED:
        if (tag == message_name<stop_transport>) { return channel_state::NOT_AVAILABLE_NOT_READY; }
        break;

    default: break;
    }
    return std::nullopt;
}

bool role_may_send(horizontal_role role, std::string_view tag) {
    if (tag == message_name<notification> || tag == message_name<check_alive> || tag == message_name<command>) {
        return true;
    }
    if (tag == message_name<service_description>) { return true; }

    if (role == horizontal_role::DOWNSTREAM) {
        return tag == message_name<board_available> || tag == message_name<revoke_board_available>
            || tag == message_name<transport_finished> || tag == message_name<board_forecast>
            || tag == message_name<send_board_info>;
    }
    return tag == message_name<machine_ready> || tag == message_name<revoke_machine_ready>
        || tag == message_name<start_transport> || tag == message_name<stop_transport>
        || tag == message_name<query_board_info>;
}

bool role_may_receive(horizontal_role role, std::string_view tag) {
    return role_may_send(
        role == horizontal_role::DOWNSTREAM ? horizontal_role::UPSTREAM : horizontal_role::DOWNSTREAM,
        tag
    );
}

//==============================================================================
// session
//==============================================================================

horizontal_session::horizontal_session(
    asio::io_context& context,
    session_id id,
    horizontal_role role,
    horizontal_options options,
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

horizontal_session::~horizontal_session() {
    asio::error_code ignored;
    ping_timer_.cancel(ignored);
    pong_timer_.cancel(ignored);
}

std::shared_ptr<connection> horizontal_session::connection_now() const {
    const std::lock_guard lock(mutex_);
    return connection_;
}

void horizontal_session::attach(const std::shared_ptr<connection>& socket) {
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

    change_state(channel_state::SOCKET_CONNECTED);
    if (handlers_.connected_) {
        guarded([&] { handlers_.connected_(id_, info_); });
    }

    if (role_ == horizontal_role::UPSTREAM) { send_service_description(); }
}

void horizontal_session::send_service_description() {
    service_description value;
    value.machine_id_ = options_.machine_id_;
    value.lane_id_ = options_.lane_id_;
    if (!options_.interface_id_.empty()) { value.interface_id_ = options_.interface_id_; }
    value.version_ = std::string { PROTOCOL_VERSION };
    value.supported_features_ = options_.features_;
    write(any_message { std::move(value) });
}

void horizontal_session::change_state(channel_state next) {
    const auto previous = state_.exchange(next);
    if (previous == next) { return; }

    if (handlers_.state_) {
        guarded([&] { handlers_.state_(id_, next); });
    }

    if (next == channel_state::NOT_AVAILABLE_NOT_READY) { start_check_alive(); }
}

void horizontal_session::notify_message(channel_state state, const any_message& message) {
    if (!handlers_.message_) { return; }
    guarded([&] { handlers_.message_(id_, state, message); });
}

void horizontal_session::trace(trace_type type, std::string_view text) const noexcept {
    if (!handlers_.trace_) { return; }
    guarded([&] { handlers_.trace_(id_, type, text); });
}

void horizontal_session::write(const any_message& message) {
    if (closed_.load(std::memory_order_relaxed) || closing_.load(std::memory_order_relaxed)) { return; }
    const auto socket = connection_now();
    if (!socket) { return; }

    const auto tag = message_name_of(message);
    const auto document = to_xml(envelope { timestamp_now(), message });
    trace(trace_type::SENT, document);

    if (const auto next = next_state(state(), tag)) { change_state(*next); }

    if (const auto* start = std::get_if<start_transport>(&message)) {
        if (!last_board_id_.empty() && start->board_id_ != last_board_id_) {
            trace(
                trace_type::WARNING,
                "StartTransport BoardId does not match the last BoardAvailable; the transport is to be cancelled (3.10)"
            );
        }
        start_board_id_ = start->board_id_;
    }
    if (const auto* available = std::get_if<board_available>(&message)) { last_board_id_ = available->board_id_; }

    socket->send(document);
}

void horizontal_session::on_frame(std::string document) {
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

void horizontal_session::handle(any_message message) {
    const auto tag = message_name_of(message);
    if (!role_may_receive(role_, tag)) {
        protocol_error("the message " + std::string { tag } + " is not allowed on this interface");
        return;
    }

    if (const auto* keep_alive = std::get_if<check_alive>(&message)) {
        handle_check_alive(*keep_alive);
        notify_message(state(), message);
        return;
    }
    if (const auto* description = std::get_if<service_description>(&message)) {
        handle_service_description(*description, message);
        return;
    }
    if (std::holds_alternative<notification>(message)) {
        notify_message(state(), message);
        return;
    }
    if (std::holds_alternative<command>(message)) {
        notify_message(state(), message);
        return;
    }
    if (std::holds_alternative<board_forecast>(message)) {
        const auto current = state();
        if (current == channel_state::NOT_AVAILABLE_NOT_READY || current == channel_state::MACHINE_READY) {
            notify_message(current, message);
        }
        else { trace(trace_type::WARNING, "ignored a BoardForecast in state " + std::string { to_string(current) }); }
        return;
    }
    if (is_anytime_message(tag)) {
        notify_message(state(), message);
        return;
    }

    const auto current = state();
    if (current == channel_state::NOT_CONNECTED || current == channel_state::SOCKET_CONNECTED
        || current == channel_state::DISCONNECTED) {
        protocol_error("the message " + std::string { tag } + " arrived before the handshake completed");
        return;
    }

    if (!check_board_id(message)) { return; }

    if (const auto* start = std::get_if<start_transport>(&message)) {
        if (!last_board_id_.empty() && start->board_id_ != last_board_id_) {
            trace(
                trace_type::WARNING,
                "StartTransport BoardId does not match the last BoardAvailable; the transport is to be cancelled (3.10)"
            );
        }
        start_board_id_ = start->board_id_;
    }
    if (const auto* available = std::get_if<board_available>(&message)) { last_board_id_ = available->board_id_; }

    const auto next = next_state(current, tag);
    if (!next) {
        protocol_error(
            "the message " + std::string { tag } + " is not allowed in state " + std::string { to_string(current) }
        );
        return;
    }

    change_state(*next);
    notify_message(*next, message);
}

bool horizontal_session::check_board_id(const any_message& message) {
    std::string_view board_id;
    std::string_view what;

    if (const auto* stop = std::get_if<stop_transport>(&message)) {
        board_id = stop->board_id_;
        what = "StopTransport";
    }
    else if (const auto* finished = std::get_if<transport_finished>(&message)) {
        board_id = finished->board_id_;
        what = "TransportFinished";
    }
    else { return true; }

    if (start_board_id_.empty() || board_id == start_board_id_) { return true; }

    protocol_error(std::string { what } + " BoardId does not match the BoardId of StartTransport");
    return false;
}

void horizontal_session::handle_service_description(const service_description& value, const any_message& message) {
    peer_supports_check_alive_response_ = value.supported_features_.check_alive_response_;

    switch (state()) {
    case channel_state::SOCKET_CONNECTED:
        change_state(channel_state::SERVICE_DESCRIPTION_DOWNSTREAM);
        notify_message(channel_state::SERVICE_DESCRIPTION_DOWNSTREAM, message);
        if (role_ == horizontal_role::DOWNSTREAM) { send_service_description(); }
        return;

    case channel_state::SERVICE_DESCRIPTION_DOWNSTREAM:
        change_state(channel_state::NOT_AVAILABLE_NOT_READY);
        notify_message(channel_state::NOT_AVAILABLE_NOT_READY, message);
        return;

    default: protocol_error("unexpected ServiceDescription"); return;
    }
}

void horizontal_session::handle_check_alive(const check_alive& value) {
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

void horizontal_session::start_check_alive() {
    if (ping_started_) { return; }
    ping_started_ = true;

    if (options_.check_alive_interval_.count() <= 0) { return; }
    schedule_ping();
}

void horizontal_session::schedule_ping() {
    ping_timer_.expires_after(options_.check_alive_interval_);
    const auto weak = weak_from_this();
    ping_timer_.async_wait([weak](const asio::error_code& error) {
        if (error) { return; }
        if (const auto self = weak.lock()) { self->on_ping(); }
    });
}

void horizontal_session::on_ping() {
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

void horizontal_session::arm_pong_timeout() {
    if (options_.check_alive_timeout_.count() <= 0) { return; }

    pong_timer_.expires_after(options_.check_alive_timeout_);
    const auto weak = weak_from_this();
    pong_timer_.async_wait([weak](const asio::error_code& error) {
        if (error) { return; }
        if (const auto self = weak.lock()) { self->on_pong_timeout(); }
    });
}

void horizontal_session::on_pong_timeout() {
    if (closed_.load(std::memory_order_relaxed) || pending_ping_id_.empty()) { return; }

    close(
        error_info { error_code::TIMEOUT,
                     "the peer did not answer a CheckAlive within "
                         + std::to_string(options_.check_alive_timeout_.count()) + " ms" }
    );
}

void horizontal_session::protocol_error(std::string text) {
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

std::expected<void, error_info> horizontal_session::send(const any_message& message) {
    if (closed_.load(std::memory_order_relaxed) || closing_.load(std::memory_order_relaxed)) {
        return std::unexpected(error_info { error_code::CLIENT, "the session is closed" });
    }

    const auto tag = message_name_of(message);
    if (!role_may_send(role_, tag)) {
        return std::unexpected(
            error_info { error_code::CLIENT, "the interface may not send a " + std::string { tag } }
        );
    }
    if (tag == message_name<service_description>) {
        return std::unexpected(error_info { error_code::CLIENT, "the ServiceDescription is sent automatically" });
    }

    const auto current = state();
    if (current == channel_state::NOT_CONNECTED || current == channel_state::DISCONNECTED) {
        return std::unexpected(error_info { error_code::CLIENT, "the session is not connected" });
    }
    if ((current == channel_state::SOCKET_CONNECTED || current == channel_state::SERVICE_DESCRIPTION_DOWNSTREAM)
        && !is_anytime_message(tag)) {
        return std::unexpected(error_info { error_code::CLIENT, "the handshake has not completed yet" });
    }

    if (options_.state_check_ == state_check::ALL && needs_transition(tag) && !next_state(current, tag)) {
        return std::unexpected(
            error_info { error_code::CLIENT,
                         "a " + std::string { tag } + " is not allowed in state " + std::string { to_string(current) } }
        );
    }

    if (const auto* stop = std::get_if<stop_transport>(&message)) {
        if (!start_board_id_.empty() && stop->board_id_ != start_board_id_) {
            return std::unexpected(
                error_info { error_code::CLIENT, "StopTransport BoardId does not match StartTransport" }
            );
        }
    }
    if (const auto* finished = std::get_if<transport_finished>(&message)) {
        if (!start_board_id_.empty() && finished->board_id_ != start_board_id_) {
            return std::unexpected(
                error_info { error_code::CLIENT, "TransportFinished BoardId does not match StartTransport" }
            );
        }
    }

    if (!connection_now()) {
        return std::unexpected(error_info { error_code::CLIENT, "the session has no connection" });
    }

    const auto self = shared_from_this();
    asio::post(context_, [self, message]() { self->write(message); });
    return {};
}

std::expected<void, error_info> horizontal_session::send_raw(std::string_view xml) {
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

void horizontal_session::shutdown(const std::optional<notification>& notification) {
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

void horizontal_session::close(const error_info& reason) {
    closing_ = true;
    if (const auto socket = connection_now()) {
        socket->close(reason);
        return;
    }
    on_closed(reason);
}

void horizontal_session::on_closed(const error_info& error) {
    if (closed_.exchange(true)) { return; }

    closing_ = true;
    asio::error_code ignored;
    ping_timer_.cancel(ignored);
    pong_timer_.cancel(ignored);

    change_state(channel_state::DISCONNECTED);

    if (handlers_.disconnected_) {
        guarded([&] { handlers_.disconnected_(id_, error); });
    }
}

} // namespace ba7lya::hermes::detail
