// SPDX-License-Identifier: MIT
///
/// @file vertical_service.cxx
/// @author BA7LYA (1042140025@qq.com)
/// @brief The vertical_service facade: the server a machine offers to supervisory systems.
/// @version 0.1
/// @date 2026-10-18
/// @copyright Copyright (c) 2026
///

#include "vertical_service.hxx"

#include <asio.hpp>
#include <atomic>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "net.hxx"
#include "vertical.hxx"

namespace ba7lya::hermes {

namespace {

detail::vertical_options to_options(const vertical_service_settings& settings_) {
    detail::vertical_options options;
    options.system_id_ = settings_.system_id_;
    options.features_ = settings_.features_;
    options.check_alive_interval_ = settings_.check_alive_interval_;
    options.check_alive_timeout_ = settings_.check_alive_timeout_;
    return options;
}

} // namespace

struct vertical_service::impl {
    void start();
    void stop(const std::optional<notification>& notification);
    void apply(vertical_service_settings updated);
    void listen();
    void handle_accept(asio::ip::tcp::socket socket, connection_info info);
    void shutdown_sessions(const std::optional<notification>& notification);
    std::shared_ptr<detail::vertical_session> find(session_id id) const;
    detail::vertical_session::handlers make_handlers();

    void notify_connected(session_id id, const connection_info& info);
    void notify_disconnected(session_id id, const error_info& error);
    void notify_state(session_id id, vertical_state state);
    void notify_message(session_id id, vertical_state state, const any_message& message);
    void notify_trace(session_id id, trace_type type, std::string_view text);
    void notify_error(const error_info& error);

    mutable std::mutex mutex_;
    vertical_service_settings settings_;
    detail::io_runner io_;
    std::shared_ptr<detail::acceptor> acceptor_;
    std::unordered_map<session_id, std::shared_ptr<detail::vertical_session>> sessions_;
    session_id next_session_ { 1 };
    std::atomic<bool> running_ { false };
    std::atomic<std::uint16_t> bound_port_ { 0 };

    connected_handler on_connected_;
    disconnected_handler on_disconnected_;
    vertical_state_handler on_state_;
    vertical_message_handler on_message_;
    trace_handler on_trace_;
    error_handler on_error_;
    listening_handler on_listening_;
};

void vertical_service::impl::start() {
    if (running_.exchange(true)) { return; }

    io_.start();
    asio::post(io_.context(), [this]() { listen(); });
}

void vertical_service::impl::stop(const std::optional<notification>& notification) {
    if (!running_.exchange(false)) {
        io_.halt();
        return;
    }

    if (io_.on_thread()) {
        shutdown_sessions(notification);
        io_.retire();
        return;
    }

    asio::post(io_.context(), [this, notification]() { shutdown_sessions(notification); });
    io_.join();
}

void vertical_service::impl::apply(vertical_service_settings updated) {
    const bool was_running = running_.load();

    {
        const std::lock_guard lock(mutex_);
        settings_ = std::move(updated);
    }

    if (!was_running) { return; }

    asio::post(io_.context(), [this]() {
        if (!running_.load()) { return; }
        shutdown_sessions(
            notification { notification_code::CONNECTION_RESET, severity::INFO, "the configuration changed" }
        );
        listen();
    });
}

void vertical_service::impl::listen() {
    if (!running_.load()) { return; }

    vertical_service_settings current_;
    {
        const std::lock_guard lock(mutex_);
        current_ = settings_;
    }

    auto server = std::make_shared<detail::acceptor>(io_.context(), current_.address_, current_.port_);
    if (const auto result = server->listen(); !result) {
        notify_error(result.error());
        return;
    }

    listening_handler listener;
    std::uint16_t bound = 0;
    {
        const std::lock_guard lock(mutex_);
        acceptor_ = server;
        bound_port_ = server->port();
        listener = on_listening_;
        bound = bound_port_;
    }
    if (listener) { listener(bound); }

    server->start(
        [this](asio::ip::tcp::socket socket, connection_info info) {
            handle_accept(std::move(socket), std::move(info));
        },
        [this](const error_info& error) { notify_error(error); }
    );
}

void vertical_service::impl::handle_accept(asio::ip::tcp::socket socket, connection_info info) {
    if (!running_.load()) {
        asio::error_code ignored;
        socket.close(ignored);
        return;
    }

    vertical_service_settings current_;
    session_id id = 0;
    {
        const std::lock_guard lock(mutex_);
        current_ = settings_;
        id = next_session_++;
    }

    auto session_ = std::make_shared<detail::vertical_session>(
        io_.context(),
        id,
        detail::vertical_role::SERVICE,
        to_options(current_),
        info,
        make_handlers()
    );

    {
        const std::lock_guard lock(mutex_);
        sessions_.emplace(id, session_);
    }

    session_->attach(std::make_shared<detail::connection>(std::move(socket), info));
}

void vertical_service::impl::shutdown_sessions(const std::optional<notification>& notification) {
    std::vector<std::shared_ptr<detail::vertical_session>> opened;
    std::shared_ptr<detail::acceptor> server;
    {
        const std::lock_guard lock(mutex_);
        server = std::move(acceptor_);
        acceptor_.reset();
        opened.reserve(sessions_.size());
        for (const auto& entry : sessions_) { opened.push_back(entry.second); }
        sessions_.clear();
    }

    bound_port_ = 0;
    if (server) { server->stop(); }
    for (const auto& session_ : opened) { session_->shutdown(notification); }
}

std::shared_ptr<detail::vertical_session> vertical_service::impl::find(session_id id) const {
    const std::lock_guard lock(mutex_);
    const auto found = sessions_.find(id);
    return found == sessions_.end() ? nullptr : found->second;
}

detail::vertical_session::handlers vertical_service::impl::make_handlers() {
    detail::vertical_session::handlers callbacks;
    callbacks.connected_ = [this](session_id id, const connection_info& info) { notify_connected(id, info); };
    callbacks.disconnected_ = [this](session_id id, const error_info& error) {
        {
            const std::lock_guard lock(mutex_);
            sessions_.erase(id);
        }
        notify_disconnected(id, error);
    };
    callbacks.state_ = [this](session_id id, vertical_state state) { notify_state(id, state); };
    callbacks.message_ = [this](session_id id, vertical_state state, const any_message& message) {
        notify_message(id, state, message);
    };
    callbacks.trace_ = [this](session_id id, trace_type type, std::string_view text) { notify_trace(id, type, text); };
    return callbacks;
}

void vertical_service::impl::notify_connected(session_id id, const connection_info& info) {
    connected_handler handler;
    {
        const std::lock_guard lock(mutex_);
        handler = on_connected_;
    }
    if (handler) { handler(id, info); }
}

void vertical_service::impl::notify_disconnected(session_id id, const error_info& error) {
    disconnected_handler handler;
    {
        const std::lock_guard lock(mutex_);
        handler = on_disconnected_;
    }
    if (handler) { handler(id, error); }
}

void vertical_service::impl::notify_state(session_id id, vertical_state state) {
    vertical_state_handler handler;
    {
        const std::lock_guard lock(mutex_);
        handler = on_state_;
    }
    if (handler) { handler(id, state); }
}

void vertical_service::impl::notify_message(session_id id, vertical_state state, const any_message& message) {
    vertical_message_handler handler;
    {
        const std::lock_guard lock(mutex_);
        handler = on_message_;
    }
    if (handler) { handler(id, state, message); }
}

void vertical_service::impl::notify_trace(session_id id, trace_type type, std::string_view text) {
    trace_handler handler;
    {
        const std::lock_guard lock(mutex_);
        handler = on_trace_;
    }
    if (handler) { handler(id, type, text); }
}

void vertical_service::impl::notify_error(const error_info& error) {
    error_handler handler;
    {
        const std::lock_guard lock(mutex_);
        handler = on_error_;
    }
    if (handler) { handler(error); }
}

//==============================================================================
// vertical_service
//==============================================================================

vertical_service::vertical_service(vertical_service_settings settings_)
    : impl_(std::make_unique<impl>()) {
    impl_->settings_ = std::move(settings_);
}

vertical_service::~vertical_service() {
    if (impl_) { impl_->stop(std::nullopt); }
}

vertical_service::vertical_service(vertical_service&&) noexcept = default;

vertical_service& vertical_service::operator=(vertical_service&&) noexcept = default;

void vertical_service::start() { impl_->start(); }

void vertical_service::stop() { impl_->stop(std::nullopt); }

void vertical_service::stop(const notification& notification) { impl_->stop(notification); }

void vertical_service::apply(const vertical_service_settings& settings_) { impl_->apply(settings_); }

std::uint16_t vertical_service::port() const { return impl_->bound_port_.load(std::memory_order_relaxed); }

std::expected<void, error_info> vertical_service::send(session_id session_, const any_message& message) {
    const auto target = impl_->find(session_);
    if (!target) {
        return std::unexpected(error_info { error_code::CLIENT, "unknown session_ " + std::to_string(session_) });
    }
    return target->send(message);
}

void vertical_service::broadcast(const any_message& message) {
    std::vector<std::shared_ptr<detail::vertical_session>> targets;
    {
        const std::lock_guard lock(impl_->mutex_);
        targets.reserve(impl_->sessions_.size());
        for (const auto& entry : impl_->sessions_) { targets.push_back(entry.second); }
    }
    for (const auto& target : targets) { (void)target->send(message); }
}

std::expected<void, error_info> vertical_service::send_raw(session_id session_, std::string_view xml) {
    const auto target = impl_->find(session_);
    if (!target) {
        return std::unexpected(error_info { error_code::CLIENT, "unknown session_ " + std::to_string(session_) });
    }
    return target->send_raw(xml);
}

void vertical_service::post(std::function<void()> work) { asio::post(impl_->io_.context(), std::move(work)); }

void vertical_service::on_connected(connected_handler handler) {
    const std::lock_guard lock(impl_->mutex_);
    impl_->on_connected_ = std::move(handler);
}

void vertical_service::on_disconnected(disconnected_handler handler) {
    const std::lock_guard lock(impl_->mutex_);
    impl_->on_disconnected_ = std::move(handler);
}

void vertical_service::on_state(vertical_state_handler handler) {
    const std::lock_guard lock(impl_->mutex_);
    impl_->on_state_ = std::move(handler);
}

void vertical_service::on_message(vertical_message_handler handler) {
    const std::lock_guard lock(impl_->mutex_);
    impl_->on_message_ = std::move(handler);
}

void vertical_service::on_trace(trace_handler handler) {
    const std::lock_guard lock(impl_->mutex_);
    impl_->on_trace_ = std::move(handler);
}

void vertical_service::on_error(error_handler handler) {
    const std::lock_guard lock(impl_->mutex_);
    impl_->on_error_ = std::move(handler);
}

void vertical_service::on_listening(listening_handler handler) {
    std::uint16_t bound = 0;
    {
        const std::lock_guard lock(impl_->mutex_);
        impl_->on_listening_ = handler;
        bound = impl_->bound_port_;
    }
    if (bound != 0 && handler) { handler(bound); }
}

} // namespace ba7lya::hermes
