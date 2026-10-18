// SPDX-License-Identifier: MIT
///
/// @file configuration_service.cxx
/// @author BA7LYA (1042140025@qq.com)
/// @brief The configuration_service facade.
/// @version 0.1
/// @date 2026-10-18
/// @copyright Copyright (c) 2026
///

#include "configuration_service.hxx"

#include <asio.hpp>
#include <atomic>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "configuration.hxx"
#include "net.hxx"

namespace ba7lya::hermes {

struct configuration_service::impl {
    void start();
    void stop();
    void apply(configuration_service_settings updated);
    void listen();
    void handle_accept(asio::ip::tcp::socket socket, connection_info info);
    void shutdown_sessions();
    detail::configuration_session::handlers make_handlers();

    void notify_connected(session_id id, const connection_info& info);
    void notify_disconnected(session_id id, const error_info& error);
    void notify_trace(session_id id, trace_type type, std::string_view text);
    void notify_error(const error_info& error);

    error_info handle_set(session_id id, const connection_info& info, const set_configuration& value);
    current_configuration handle_get(session_id id, const connection_info& info);

    mutable std::mutex mutex_;
    configuration_service_settings settings_;
    detail::io_runner io_;
    std::shared_ptr<detail::acceptor> acceptor_;
    std::unordered_map<session_id, std::shared_ptr<detail::configuration_session>> sessions_;
    session_id next_session_ { 1 };
    std::atomic<bool> running_ { false };
    std::atomic<std::uint16_t> bound_port_ { 0 };

    connected_handler on_connected_;
    disconnected_handler on_disconnected_;
    trace_handler on_trace_;
    error_handler on_error_;
    listening_handler on_listening_;
    set_handler on_set_;
    get_handler on_get_;
};

void configuration_service::impl::start() {
    if (running_.exchange(true)) { return; }

    io_.start();
    asio::post(io_.context(), [this]() { listen(); });
}

void configuration_service::impl::stop() {
    if (!running_.exchange(false)) {
        io_.halt();
        return;
    }

    if (io_.on_thread()) {
        shutdown_sessions();
        io_.retire();
        return;
    }

    asio::post(io_.context(), [this]() { shutdown_sessions(); });
    io_.join();
}

void configuration_service::impl::apply(configuration_service_settings updated) {
    const bool was_running = running_.load();

    {
        const std::lock_guard lock(mutex_);
        settings_ = std::move(updated);
    }

    if (!was_running) { return; }

    asio::post(io_.context(), [this]() {
        if (!running_.load()) { return; }
        shutdown_sessions();
        listen();
    });
}

void configuration_service::impl::listen() {
    if (!running_.load()) { return; }

    configuration_service_settings current_;
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

void configuration_service::impl::handle_accept(asio::ip::tcp::socket socket, connection_info info) {
    if (!running_.load()) {
        asio::error_code ignored;
        socket.close(ignored);
        return;
    }

    session_id id = 0;
    {
        const std::lock_guard lock(mutex_);
        id = next_session_++;
    }

    auto session_ = std::make_shared<detail::configuration_session>(io_.context(), id, info, make_handlers());

    {
        const std::lock_guard lock(mutex_);
        sessions_.emplace(id, session_);
    }

    session_->attach(std::make_shared<detail::connection>(std::move(socket), info));
}

void configuration_service::impl::shutdown_sessions() {
    std::vector<std::shared_ptr<detail::configuration_session>> opened;
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
    for (const auto& session_ : opened) { session_->shutdown(); }
}

detail::configuration_session::handlers configuration_service::impl::make_handlers() {
    detail::configuration_session::handlers callbacks;
    callbacks.connected_ = [this](session_id id, const connection_info& info) { notify_connected(id, info); };
    callbacks.disconnected_ = [this](session_id id, const error_info& error) {
        {
            const std::lock_guard lock(mutex_);
            sessions_.erase(id);
        }
        notify_disconnected(id, error);
    };
    callbacks.set_ = [this](session_id id, const connection_info& info, const set_configuration& value) {
        return handle_set(id, info, value);
    };
    callbacks.get_ = [this](session_id id, const connection_info& info) { return handle_get(id, info); };
    callbacks.trace_ = [this](session_id id, trace_type type, std::string_view text) { notify_trace(id, type, text); };
    return callbacks;
}

error_info configuration_service::impl::handle_set(
    session_id id,
    const connection_info& info,
    const set_configuration& value
) {
    set_handler handler;
    {
        const std::lock_guard lock(mutex_);
        handler = on_set_;
    }
    if (!handler) {
        return error_info { error_code::IMPLEMENTATION, "the configuration service does not accept writes" };
    }
    return handler(id, info, value);
}

current_configuration configuration_service::impl::handle_get(session_id id, const connection_info& info) {
    get_handler handler;
    {
        const std::lock_guard lock(mutex_);
        handler = on_get_;
    }
    if (!handler) { return current_configuration {}; }
    return handler(id, info);
}

void configuration_service::impl::notify_connected(session_id id, const connection_info& info) {
    connected_handler handler;
    {
        const std::lock_guard lock(mutex_);
        handler = on_connected_;
    }
    if (handler) { handler(id, info); }
}

void configuration_service::impl::notify_disconnected(session_id id, const error_info& error) {
    disconnected_handler handler;
    {
        const std::lock_guard lock(mutex_);
        handler = on_disconnected_;
    }
    if (handler) { handler(id, error); }
}

void configuration_service::impl::notify_trace(session_id id, trace_type type, std::string_view text) {
    trace_handler handler;
    {
        const std::lock_guard lock(mutex_);
        handler = on_trace_;
    }
    if (handler) { handler(id, type, text); }
}

void configuration_service::impl::notify_error(const error_info& error) {
    error_handler handler;
    {
        const std::lock_guard lock(mutex_);
        handler = on_error_;
    }
    if (handler) { handler(error); }
}

//==============================================================================
// configuration_service
//==============================================================================

configuration_service::configuration_service(configuration_service_settings settings_)
    : impl_(std::make_unique<impl>()) {
    impl_->settings_ = std::move(settings_);
}

configuration_service::~configuration_service() {
    if (impl_) { impl_->stop(); }
}

configuration_service::configuration_service(configuration_service&&) noexcept = default;

configuration_service& configuration_service::operator=(configuration_service&&) noexcept = default;

void configuration_service::start() { impl_->start(); }

void configuration_service::stop() { impl_->stop(); }

void configuration_service::apply(const configuration_service_settings& settings_) { impl_->apply(settings_); }

std::uint16_t configuration_service::port() const { return impl_->bound_port_.load(std::memory_order_relaxed); }

void configuration_service::on_set_configuration(set_handler handler) {
    const std::lock_guard lock(impl_->mutex_);
    impl_->on_set_ = std::move(handler);
}

void configuration_service::on_get_configuration(get_handler handler) {
    const std::lock_guard lock(impl_->mutex_);
    impl_->on_get_ = std::move(handler);
}

void configuration_service::on_connected(connected_handler handler) {
    const std::lock_guard lock(impl_->mutex_);
    impl_->on_connected_ = std::move(handler);
}

void configuration_service::on_disconnected(disconnected_handler handler) {
    const std::lock_guard lock(impl_->mutex_);
    impl_->on_disconnected_ = std::move(handler);
}

void configuration_service::on_trace(trace_handler handler) {
    const std::lock_guard lock(impl_->mutex_);
    impl_->on_trace_ = std::move(handler);
}

void configuration_service::on_error(error_handler handler) {
    const std::lock_guard lock(impl_->mutex_);
    impl_->on_error_ = std::move(handler);
}

void configuration_service::on_listening(listening_handler handler) {
    std::uint16_t bound = 0;
    {
        const std::lock_guard lock(impl_->mutex_);
        impl_->on_listening_ = handler;
        bound = impl_->bound_port_;
    }
    if (bound != 0 && handler) { handler(bound); }
}

} // namespace ba7lya::hermes
