// SPDX-License-Identifier: MIT
///
/// @file configuration.hxx
/// @author BA7LYA (1042140025@qq.com)
/// @brief The session of the configuration service.
/// @version 0.1
/// @date 2026-10-18
/// @copyright Copyright (c) 2026
///

#pragma once

#include <asio.hpp>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

#include "channel.hxx"
#include "messages.hxx"
#include "net.hxx"
#include "settings.hxx"

namespace ba7lya::hermes::detail {

///
/// @brief One connection of the configuration service.
///
/// The configuration protocol of The Hermes Standard 2.4 has no handshake: the client sends
/// get_configuration or set_configuration, the service answers with current_configuration or a
/// notification. A session serves one client until it disconnects.
///
class configuration_session : public std::enable_shared_from_this<configuration_session> {
public:
    using set_handler = std::function<error_info(session_id, const connection_info&, const set_configuration&)>;
    using get_handler = std::function<current_configuration(session_id, const connection_info&)>;

    struct handlers {
        connected_handler connected_;
        disconnected_handler disconnected_;
        set_handler set_;
        get_handler get_;
        trace_handler trace_;
    };

    configuration_session(asio::io_context& context, session_id id, connection_info info, handlers callbacks);
    ~configuration_session();

    configuration_session(const configuration_session&) = delete;
    configuration_session& operator=(const configuration_session&) = delete;

    void attach(const std::shared_ptr<connection>& socket);
    void shutdown();
    void close(const error_info& reason);

    session_id id() const noexcept { return id_; }

    const connection_info& info() const noexcept { return info_; }

    bool closed() const noexcept { return closed_.load(std::memory_order_relaxed); }

private:
    void on_frame(std::string document);
    void on_closed(const error_info& error);
    void handle(const any_message& message);
    void handle_set(const set_configuration& value);
    void handle_get();
    void send(const any_message& message);
    void notify_error(const notification& notification, bool close_after);
    void trace(trace_type type, std::string_view text) const noexcept;
    std::shared_ptr<connection> connection_now() const;

    asio::io_context& context_;
    session_id id_;
    connection_info info_;
    handlers handlers_;

    mutable std::mutex mutex_;
    std::shared_ptr<connection> connection_;
    std::atomic<bool> closed_ { false };
    std::atomic<bool> closing_ { false };
};

} // namespace ba7lya::hermes::detail
