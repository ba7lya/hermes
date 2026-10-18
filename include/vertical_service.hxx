// SPDX-License-Identifier: MIT
///
/// @file vertical_service.hxx
/// @author BA7LYA (1042140025@qq.com)
/// @brief The vertical_service interface: the server a machine offers to supervisory systems.
/// @version 0.1
/// @date 2026-10-18
/// @copyright Copyright (c) 2026
///
/// The machine answers the supervisory system's supervisory_service_description with its own and
/// then serves the supervisory features both sides implement (The Hermes Standard 2.5).
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
/// @brief The vertical_service interface of The Hermes Standard.
///
/// The facade owns an I/O thread. Register the callbacks and call start() before a supervisory
/// system connects; stop() closes the sessions and joins the thread.
///
class vertical_service {
public:
    /// @brief Creates the interface; call start() to listen.
    explicit vertical_service(vertical_service_settings settings = {});
    ~vertical_service();

    vertical_service(vertical_service&&) noexcept;
    vertical_service& operator=(vertical_service&&) noexcept;
    vertical_service(const vertical_service&) = delete;
    vertical_service& operator=(const vertical_service&) = delete;

    /// @brief Starts the I/O thread and listens on the configured address.
    void start();

    /// @brief Closes every session and stops the I/O thread.
    void stop();

    /// @brief Closes every session after sending @p notification, then stops the I/O thread.
    void stop(const notification& notification);

    /// @brief Replaces the settings; running sessions are closed and the listener is restarted.
    void apply(const vertical_service_settings& settings);

    /// @brief The port actually bound; useful when the settings asked for port 0.
    std::uint16_t port() const;

    /// @brief Queues @p message on @p session.
    std::expected<void, error_info> send(session_id session, const any_message& message);

    /// @brief Queues the message @p message on @p session.
    template<message M>
    std::expected<void, error_info> send(session_id session, const M& message) {
        return send(session, any_message { message });
    }

    /// @brief Sends @p message to every connected session.
    void broadcast(const any_message& message);

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
    /// @brief Registers the handler of the bound listening port; call before start().
    void on_listening(listening_handler handler);

private:
    struct impl;
    std::unique_ptr<impl> impl_;
};

} // namespace ba7lya::hermes
