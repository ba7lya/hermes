// SPDX-License-Identifier: MIT
///
/// @file types.hxx
/// @author BA7LYA (1042140025@qq.com)
/// @brief The value types The Hermes Standard puts inside its messages.
/// @version 0.1
/// @date 2026-10-18
/// @copyright Copyright (c) 2026
///

#pragma once

#include <cstdint>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

#include "enums.hxx"

namespace ba7lya::hermes {

/// @brief The identifier one connection is known by inside a facade.
using session_id = std::uint16_t;

///
/// @brief The address a session is connected to or from.
///
struct connection_info {
    std::string address_;   ///< The IP address or host name.
    std::uint16_t port_ {}; ///< The TCP port.

    bool operator==(const connection_info&) const = default;
};

/// @brief Streams @p value as `address:port`.
inline std::ostream& operator<<(std::ostream& stream, const connection_info& value) {
    return stream << value.address_ << ':' << value.port_;
}

///
/// @brief One subboard of the SubBoards list (3.6).
///
struct sub_board {
    std::uint16_t pos_ {};                               ///< Position number according to IPC-2591 CFX unit numbering.
    std::optional<std::string> barcode_;                 ///< The barcode of the subboard, if it has one.
    sub_board_state state_ { sub_board_state::UNKNOWN }; ///< The state of the subboard.

    bool operator==(const sub_board&) const = default;
};

/// @brief Streams @p value for debugging.
inline std::ostream& operator<<(std::ostream& stream, const sub_board& value) {
    stream << "{pos=" << value.pos_;
    if (value.barcode_) { stream << ", barcode=" << *value.barcode_; }
    return stream << ", state=" << value.state_ << '}';
}

///
/// @brief The optional features of the horizontal channel a service_description announces (3.4).
/// @note Every feature of protocol version 1.0 is mandatory and never listed.
///
struct supported_features {
    bool check_alive_response_ {}; ///< The machine answers check_alive pings with pongs.
    bool board_forecast_ {};       ///< The machine sends or handles board_forecast messages.
    bool query_board_info_ {};     ///< The machine sends query_board_info messages.
    bool send_board_info_ {};      ///< The machine answers query_board_info messages.
    bool command_ {};              ///< The machine handles command messages.

    bool operator==(const supported_features&) const = default;
};

///
/// @brief The optional features of the vertical channel a supervisory_service_description announces (3.19).
///
struct supervisory_features {
    bool configuration_ {};             ///< set_configuration and get_configuration are implemented.
    bool check_alive_response_ {};      ///< The partner answers check_alive pings with pongs.
    bool board_tracking_ {};            ///< board_arrived and board_departed are implemented.
    bool query_work_order_info_ {};     ///< query_work_order_info is implemented.
    bool send_work_order_info_ {};      ///< send_work_order_info is implemented.
    bool reply_work_order_info_ {};     ///< reply_work_order_info is implemented.
    bool query_hermes_capabilities_ {}; ///< query_hermes_capabilities is implemented.
    bool send_hermes_capabilities_ {};  ///< send_hermes_capabilities is implemented.

    bool operator==(const supervisory_features&) const = default;
};

///
/// @brief The optional messages a send_hermes_capabilities message announces (3.27).
///
struct optional_messages {
    bool check_alive_response_ {};
    bool board_forecast_ {};
    bool query_board_info_ {};
    bool send_board_info_ {};
    bool board_arrived_ {};
    bool board_departed_ {};
    bool query_work_order_info_ {};
    bool reply_work_order_info_ {};
    bool command_ {};

    bool operator==(const optional_messages&) const = default;
};

///
/// @brief The bit-wise attribute support values of a send_hermes_capabilities message (3.27).
/// @note Each value is a combination of attribute_support::PASS_ON, attribute_support::UPDATE and
/// attribute_support::REACT; 0 means the machine does not support the attribute at all.
///
struct attribute_capabilities {
    std::uint8_t product_type_id_ {};
    std::uint8_t top_barcode_ {};
    std::uint8_t bottom_barcode_ {};
    std::uint8_t length_ {};
    std::uint8_t width_ {};
    std::uint8_t thickness_ {};
    std::uint8_t conveyor_speed_ {};
    std::uint8_t top_clearance_height_ {};
    std::uint8_t bottom_clearance_height_ {};
    std::uint8_t weight_ {};
    std::uint8_t work_order_id_ {};
    std::uint8_t batch_id_ {};
    std::uint8_t route_ {};
    std::uint8_t action_ {};
    std::uint8_t sub_boards_ {};

    bool operator==(const attribute_capabilities&) const = default;
};

///
/// @brief The configuration of one upstream connection inside set_configuration (3.13).
///
struct upstream_configuration {
    std::uint16_t upstream_lane_id_ {};                ///< The lane on the upstream side.
    std::optional<std::string> upstream_interface_id_; ///< The transport interface on the upstream side.
    std::string host_address_;                         ///< IP address or host name of the upstream machine.
    std::uint16_t port_ {};                            ///< The port to connect to.

    bool operator==(const upstream_configuration&) const = default;
};

/// @brief Streams @p value for debugging.
inline std::ostream& operator<<(std::ostream& stream, const upstream_configuration& value) {
    stream << "{lane=" << value.upstream_lane_id_;
    if (value.upstream_interface_id_) { stream << ", interface=" << *value.upstream_interface_id_; }
    return stream << ", host=" << value.host_address_ << ", port=" << value.port_ << '}';
}

///
/// @brief The configuration of one downstream connection inside set_configuration (3.13).
///
struct downstream_configuration {
    std::uint16_t downstream_lane_id_ {};                ///< The lane on the downstream side.
    std::optional<std::string> downstream_interface_id_; ///< The transport interface on the downstream side.
    std::optional<std::string> client_address_;          ///< The accepted client address; unset accepts any.
    std::uint16_t port_ {};                              ///< The port to listen on.

    bool operator==(const downstream_configuration&) const = default;
};

/// @brief Streams @p value for debugging.
inline std::ostream& operator<<(std::ostream& stream, const downstream_configuration& value) {
    stream << "{lane=" << value.downstream_lane_id_;
    if (value.downstream_interface_id_) { stream << ", interface=" << *value.downstream_interface_id_; }
    if (value.client_address_) { stream << ", client=" << *value.client_address_; }
    return stream << ", port=" << value.port_ << '}';
}

/// @brief The list of upstream connections of a configuration message.
using upstream_configurations = std::vector<upstream_configuration>;

/// @brief The list of downstream connections of a configuration message.
using downstream_configurations = std::vector<downstream_configuration>;

} // namespace ba7lya::hermes
