// SPDX-License-Identifier: MIT
///
/// @file framing.hxx
/// @author BA7LYA (1042140025@qq.com)
/// @brief The document framing of the Hermes TCP streams.
/// @version 0.1
/// @date 2026-10-18
/// @copyright Copyright (c) 2026
///
/// A Hermes stream carries one complete XML document per message; there is no length prefix.
/// The extractor scans the bytes for the end of the <Hermes> root element and keeps whatever
/// follows for the next call, so a document may be split over any number of reads.
///

#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "error.hxx"

namespace ba7lya::hermes::detail {

///
/// @brief Extracts complete <Hermes> documents from one byte stream.
///
class frame_extractor {
public:
    ///
    /// @brief Appends @p bytes to the stream and moves every document that is now complete into
    /// @p documents.
    /// @return false when the stream cannot be framed: the bytes are not a Hermes document or a
    /// document exceeds MAX_MESSAGE_SIZE; @p error_info then carries the reason.
    ///
    bool feed(std::string_view bytes, std::vector<std::string>& documents, error_info& error);

    /// @brief The number of bytes of an incomplete document kept for the next call.
    std::size_t buffered() const noexcept { return buffer_.size(); }

    /// @brief Drops the buffered bytes.
    void clear() noexcept { buffer_.clear(); }

private:
    std::string buffer_;
};

} // namespace ba7lya::hermes::detail
