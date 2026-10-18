// SPDX-License-Identifier: MIT
///
/// @file framing_test.cxx
/// @author BA7LYA (1042140025@qq.com)
/// @brief The unit tests of the TCP document framing.
/// @version 0.1
/// @date 2026-10-18
/// @copyright Copyright (c) 2026
///

#include <gtest/gtest.h>
#include <string>
#include <string_view>
#include <vector>

#include "framing.hxx"
#include "version.hxx"

namespace {

using namespace ba7lya::hermes;
using ba7lya::hermes::detail::frame_extractor;

std::string_view view(const std::string& text) { return { text.data(), text.size() }; }

TEST(framing, reassembles_a_document_split_into_bytes) {
    const std::string document = "<Hermes><CheckAlive Type=\"1\" Id=\"abc\"/></Hermes>";

    frame_extractor extractor;
    std::vector<std::string> documents;
    error_info error;

    for (const char character : document) {
        ASSERT_TRUE(extractor.feed(std::string_view { &character, 1 }, documents, error)) << error;
    }

    ASSERT_EQ(documents.size(), 1U);
    EXPECT_EQ(documents.front(), document);
    EXPECT_EQ(extractor.buffered(), 0U);
}

TEST(framing, extracts_two_documents_of_one_read) {
    const std::string first = "<Hermes><GetConfiguration/></Hermes>";
    const std::string second = "<Hermes><RevokeMachineReady/></Hermes>";

    frame_extractor extractor;
    std::vector<std::string> documents;
    error_info error;

    ASSERT_TRUE(extractor.feed(first + second, documents, error)) << error;
    ASSERT_EQ(documents.size(), 2U);
    EXPECT_EQ(documents[0], first);
    EXPECT_EQ(documents[1], second);
}

TEST(framing, skips_the_prologue_and_the_whitespace_between_documents) {
    const std::string document
        = "\xEF\xBB\xBF\r\n<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<!-- a comment -->\n"
          "<Hermes><CheckAlive/></Hermes>\n\n";

    frame_extractor extractor;
    std::vector<std::string> documents;
    error_info error;

    ASSERT_TRUE(extractor.feed(document, documents, error)) << error;
    ASSERT_EQ(documents.size(), 1U);
    EXPECT_EQ(documents.front(), "<Hermes><CheckAlive/></Hermes>");
}

TEST(framing, keeps_an_incomplete_document_for_the_next_call) {
    frame_extractor extractor;
    std::vector<std::string> documents;
    error_info error;

    ASSERT_TRUE(extractor.feed("<Hermes><BoardAvailable BoardId=\"x\"", documents, error)) << error;
    EXPECT_TRUE(documents.empty());
    EXPECT_GT(extractor.buffered(), 0U);

    ASSERT_TRUE(extractor.feed(" BoardIdCreatedBy=\"y\"/></Hermes>", documents, error)) << error;
    ASSERT_EQ(documents.size(), 1U);
}

TEST(framing, accepts_comments_and_cdata_inside_the_document) {
    const std::string document = "<Hermes><!-- </Hermes> --><CheckAlive/><![CDATA[ </Hermes> ]]></Hermes>";

    frame_extractor extractor;
    std::vector<std::string> documents;
    error_info error;

    ASSERT_TRUE(extractor.feed(document, documents, error)) << error;
    ASSERT_EQ(documents.size(), 1U);
    EXPECT_EQ(documents.front(), document);
}

TEST(framing, accepts_a_self_closing_root) {
    frame_extractor extractor;
    std::vector<std::string> documents;
    error_info error;

    ASSERT_TRUE(extractor.feed("<Hermes/>", documents, error)) << error;
    ASSERT_EQ(documents.size(), 1U);
    EXPECT_EQ(documents.front(), "<Hermes/>");
}

TEST(framing, rejects_garbage) {
    frame_extractor extractor;
    std::vector<std::string> documents;
    error_info error;

    EXPECT_FALSE(extractor.feed("this is not a document", documents, error));
    EXPECT_EQ(error.code_, error_code::MALFORMED);
}

TEST(framing, rejects_a_foreign_root_element) {
    frame_extractor extractor;
    std::vector<std::string> documents;
    error_info error;

    EXPECT_FALSE(extractor.feed("<Other><CheckAlive/></Other>", documents, error));
    EXPECT_EQ(error.code_, error_code::MALFORMED);
}

TEST(framing, rejects_an_oversized_document) {
    std::string document = "<Hermes>";
    document.append(MAX_MESSAGE_SIZE, 'a');
    document.append("</Hermes>");

    frame_extractor extractor;
    std::vector<std::string> documents;
    error_info error;

    EXPECT_FALSE(extractor.feed(document, documents, error));
    EXPECT_EQ(error.code_, error_code::MALFORMED);
    EXPECT_NE(error.text_.find("maximum message size"), std::string::npos);
}

} // namespace
