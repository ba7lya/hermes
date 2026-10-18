// SPDX-License-Identifier: MIT
///
/// @file framing.cxx
/// @author BA7LYA (1042140025@qq.com)
/// @brief The scan that turns a Hermes byte stream back into documents.
/// @version 0.1
/// @date 2026-10-18
/// @copyright Copyright (c) 2026
///

#include "framing.hxx"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "version.hxx"

namespace ba7lya::hermes::detail {

namespace {

/// @brief One tag found in the stream.
struct tag_info {
    std::string_view name_; ///< The element name, without the '<', '/' and '>'.
    bool closing_ {};       ///< Whether this is an end tag.
    bool self_closing_ {};  ///< Whether the tag ends with "/>".
    std::size_t end_ {};    ///< The offset just past the '>'.
};

bool starts_with(std::string_view text, std::size_t offset, std::string_view prefix) {
    return offset + prefix.size() <= text.size() && text.compare(offset, prefix.size(), prefix) == 0;
}

bool is_space(char value) { return value == ' ' || value == '\t' || value == '\r' || value == '\n'; }

bool is_name_char(char value) {
    if (value == '>' || value == '/' || value == '<' || value == '=') { return false; }
    return !is_space(value);
}

///
/// @brief Parses the tag that starts at @p offset, which has to point at a '<'.
/// @return The tag, or nothing when the tag is not complete yet.
///
std::optional<tag_info> parse_tag(std::string_view text, std::size_t offset) {
    std::size_t cursor = offset + 1;
    if (cursor >= text.size()) { return std::nullopt; }

    tag_info tag;
    if (text[cursor] == '/') {
        tag.closing_ = true;
        ++cursor;
    }

    const auto name_start = cursor;
    while (cursor < text.size() && is_name_char(text[cursor])) { ++cursor; }
    if (cursor >= text.size()) { return std::nullopt; }
    if (cursor == name_start) { return {}; } // "<>" or "</" are not tags
    tag.name_ = text.substr(name_start, cursor - name_start);

    // Walk to the '>' that ends the tag, ignoring any '>' inside a quoted attribute value.
    char quote = '\0';
    for (; cursor < text.size(); ++cursor) {
        const char value = text[cursor];
        if (quote != '\0') {
            if (value == quote) { quote = '\0'; }
            continue;
        }
        if (value == '"' || value == '\'') {
            quote = value;
            continue;
        }
        if (value == '>') {
            tag.self_closing_ = !tag.closing_ && cursor > offset + 1 && text[cursor - 1] == '/';
            tag.end_ = cursor + 1;
            return tag;
        }
    }
    return std::nullopt;
}

///
/// @brief Skips the bytes in front of the root element (whitespace, BOM, declarations, comments).
/// @return The offset of the root's '<', or std::string_view::npos when more bytes are needed.
///
std::size_t skip_prologue(std::string_view text, bool& bad) {
    std::size_t cursor = 0;
    bad = false;

    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF && static_cast<unsigned char>(text[1]) == 0xBB
        && static_cast<unsigned char>(text[2]) == 0xBF) {
        cursor = 3;
    }
    else if (text.size() < 3) {
        // The BOM may be split over reads; do not mistake its first bytes for garbage.
        static constexpr unsigned char bom[] = { 0xEF, 0xBB, 0xBF };
        bool maybe_bom = true;
        for (std::size_t index = 0; index < text.size(); ++index) {
            if (static_cast<unsigned char>(text[index]) != bom[index]) {
                maybe_bom = false;
                break;
            }
        }
        if (maybe_bom) { return std::string_view::npos; }
    }

    while (true) {
        while (cursor < text.size() && is_space(text[cursor])) { ++cursor; }
        if (cursor >= text.size()) { return std::string_view::npos; }
        if (text[cursor] != '<') {
            bad = true;
            return std::string_view::npos;
        }
        if (starts_with(text, cursor, "<?")) {
            const auto end = text.find("?>", cursor + 2);
            if (end == std::string_view::npos) { return std::string_view::npos; }
            cursor = end + 2;
            continue;
        }
        if (starts_with(text, cursor, "<!--")) {
            const auto end = text.find("-->", cursor + 4);
            if (end == std::string_view::npos) { return std::string_view::npos; }
            cursor = end + 3;
            continue;
        }
        if (starts_with(text, cursor, "<!")) {
            const auto end = text.find('>', cursor + 2);
            if (end == std::string_view::npos) { return std::string_view::npos; }
            cursor = end + 1;
            continue;
        }
        return cursor;
    }
}

///
/// @brief Finds the terminator of a comment, CDATA section or processing instruction.
/// @return The offset just past the terminator, or npos when it is not in the buffer yet.
///
std::size_t skip_special(std::string_view text, std::size_t offset) {
    if (starts_with(text, offset, "<!--")) {
        const auto end = text.find("-->", offset + 4);
        return end == std::string_view::npos ? std::string_view::npos : end + 3;
    }
    if (starts_with(text, offset, "<![CDATA[")) {
        const auto end = text.find("]]>", offset + 9);
        return end == std::string_view::npos ? std::string_view::npos : end + 3;
    }
    if (starts_with(text, offset, "<?")) {
        const auto end = text.find("?>", offset + 2);
        return end == std::string_view::npos ? std::string_view::npos : end + 2;
    }
    if (starts_with(text, offset, "<!")) {
        const auto end = text.find('>', offset + 2);
        return end == std::string_view::npos ? std::string_view::npos : end + 1;
    }
    return offset;
}

} // namespace

bool frame_extractor::feed(std::string_view bytes, std::vector<std::string>& documents, error_info& error) {
    buffer_.append(bytes);

    while (true) {
        bool bad = false;
        const auto root_offset = skip_prologue(buffer_, bad);
        if (bad) {
            error = error_info { error_code::MALFORMED, "the stream does not start with an XML element" };
            return false;
        }
        if (root_offset == std::string_view::npos) {
            if (buffer_.size() > 4096U) {
                error = error_info { error_code::MALFORMED, "no Hermes element found in the first 4096 bytes" };
                return false;
            }
            return true;
        }

        const auto root = parse_tag(buffer_, root_offset);
        if (!root) {
            if (buffer_.size() - root_offset > MAX_MESSAGE_SIZE) {
                error = error_info { error_code::MALFORMED, "unterminated root tag" };
                return false;
            }
            buffer_.erase(0, root_offset);
            return true;
        }
        if (root->closing_ || root->name_ != "Hermes") {
            error = error_info { error_code::MALFORMED, "the root element is not Hermes" };
            return false;
        }

        if (root->self_closing_) {
            documents.emplace_back(buffer_.substr(root_offset, root->end_ - root_offset));
            buffer_.erase(0, root->end_);
            continue;
        }

        auto cursor = root->end_;
        int depth = 1;
        bool complete = false;
        std::size_t frame_end = 0;

        while (cursor < buffer_.size()) {
            const auto offset = buffer_.find('<', cursor);
            if (offset == std::string::npos) { break; }

            const auto special = skip_special(buffer_, offset);
            if (special != offset) {
                if (special == std::string_view::npos) { break; }
                cursor = special;
                continue;
            }

            const auto tag = parse_tag(buffer_, offset);
            if (!tag) { break; }

            if (tag->closing_) {
                --depth;
                if (depth == 0) {
                    complete = true;
                    frame_end = tag->end_;
                    break;
                }
                if (depth < 0) {
                    error = error_info { error_code::MALFORMED, "unbalanced end tag" };
                    return false;
                }
            }
            else if (!tag->self_closing_) { ++depth; }

            if (buffer_.size() - root_offset > MAX_MESSAGE_SIZE) {
                error = error_info { error_code::MALFORMED,
                                     "the document exceeds the maximum message size of "
                                         + std::to_string(MAX_MESSAGE_SIZE) + " bytes" };
                return false;
            }
            cursor = tag->end_;
        }

        if (!complete) {
            if (buffer_.size() - root_offset > MAX_MESSAGE_SIZE) {
                error = error_info { error_code::MALFORMED,
                                     "the document exceeds the maximum message size of "
                                         + std::to_string(MAX_MESSAGE_SIZE) + " bytes" };
                return false;
            }
            buffer_.erase(0, root_offset);
            return true;
        }

        if (frame_end - root_offset > MAX_MESSAGE_SIZE) {
            error = error_info { error_code::MALFORMED,
                                 "the document exceeds the maximum message size of " + std::to_string(MAX_MESSAGE_SIZE)
                                     + " bytes" };
            return false;
        }

        documents.emplace_back(buffer_.substr(root_offset, frame_end - root_offset));
        buffer_.erase(0, frame_end);
    }
}

} // namespace ba7lya::hermes::detail
