// SPDX-License-Identifier: MIT
///
/// @file vertical_client.cxx
/// @author BA7LYA (1042140025@qq.com)
/// @brief The vertical_client facade: the client a supervisory system runs.
/// @version 0.1
/// @date 2026-10-18
/// @copyright Copyright (c) 2026
///

#include "vertical_client.hxx"

#include <asio.hpp>
#include <atomic>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

#include "net.hxx"
#include "vertical.hxx"

namespace ba7lya::hermes {

namespace {

detail::vertical_options to_options(const vertical_client_settings& settings_) {
    detail::vertical_options options;
    options.system_id_ = settings_.system_id_;
    options.features_ = settings_.features_;
    options.check_alive_interval_ = settings_.check_alive_interval_;
    options.check_alive_timeout_ = settings_.check_alive_timeout_;
    return options;
}

} // namespace

struct vertical_client::impl {
    void start();
    void stop(const std::optional<notification>& notification);
    void apply(vertical_client_settings updated);
    void connect();
    void handle_connect(asio::ip::tcp::socket socket, connection_info info);
    void handle_disconnect(session_id id, const error_info& error);
    void close_current(const std::optional<notification>& notification);
    detail::vertical_session::handlers make_handlers();

    void notify_connected(session_id id, const connection_info& info);
    void notify_disconnected(session_id id, const error_info& error);
    void notify_state(session_id id, vertical_state state);
    void notify_message(session_id id, vertical_state state, const any_message& message);
    void notify_trace(session_id id, trace_type type, std::string_view text);
    void notify_error(const error_info& error);

    mutable std::mutex mutex_;
    vertical_client_settings settings_;
    detail::io_runner io_;
    std::shared_ptr<detail::connector> connector_;
    std::shared_ptr<detail::vertical_session> session_;
    session_id next_session_ { 1 };
    std::atomic<bool> running_ { false };

    connected_handler on_connected_;
    disconnected_handler on_disconnected_;
    vertical_state_handler on_state_;
    vertical_message_handler on_message_;
    trace_handler on_trace_;
    error_handler on_error_;
};

void vertical_client::impl::start() {
    if (running_.exchange(true)) { return; }

    io_.start();
    asio::post(io_.context(), [this]() { connect(); });
}

void vertical_client::impl::stop(const std::optional<notification>& notification) {
    if (!running_.exchange(false)) {
        io_.halt();
        return;
    }

    if (io_.on_thread()) {
        close_current(notification);
        io_.retire();
        return;
    }

    asio::post(io_.context(), [this, notification]() { close_current(notification); });
    io_.join();
}

void vertical_client::impl::apply(vertical_client_settings updated) {
    const bool was_running = running_.load();

    {
        const std::lock_guard lock(mutex_);
        settings_ = std::move(updated);
    }

    if (!was_running) { return; }

    asio::post(io_.context(), [this]() {
        if (!running_.load()) { return; }
        close_current(
            notification { notification_code::CONNECTION_RESET, severity::INFO, "the configuration changed" }
        );
        connect();
    });
}

void vertical_client::impl::close_current(const std::optional<notification>& notification) {
    std::shared_ptr<detail::connector> old_connector;
    std::shared_ptr<detail::vertical_session> old_session;
    {
        const std::lock_guard lock(mutex_);
        old_connector = std::move(connector_);
        connector_.reset();
        old_session = std::move(session_);
        session_.reset();
    }

    if (old_connector) { old_connector->stop(); }
    if (old_session) { old_session->shutdown(notification); }
}

void vertical_client::impl::connect() {
    if (!running_.load()) { return; }

    vertical_client_settings current_;
    {
        const std::lock_guard lock(mutex_);
        current_ = settings_;
    }

    std::shared_ptr<detail::connector> previous;
    {
        const std::lock_guard lock(mutex_);
        previous = std::move(connector_);
    }
    if (previous) { previous->stop(); }

    auto client = std::make_shared<detail::connector>(
        io_.context(),
        current_.address_,
        current_.port_,
        current_.reconnect_delay_
    );

    {
        const std::lock_guard lock(mutex_);
        connector_ = client;
    }

    client->start(
        [this](asio::ip::tcp::socket socket, connection_info info) {
            handle_connect(std::move(socket), std::move(info));
        },
        [this](const error_info& error) { notify_error(error); }
    );
}

void vertical_client::impl::handle_connect(asio::ip::tcp::socket socket, connection_info info) {
    if (!running_.load()) {
        asio::error_code ignored;
        socket.close(ignored);
        return;
    }

    vertical_client_settings current_;
    session_id id = 0;
    {
        const std::lock_guard lock(mutex_);
        current_ = settings_;
        id = next_session_++;
    }

    auto created = std::make_shared<detail::vertical_session>(
        io_.context(),
        id,
        detail::vertical_role::CLIENT,
        to_options(current_),
        info,
        make_handlers()
    );

    {
        const std::lock_guard lock(mutex_);
        session_ = created;
    }

    created->attach(std::make_shared<detail::connection>(std::move(socket), info));
}

void vertical_client::impl::handle_disconnect(session_id id, const error_info& error) {
    bool was_current = false;
    {
        const std::lock_guard lock(mutex_);
        if (session_ && session_->id() == id) {
            session_.reset();
            was_current = true;
        }
    }

    notify_disconnected(id, error);

    if (was_current && running_.load()) { connect(); }
}

detail::vertical_session::handlers vertical_client::impl::make_handlers() {
    detail::vertical_session::handlers callbacks;
    callbacks.connected_ = [this](session_id id, const connection_info& info) { notify_connected(id, info); };
    callbacks.disconnected_ = [this](session_id id, const error_info& error) { handle_disconnect(id, error); };
    callbacks.state_ = [this](session_id id, vertical_state state) { notify_state(id, state); };
    callbacks.message_ = [this](session_id id, vertical_state state, const any_message& message) {
        notify_message(id, state, message);
    };
    callbacks.trace_ = [this](session_id id, trace_type type, std::string_view text) { notify_trace(id, type, text); };
    return callbacks;
}

void vertical_client::impl::notify_connected(session_id id, const connection_info& info) {
    connected_handler handler;
    {
        const std::lock_guard lock(mutex_);
        handler = on_connected_;
    }
    if (handler) { handler(id, info); }
}

void vertical_client::impl::notify_disconnected(session_id id, const error_info& error) {
    disconnected_handler handler;
    {
        const std::lock_guard lock(mutex_);
        handler = on_disconnected_;
    }
    if (handler) { handler(id, error); }
}

void vertical_client::impl::notify_state(session_id id, vertical_state state) {
    vertical_state_handler handler;
    {
        const std::lock_guard lock(mutex_);
        handler = on_state_;
    }
    if (handler) { handler(id, state); }
}

void vertical_client::impl::notify_message(session_id id, vertical_state state, const any_message& message) {
    vertical_message_handler handler;
    {
        const std::lock_guard lock(mutex_);
        handler = on_message_;
    }
    if (handler) { handler(id, state, message); }
}

void vertical_client::impl::notify_trace(session_id id, trace_type type, std::string_view text) {
    trace_handler handler;
    {
        const std::lock_guard lock(mutex_);
        handler = on_trace_;
    }
    if (handler) { handler(id, type, text); }
}

void vertical_client::impl::notify_error(const error_info& error) {
    error_handler handler;
    {
        const std::lock_guard lock(mutex_);
        handler = on_error_;
    }
    if (handler) { handler(error); }
}

//==============================================================================
// vertical_client
//==============================================================================

vertical_client::vertical_client(vertical_client_settings settings_)
    : impl_(std::make_unique<impl>()) {
    impl_->settings_ = std::move(settings_);
}

vertical_client::~vertical_client() {
    if (impl_) { impl_->stop(std::nullopt); }
}

vertical_client::vertical_client(vertical_client&&) noexcept = default;

vertical_client& vertical_client::operator=(vertical_client&&) noexcept = default;

void vertical_client::start() { impl_->start(); }

void vertical_client::stop() { impl_->stop(std::nullopt); }

void vertical_client::stop(const notification& notification) { impl_->stop(notification); }

void vertical_client::apply(const vertical_client_settings& settings_) { impl_->apply(settings_); }

std::expected<void, error_info> vertical_client::send(session_id session_, const any_message& message) {
    std::shared_ptr<detail::vertical_session> target;
    {
        const std::lock_guard lock(impl_->mutex_);
        target = impl_->session_;
    }
    if (!target || target->id() != session_) {
        return std::unexpected(error_info { error_code::CLIENT, "unknown session_ " + std::to_string(session_) });
    }
    return target->send(message);
}

std::expected<void, error_info> vertical_client::send_raw(session_id session_, std::string_view xml) {
    std::shared_ptr<detail::vertical_session> target;
    {
        const std::lock_guard lock(impl_->mutex_);
        target = impl_->session_;
    }
    if (!target || target->id() != session_) {
        return std::unexpected(error_info { error_code::CLIENT, "unknown session_ " + std::to_string(session_) });
    }
    return target->send_raw(xml);
}

void vertical_client::post(std::function<void()> work) { asio::post(impl_->io_.context(), std::move(work)); }

void vertical_client::on_connected(connected_handler handler) {
    const std::lock_guard lock(impl_->mutex_);
    impl_->on_connected_ = std::move(handler);
}

void vertical_client::on_disconnected(disconnected_handler handler) {
    const std::lock_guard lock(impl_->mutex_);
    impl_->on_disconnected_ = std::move(handler);
}

void vertical_client::on_state(vertical_state_handler handler) {
    const std::lock_guard lock(impl_->mutex_);
    impl_->on_state_ = std::move(handler);
}

void vertical_client::on_message(vertical_message_handler handler) {
    const std::lock_guard lock(impl_->mutex_);
    impl_->on_message_ = std::move(handler);
}

void vertical_client::on_trace(trace_handler handler) {
    const std::lock_guard lock(impl_->mutex_);
    impl_->on_trace_ = std::move(handler);
}

void vertical_client::on_error(error_handler handler) {
    const std::lock_guard lock(impl_->mutex_);
    impl_->on_error_ = std::move(handler);
}

} // namespace ba7lya::hermes
