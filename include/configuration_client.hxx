// SPDX-License-Identifier: MIT
///
/// @file configuration_client.hxx
/// @author BA7LYA (1042140025@qq.com)
/// @brief The client side of the configuration service.
/// @version 0.1
/// @date 2026-10-18
/// @copyright Copyright (c) 2026
///
/// An engineering station opens one short lived connection per operation and closes it again, as
/// The Hermes Standard 2.4.1 asks for.
///

#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "error.hxx"
#include "expected.hxx"
#include "messages.hxx"
#include "version.hxx"

namespace ba7lya::hermes {

///
/// @brief What the configuration service answered.
///
struct configuration_reply {
    std::optional<current_configuration> configuration_; ///< The configuration, if the service sent one.
    std::vector<notification> notifications_;            ///< The notifications the service sent.
};

///
/// @brief Reads the configuration of the machine at @p address.
///
/// @param address The IP address or host name of the machine.
/// @param port The port its configuration service listens on.
/// @param timeout How long to wait for the answer.
/// @return The reply, or an error_info when the connection or the exchange failed or a notification
/// reported a configuration error.
///
std::expected<configuration_reply, error_info> read_configuration(
    std::string_view address,
    std::uint16_t port = CONFIGURATION_PORT,
    std::chrono::milliseconds timeout = std::chrono::seconds(5)
);

///
/// @brief Writes @p configuration to the machine at @p address.
///
/// @param address The IP address or host name of the machine.
/// @param configuration The complete configuration to write.
/// @param port The port its configuration service listens on.
/// @param timeout How long to wait for the answer.
/// @return The reply, or an error_info when the connection or the exchange failed or a notification
/// reported a configuration error.
///
std::expected<configuration_reply, error_info> write_configuration(
    std::string_view address,
    const set_configuration& configuration,
    std::uint16_t port = CONFIGURATION_PORT,
    std::chrono::milliseconds timeout = std::chrono::seconds(5)
);

} // namespace ba7lya::hermes
