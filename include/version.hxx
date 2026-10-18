// SPDX-License-Identifier: MIT
///
/// @file version.hxx
/// @author BA7LYA (1042140025@qq.com)
/// @brief The constants of The Hermes Standard this library implements.
/// @version 0.1
/// @date 2026-10-18
/// @copyright Copyright (c) 2026
///

#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace ba7lya::hermes {

/// @brief The protocol version this library implements, as it travels in the Version attribute.
inline constexpr std::string_view PROTOCOL_VERSION { "1.6" };

/// @brief The largest document one message may occupy, in bytes (The Hermes Standard 3.1).
inline constexpr std::size_t MAX_MESSAGE_SIZE { 65536U };

/// @brief The recommended base port of the horizontal channel; the lane id is added to it (2.3.1).
inline constexpr std::uint16_t DEFAULT_PORT { 50100U };

/// @brief The port the configuration service listens on (2.4.1).
inline constexpr std::uint16_t CONFIGURATION_PORT { 1248U };

///
/// @brief The values of the Route attribute the standard defines (3.6).
/// @note Values from 1 to 99 name a line specific route, values above 1000 are customer defined.
///
namespace route {
inline constexpr std::uint16_t UNDEFINED { 0U };        ///< Route undefined.
inline constexpr std::uint16_t RETURN_BOARD { 900U };   ///< Return the board.
inline constexpr std::uint16_t MANUAL_REMOVAL { 999U }; ///< Manual removal of the board.
inline constexpr std::uint16_t INDIVIDUAL { 1000U };    ///< First value reserved for individual definition.
} // namespace route

///
/// @brief The values of the Action attribute the standard defines (3.6).
/// @note Values above 1000 are customer defined.
///
namespace action {
inline constexpr std::uint16_t UNDEFINED { 0U };     ///< Action undefined.
inline constexpr std::uint16_t PROCESS { 1U };       ///< Process the board (e.g. flipping, marking).
inline constexpr std::uint16_t PASS_THROUGH { 2U };  ///< Pass the board through without processing.
inline constexpr std::uint16_t INDIVIDUAL { 1000U }; ///< First value reserved for individual definition.
} // namespace action

///
/// @brief The mask bits of the attribute support values of a send_hermes_capabilities message (3.27).
///
namespace attribute_support {
inline constexpr std::uint8_t PASS_ON { 1U }; ///< The machine passes the attribute on.
inline constexpr std::uint8_t UPDATE { 2U };  ///< The machine updates the attribute.
inline constexpr std::uint8_t REACT { 4U };   ///< The machine reacts to the attribute.
} // namespace attribute_support

} // namespace ba7lya::hermes
