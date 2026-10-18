// SPDX-License-Identifier: MIT
///
/// @file settings.hxx
/// @author BA7LYA (1042140025@qq.com)
/// @brief The configuration of the interfaces the library offers.
/// @version 0.1
/// @date 2026-10-18
/// @copyright Copyright (c) 2026
///
/// The settings are plain aggregates: fill in the fields you need before start() and leave the
/// rest at their defaults.
///

#pragma once

#include <chrono>
#include <cstdint>
#include <string>

#include "enums.hxx"
#include "types.hxx"
#include "version.hxx"

namespace ba7lya::hermes {

//==============================================================================
// horizontal channel
//==============================================================================

///
/// @brief The settings of the downstream interface: the server a machine offers to its downstream neighbour.
/// @note This interface sends board_available and receives machine_ready, i.e. it belongs to the
/// machine that provides boards.
///
struct downstream_settings {
    std::string machine_id_;              ///< The id of this machine, sent in the service_description.
    std::string interface_id_;            ///< The transport interface id; empty leaves the attribute out.
    std::uint16_t lane_id_ { 1 };         ///< The lane this interface belongs to.
    supported_features features_;         ///< The optional features this machine implements.
    std::string address_ { "0.0.0.0" };   ///< The local address to listen on.
    std::uint16_t port_ { DEFAULT_PORT }; ///< The TCP port to listen on.
    std::chrono::milliseconds check_alive_interval_ { 10000 }; ///< How often to send check_alive pings.
    std::chrono::milliseconds check_alive_timeout_ { 3000 };   ///< How long to wait for a pong.
    state_check state_check_ { state_check::ALL };             ///< Whether sent documents are validated too.

    bool operator==(const downstream_settings&) const = default;
};

///
/// @brief The settings of the upstream interface: the client a machine runs towards its upstream neighbour.
/// @note This interface sends machine_ready and receives board_available, i.e. it belongs to the
/// machine that receives boards.
///
struct upstream_settings {
    std::string machine_id_;              ///< The id of this machine, sent in the service_description.
    std::string interface_id_;            ///< The transport interface id; empty leaves the attribute out.
    std::uint16_t lane_id_ { 1 };         ///< The lane this interface belongs to.
    supported_features features_;         ///< The optional features this machine implements.
    std::string address_ { "127.0.0.1" }; ///< The address of the upstream machine.
    std::uint16_t port_ { DEFAULT_PORT }; ///< The TCP port of the upstream machine.
    std::chrono::milliseconds check_alive_interval_ { 10000 }; ///< How often to send check_alive pings.
    std::chrono::milliseconds check_alive_timeout_ { 3000 };   ///< How long to wait for a pong.
    std::chrono::milliseconds reconnect_delay_ { 5000 };       ///< How long to wait between connect attempts.
    state_check state_check_ { state_check::ALL };             ///< Whether sent documents are validated too.

    bool operator==(const upstream_settings&) const = default;
};

//==============================================================================
// vertical channel
//==============================================================================

///
/// @brief The settings of the vertical_service interface: the server a machine offers to supervisory systems.
///
struct vertical_service_settings {
    std::string system_id_;             ///< The id of this machine, sent in the supervisory_service_description.
    supervisory_features features_;     ///< The supervisory features this machine implements.
    std::string address_ { "0.0.0.0" }; ///< The local address to listen on.
    std::uint16_t port_ { 0 };          ///< The TCP port to listen on; 0 makes the system choose one.
    std::chrono::milliseconds check_alive_interval_ { 10000 }; ///< How often to send check_alive pings.
    std::chrono::milliseconds check_alive_timeout_ { 3000 };   ///< How long to wait for a pong.

    bool operator==(const vertical_service_settings&) const = default;
};

///
/// @brief The settings of the vertical_client interface: the client a supervisory system runs.
///
struct vertical_client_settings {
    std::string system_id_;                                    ///< The id of this supervisory system.
    supervisory_features features_;                            ///< The supervisory features this system implements.
    std::string address_ { "127.0.0.1" };                      ///< The address of the machine.
    std::uint16_t port_ { 0 };                                 ///< The TCP port of the machine.
    std::chrono::milliseconds check_alive_interval_ { 10000 }; ///< How often to send check_alive pings.
    std::chrono::milliseconds check_alive_timeout_ { 3000 };   ///< How long to wait for a pong.
    std::chrono::milliseconds reconnect_delay_ { 5000 };       ///< How long to wait between connect attempts.

    bool operator==(const vertical_client_settings&) const = default;
};

//==============================================================================
// configuration service
//==============================================================================

///
/// @brief The settings of the configuration_service interface (2.4.1).
///
struct configuration_service_settings {
    std::string address_ { "0.0.0.0" };         ///< The local address to listen on.
    std::uint16_t port_ { CONFIGURATION_PORT }; ///< The TCP port to listen on.

    bool operator==(const configuration_service_settings&) const = default;
};

} // namespace ba7lya::hermes
