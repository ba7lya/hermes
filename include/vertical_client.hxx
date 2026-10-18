// SPDX-License-Identifier: MIT
///
/// @file vertical_client.hxx
/// @author BA7LYA (1042140025@qq.com)
/// @brief The vertical_client interface: the client a supervisory system runs.
/// @version 0.1
/// @date 2026-10-18
/// @copyright Copyright (c) 2026
///
/// The supervisory system connects to a machine, sends its supervisory_service_description first,
/// and then uses the supervisory features both sides implement (The Hermes Standard 2.5).
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
/// @brief The vertical_client interface of The Hermes Standard.
///
/// The facade owns an I/O thread and reconnects cyclically until stop() is called.
///
class vertical_client {
public:
    /// @brief Creates the interface; call start() to connect.
    explicit vertical_client(vertical_client_settings settings = {});
    ~vertical_client();

    vertical_client(vertical_client&&) noexcept;
    vertical_client& operator=(vertical_client&&) noexcept;
    vertical_client(const vertical_client&) = delete;
    vertical_client& operator=(const vertical_client&) = delete;

    /// @brief Starts the I/O thread and the cyclic connect attempts.
    void start();

    /// @brief Closes the session, stops reconnecting and stops the I/O thread.
    void stop();

    /// @brief Closes the session after sending @p notification, then stops the I/O thread.
    void stop(const notification& notification);

    /// @brief Replaces the settings; the running session is closed and a new connection attempted.
    void apply(const vertical_client_settings& settings);

    /// @brief Queues @p message on @p session.
    std::expected<void, error_info> send(session_id session, const any_message& message);

    /// @brief Queues the message @p message on @p session.
    template<message M>
    std::expected<void, error_info> send(session_id session, const M& message) {
        return send(session, any_message { message });
    }

    /// @brief Writes @p xml to @p session verbatim, bypassing serialization and state checks.
    std::expected<void, error_info> send_raw(session_id session, std::string_view xml);

    /// @brief Queues @p work to run on the I/O thread.
    void post(std::function<void()> work);

    void on_connected(connected_handler handler);
    void on_disconnected(disconnected_handler handler);
    void on_state(vertical_state_handler handler);
    void on_message(vertical_message_handler handler);
    void on_trace(trace_handler handler);
    void on_error(error_handler handler);

private:
    struct impl;
    std::unique_ptr<impl> impl_;
};

} // namespace ba7lya::hermes
