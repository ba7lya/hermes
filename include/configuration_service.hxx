// SPDX-License-Identifier: MIT
///
/// @file configuration_service.hxx
/// @author BA7LYA (1042140025@qq.com)
/// @brief The configuration_service interface: the endpoint a machine offers on port 1248.
/// @version 0.1
/// @date 2026-10-18
/// @copyright Copyright (c) 2026
///
/// The configuration service accepts get_configuration and set_configuration from an engineering
/// station and answers with current_configuration, a notification, or both (The Hermes Standard 2.4).
///

#pragma once

#include <functional>
#include <memory>

#include "channel.hxx"
#include "error.hxx"
#include "messages.hxx"
#include "settings.hxx"
#include "types.hxx"

namespace ba7lya::hermes {

///
/// @brief The configuration_service interface of The Hermes Standard.
///
/// The facade owns an I/O thread. Register the two handlers and call start() before a client
/// connects; stop() closes the sessions and joins the thread.
///
class configuration_service {
public:
    /// @brief Handles a set_configuration; the returned error_info makes the service answer with a
    /// notification instead of applying the configuration.
    using set_handler = std::function<error_info(session_id, const connection_info&, const set_configuration&)>;
    /// @brief Handles a get_configuration; the returned value is sent as current_configuration.
    using get_handler = std::function<current_configuration(session_id, const connection_info&)>;

    /// @brief Creates the interface; call start() to listen.
    explicit configuration_service(configuration_service_settings settings = {});
    ~configuration_service();

    configuration_service(configuration_service&&) noexcept;
    configuration_service& operator=(configuration_service&&) noexcept;
    configuration_service(const configuration_service&) = delete;
    configuration_service& operator=(const configuration_service&) = delete;

    /// @brief Starts the I/O thread and listens on the configured address.
    void start();

    /// @brief Closes every session and stops the I/O thread.
    void stop();

    /// @brief Replaces the settings; running sessions are closed and the listener is restarted.
    void apply(const configuration_service_settings& settings);

    /// @brief The port actually bound; useful when the settings asked for port 0.
    std::uint16_t port() const;

    /// @brief Registers the handler of a set_configuration; without one every write is rejected.
    void on_set_configuration(set_handler handler);
    /// @brief Registers the handler of a get_configuration; without one an empty one is answered.
    void on_get_configuration(get_handler handler);

    void on_connected(connected_handler handler);
    void on_disconnected(disconnected_handler handler);
    void on_trace(trace_handler handler);
    void on_error(error_handler handler);
    /// @brief Registers the handler of the bound listening port; call before start().
    void on_listening(listening_handler handler);

private:
    struct impl;
    std::unique_ptr<impl> impl_;
};

} // namespace ba7lya::hermes
