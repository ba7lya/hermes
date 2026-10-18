// SPDX-License-Identifier: MIT
///
/// @file vertical.hxx
/// @author BA7LYA (1042140025@qq.com)
/// @brief The state chart and the session of the vertical channel.
/// @version 0.1
/// @date 2026-10-18
/// @copyright Copyright (c) 2026
///

#pragma once

#include <asio.hpp>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

#include "channel.hxx"
#include "enums.hxx"
#include "expected.hxx"
#include "messages.hxx"
#include "net.hxx"
#include "settings.hxx"

namespace ba7lya::hermes::detail {

///
/// @brief Which side of the vertical connection a session is.
/// @note service is the machine that offers the server, client is the supervisory system.
///
enum class vertical_role { SERVICE, CLIENT };

///
/// @brief One resolved set of vertical interface settings.
///
struct vertical_options {
    std::string system_id_;
    supervisory_features features_;
    std::chrono::milliseconds check_alive_interval_ {};
    std::chrono::milliseconds check_alive_timeout_ {};
};

//==============================================================================
// the state chart of Figure 22
//==============================================================================

/// @brief Whether @p tag may travel without a transition once the handshake is done.
bool vertical_is_anytime_message(std::string_view tag);

/// @brief The state @p tag moves the interface to, or nothing when it triggers no transition.
std::optional<vertical_state> vertical_next_state(vertical_state current, std::string_view tag);

/// @brief Whether the interface with @p role may send @p tag at all.
bool vertical_role_may_send(vertical_role role, std::string_view tag);

/// @brief Whether the interface with @p role may receive @p tag at all.
bool vertical_role_may_receive(vertical_role role, std::string_view tag);

//==============================================================================
// session
//==============================================================================

///
/// @brief One connection of the vertical channel.
///
class vertical_session : public std::enable_shared_from_this<vertical_session> {
public:
    struct handlers {
        connected_handler connected_;
        disconnected_handler disconnected_;
        vertical_state_handler state_;
        vertical_message_handler message_;
        trace_handler trace_;
    };

    vertical_session(
        asio::io_context& context,
        session_id id,
        vertical_role role,
        vertical_options options,
        connection_info info,
        handlers callbacks
    );
    ~vertical_session();

    vertical_session(const vertical_session&) = delete;
    vertical_session& operator=(const vertical_session&) = delete;

    void attach(const std::shared_ptr<connection>& socket);
    std::expected<void, error_info> send(const any_message& message);
    std::expected<void, error_info> send_raw(std::string_view xml);
    void shutdown(const std::optional<notification>& notification);
    void close(const error_info& reason);

    session_id id() const noexcept { return id_; }

    vertical_state state() const noexcept { return state_.load(std::memory_order_relaxed); }

    const connection_info& info() const noexcept { return info_; }

    bool closed() const noexcept { return closed_.load(std::memory_order_relaxed); }

private:
    void on_frame(std::string document);
    void on_closed(const error_info& error);
    void handle(any_message message);
    void handle_service_description(const supervisory_service_description& value, const any_message& message);
    void handle_check_alive(const check_alive& value);
    void send_service_description();
    void write(const any_message& message);
    void change_state(vertical_state next);
    void notify_message(vertical_state state, const any_message& message);
    void trace(trace_type type, std::string_view text) const noexcept;
    void protocol_error(std::string text);
    void start_check_alive();
    void schedule_ping();
    void on_ping();
    void arm_pong_timeout();
    void on_pong_timeout();
    std::shared_ptr<connection> connection_now() const;

    asio::io_context& context_;
    session_id id_;
    vertical_role role_;
    vertical_options options_;
    connection_info info_;
    handlers handlers_;

    mutable std::mutex mutex_;
    std::shared_ptr<connection> connection_;

    std::atomic<vertical_state> state_ { vertical_state::NOT_CONNECTED };
    std::atomic<bool> closed_ { false };
    std::atomic<bool> closing_ { false };

    asio::steady_timer ping_timer_;
    asio::steady_timer pong_timer_;
    bool ping_started_ { false };
    std::uint64_t ping_counter_ { 0 };
    std::string pending_ping_id_;
    bool peer_supports_check_alive_response_ { false };
};

} // namespace ba7lya::hermes::detail
