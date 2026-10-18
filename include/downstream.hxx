// SPDX-License-Identifier: MIT
///
/// @file downstream.hxx
/// @author BA7LYA (1042140025@qq.com)
/// @brief The downstream interface: the server a machine offers to its downstream neighbour.
/// @version 0.1
/// @date 2026-10-18
/// @copyright Copyright (c) 2026
///
/// A machine that hands boards over runs one downstream interface per lane and transport
/// interface: it listens on a TCP port, answers the downstream machine's service_description with
/// its own, offers boards with board_available, and reports transport_finished once a transfer is
/// done (The Hermes Standard 2.3.1 and 3.4 to 3.12). The other side of the connection is the
/// upstream facade.
///
/// One lane and transport interface carries one connection at a time. While a session is
/// established, a further connection is answered with a notification of code
/// `connection_refused` and closed again, while the established connection is retained (2.3.2).
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
/// @brief The downstream interface of The Hermes Standard.
///
/// The facade owns an I/O thread. Register the callbacks and call start() before the peer
/// connects; stop() closes the sessions and joins the thread. Every method may be called from any
/// thread, including from inside a callback.
///
class downstream {
public:
    /// @brief Creates the interface; call start() to listen.
    explicit downstream(downstream_settings settings = {});
    ~downstream();

    downstream(downstream&&) noexcept;
    downstream& operator=(downstream&&) noexcept;
    downstream(const downstream&) = delete;
    downstream& operator=(const downstream&) = delete;

    /// @brief Starts the I/O thread and listens on the configured address.
    void start();

    /// @brief Closes every session and stops the I/O thread.
    void stop();

    /// @brief Closes every session after sending @p notification, then stops the I/O thread.
    void stop(const notification& notification);

    /// @brief Replaces the settings; running sessions are closed and the listener is restarted.
    void apply(const downstream_settings& settings);

    /// @brief The port actually bound; useful when the settings asked for port 0.
    /// @return The bound port, or 0 while the interface is not listening.
    std::uint16_t port() const;

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
    /// @brief Registers the handler of listener errors; call before start().
    void on_error(error_handler handler);
    /// @brief Registers the handler of the bound listening port; call before start().
    void on_listening(listening_handler handler);

private:
    struct impl;
    std::unique_ptr<impl> impl_;
};

} // namespace ba7lya::hermes
