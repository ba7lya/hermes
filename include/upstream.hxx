// SPDX-License-Identifier: MIT
///
/// @file upstream.hxx
/// @author BA7LYA (1042140025@qq.com)
/// @brief The upstream interface: the client a machine runs towards its upstream neighbour.
/// @version 0.1
/// @date 2026-10-18
/// @copyright Copyright (c) 2026
///
/// A machine that receives boards runs one upstream interface per lane and transport interface:
/// it connects to the upstream machine, sends its service_description first, answers board_available
/// with machine_ready, and drives the handover with start_transport and stop_transport (The Hermes
/// Standard 2.3.1 and 3.4 to 3.12). The other side of the connection is the downstream facade.
///

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string_view>
#include <utility>

#include "channel.hxx"
#include "error.hxx"
#include "expected.hxx"
#include "messages.hxx"
#include "settings.hxx"

namespace ba7lya::hermes {

///
/// @brief The upstream interface of The Hermes Standard.
///
/// The facade owns an I/O thread and reconnects cyclically until stop() is called. Register the
/// callbacks before start(); every method may be called from any thread.
///
class upstream {
public:
    /// @brief Creates the interface; call start() to connect.
    explicit upstream(upstream_settings settings = {});
    ~upstream();

    upstream(upstream&&) noexcept;
    upstream& operator=(upstream&&) noexcept;
    upstream(const upstream&) = delete;
    upstream& operator=(const upstream&) = delete;

    /// @brief Starts the I/O thread and the cyclic connect attempts.
    void start();

    /// @brief Closes the session, stops reconnecting and stops the I/O thread.
    void stop();

    /// @brief Closes the session after sending @p notification, then stops the I/O thread.
    void stop(const notification& notification);

    /// @brief Replaces the settings; the running session is closed and a new connection attempted.
    void apply(const upstream_settings& settings);

    /// @brief Queues @p message on @p session.
    /// @return An error_info when the session is unknown or the interface may not send this message.
    std::expected<void, error_info> send(session_id session, const any_message& message);

    /// @brief Queues the message @p message on @p session; see send(session_id, const any_message&).
    template<message M>
    std::expected<void, error_info> send(session_id session, const M& message) {
        return send(session, any_message { message });
    }

    /// @brief Writes @p xml to @p session verbatim, bypassing serialization and state checks.
    std::expected<void, error_info> send_raw(session_id session, std::string_view xml);

    /// @brief Queues @p work to run on the I/O thread.
    void post(std::function<void()> work);

    /// @brief Registers the handler of a new session; call before start().
    void on_connected(connected_handler handler);
    /// @brief Registers the handler of a closed session; call before start().
    void on_disconnected(disconnected_handler handler);
    /// @brief Registers the handler of a state change; call before start().
    void on_state(state_handler handler);
    /// @brief Registers the handler of an accepted message; call before start().
    void on_message(message_handler handler);
    /// @brief Registers the handler of the raw documents and diagnostics; call before start().
    void on_trace(trace_handler handler);
    /// @brief Registers the handler of connection errors; call before start().
    void on_error(error_handler handler);

private:
    struct impl;
    std::unique_ptr<impl> impl_;
};

} // namespace ba7lya::hermes
