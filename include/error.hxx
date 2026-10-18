// SPDX-License-Identifier: MIT
///
/// @file error.hxx
/// @author BA7LYA (1042140025@qq.com)
/// @brief The error_info type every fallible operation of the library reports.
/// @version 0.1
/// @date 2026-10-18
/// @copyright Copyright (c) 2026
///

#pragma once

#include <cstdint>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>

namespace ba7lya::hermes {

///
/// @brief The reason an operation failed.
/// @note The codes are not part of The Hermes Standard; the standard only defines the
/// notification codes a peer reports with a notification message.
///
enum class error_code : std::uint8_t {
    NONE,           ///< No error.
    IMPLEMENTATION, ///< A bug or resource exhaustion inside the library.
    PEER,           ///< The remote peer sent something the standard does not allow.
    CLIENT,         ///< The local application asked for something the standard does not allow.
    NETWORK,        ///< The transport failed (resolve, connect, read or write).
    TIMEOUT,        ///< An operation did not complete within the configured time.
    UNSUPPORTED,    ///< A well formed message or value this implementation does not know.
    MALFORMED       ///< The bytes are not a well formed Hermes document.
};

///
/// @brief The outcome of a fallible operation: an error_code and a human readable description.
///
struct error_info {
    error_code code_ { error_code::NONE };
    std::string text_;

    /// @brief True when the operation failed.
    explicit operator bool() const noexcept { return code_ != error_code::NONE; }

    bool operator==(const error_info&) const = default;
};

///
/// @brief Builds an error_info from its code and description.
///
inline error_info make_error(error_code code, std::string text) { return error_info { code, std::move(text) }; }

///
/// @brief The name of @p code as used in logs.
///
inline std::string_view to_string(error_code code) {
    switch (code) {
    case error_code::NONE:           return "none";
    case error_code::IMPLEMENTATION: return "implementation";
    case error_code::PEER:           return "peer";
    case error_code::CLIENT:         return "client";
    case error_code::NETWORK:        return "network";
    case error_code::TIMEOUT:        return "timeout";
    case error_code::UNSUPPORTED:    return "unsupported";
    case error_code::MALFORMED:      return "malformed";
    }
    return "unknown";
}

///
/// @brief Streams @p error_info as `code: text`.
///
inline std::ostream& operator<<(std::ostream& stream, const error_info& error) {
    stream << to_string(error.code_);
    if (!error.text_.empty()) { stream << ": " << error.text_; }
    return stream;
}

} // namespace ba7lya::hermes
