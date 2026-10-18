// SPDX-License-Identifier: MIT
///
/// @file horizontal.hxx
/// @author BA7LYA (1042140025@qq.com)
/// @brief The state chart and the session of the horizontal channel.
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
/// @brief Which side of the horizontal connection a session is.
/// @note downstream is the server that provides boards (board_available), upstream is the client
/// that receives them (machine_ready); the names follow the interface, not the machine.
///
enum class horizontal_role { DOWNSTREAM, UPSTREAM };

///
/// @brief One resolved set of interface settings, shared by both horizontal facades.
///
struct horizontal_options {
    std::string machine_id_;
    std::string interface_id_;
    std::uint16_t lane_id_ {};
    supported_features features_;
    std::chrono::milliseconds check_alive_interval_ {};
    std::chrono::milliseconds check_alive_timeout_ {};
    state_check state_check_ { state_check::ALL };
};

//==============================================================================
// the state chart of Figure 19
//==============================================================================

/// @brief Whether @p tag may travel in any state without a transition.
bool is_anytime_message(std::string_view tag);

/// @brief Whether @p tag is one of the messages the state chart governs.
bool needs_transition(std::string_view tag);

/// @brief The state @p tag moves the interface to, or nothing when it triggers no transition.
std::optional<channel_state> next_state(channel_state current, std::string_view tag);

/// @brief Whether the interface with @p role may send @p tag at all.
bool role_may_send(horizontal_role role, std::string_view tag);

/// @brief Whether the interface with @p role may receive @p tag at all.
bool role_may_receive(horizontal_role role, std::string_view tag);

//==============================================================================
// session
//==============================================================================

///
/// @brief One connection of the horizontal channel.
///
/// The session performs the handshake, validates every document against the state chart of
/// Figure 19, answers check_alive pings, and reports what the application needs to see. Every
/// callback runs on the I/O thread; send() and shutdown() may be called from any thread.
///
class horizontal_session : public std::enable_shared_from_this<horizontal_session> {
public:
    struct handlers {
        connected_handler connected_;
        disconnected_handler disconnected_;
        state_handler state_;
        message_handler message_;
        trace_handler trace_;
    };

    horizontal_session(
        asio::io_context& context,
        session_id id,
        horizontal_role role,
        horizontal_options options,
        connection_info info,
        handlers callbacks
    );
    ~horizontal_session();

    horizontal_session(const horizontal_session&) = delete;
    horizontal_session& operator=(const horizontal_session&) = delete;

    /// @brief Starts the session on an established socket; runs on the I/O thread.
    void attach(const std::shared_ptr<connection>& socket);

    /// @brief Queues @p message; the caller may be any thread.
    std::expected<void, error_info> send(const any_message& message);

    /// @brief Queues @p xml verbatim, bypassing serialization and the state chart.
    std::expected<void, error_info> send_raw(std::string_view xml);

    /// @brief Sends @p notification and closes, or just closes when it is empty.
    void shutdown(const std::optional<notification>& notification);

    /// @brief Closes the session now; runs on the I/O thread.
    void close(const error_info& reason);

    session_id id() const noexcept { return id_; }

    channel_state state() const noexcept { return state_.load(std::memory_order_relaxed); }

    const connection_info& info() const noexcept { return info_; }

    bool closed() const noexcept { return closed_.load(std::memory_order_relaxed); }

private:
    void on_frame(std::string document);
    void on_closed(const error_info& error);

    void handle(any_message message);
    void handle_service_description(const service_description& value, const any_message& message);
    void handle_check_alive(const check_alive& value);
    bool check_board_id(const any_message& message);

    void write(const any_message& message);
    void write_document(const std::string& document);
    void send_service_description();

    void change_state(channel_state next);
    void notify_message(channel_state state, const any_message& message);
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
    horizontal_role role_;
    horizontal_options options_;
    connection_info info_;
    handlers handlers_;

    mutable std::mutex mutex_;
    std::shared_ptr<connection> connection_;

    std::atomic<channel_state> state_ { channel_state::NOT_CONNECTED };
    std::atomic<bool> closed_ { false };
    std::atomic<bool> closing_ { false };

    asio::steady_timer ping_timer_;
    asio::steady_timer pong_timer_;
    bool ping_started_ { false };
    std::uint64_t ping_counter_ { 0 };
    std::string pending_ping_id_;
    bool peer_supports_check_alive_response_ { false };

    std::string start_board_id_;
    std::string last_board_id_;
};

} // namespace ba7lya::hermes::detail
