// SPDX-License-Identifier: MIT
///
/// @file xml.hxx
/// @author BA7LYA (1042140025@qq.com)
/// @brief The XML wire format of The Hermes Standard.
/// @version 0.1
/// @date 2026-10-18
/// @copyright Copyright (c) 2026
///

#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <string_view>

#include "error.hxx"
#include "expected.hxx"
#include "messages.hxx"

namespace ba7lya::hermes {

///
/// @brief One message as it travels: the <Hermes> envelope and the message inside it.
///
struct envelope {
    std::optional<std::string> timestamp_; ///< The diagnostic Timestamp attribute, if the peer sent one.
    any_message message_;                  ///< The message inside the envelope.

    bool operator==(const envelope&) const = default;
};

///
/// @brief Formats @p time as the `YYYY-MM-DDThh:mm:ss.sss` the Timestamp attribute uses (3.2).
///
std::string format_timestamp(std::chrono::system_clock::time_point time);

/// @brief Formats the current time as the Timestamp attribute expects it.
std::string timestamp_now();

///
/// @brief Serializes @p message as a complete <Hermes> document without a Timestamp attribute.
///
/// A message whose SubBoards list would push the document over MAX_MESSAGE_SIZE is written
/// without the list, as the standard allows for machines with limited memory (3.6).
///
std::string to_xml(const any_message& message);

/// @brief Serializes @p message as one of its message types; see to_xml(const any_message&).
template<message M>
std::string to_xml(const M& message) {
    return to_xml(any_message { message });
}

///
/// @brief Serializes @p envelope as a complete <Hermes> document, with its Timestamp if it has one.
///
std::string to_xml(const envelope& envelope);

///
/// @brief Parses one complete <Hermes> document.
/// @return The envelope, an error_info of code error_code::MALFORMED when the bytes are not a document,
/// an error_info of code error_code::PEER when a mandatory field is missing or invalid, and an error_info of
/// code error_code::UNSUPPORTED when the document carries a message this implementation does not
/// know (such a message is to be ignored and discarded, 3.1).
///
std::expected<envelope, error_info> parse(std::string_view xml);

} // namespace ba7lya::hermes
