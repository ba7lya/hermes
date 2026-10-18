// SPDX-License-Identifier: MIT
///
/// @file raw_xml_test.cxx
/// @author BA7LYA (1042140025@qq.com)
/// @brief The end-to-end tests that drive the downstream interface with raw XML.
/// @version 0.1
/// @date 2026-10-18
/// @copyright Copyright (c) 2026
///

#include <array>
#include <asio.hpp>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <gtest/gtest.h>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

#include "helpers.hxx"

namespace {

using namespace ba7lya::hermes;
using namespace hermes_test;

///
/// @brief A plain TCP peer that writes and reads raw Hermes documents.
///
class raw_peer {
public:
    raw_peer()
        : socket_(context_) {}

    ~raw_peer() { close(); }

    raw_peer(const raw_peer&) = delete;
    raw_peer& operator=(const raw_peer&) = delete;

    bool connect(std::uint16_t port) {
        asio::error_code error;
        asio::ip::tcp::resolver resolver(context_);
        const auto endpoints = resolver.resolve("127.0.0.1", std::to_string(port), error);
        if (error) { return false; }

        asio::connect(socket_, endpoints, error);
        if (error) { return false; }

        // Member-function pointer, for the same Clang 17/18 std::thread reason as io_runner::start.
        reader_ = std::thread(&raw_peer::read_loop, this);
        return true;
    }

    bool send(std::string_view text) {
        asio::error_code error;
        asio::write(socket_, asio::buffer(text), error);
        return !error;
    }

    std::optional<std::string> receive(std::chrono::milliseconds timeout) {
        std::unique_lock lock(mutex_);
        cv_.wait_for(lock, timeout, [&] { return !documents_.empty() || closed_.load(); });
        if (documents_.empty()) { return std::nullopt; }

        auto document = std::move(documents_.front());
        documents_.pop_front();
        return document;
    }

    bool wait_closed(std::chrono::milliseconds timeout) {
        std::unique_lock lock(mutex_);
        cv_.wait_for(lock, timeout, [&] { return closed_.load(); });
        return closed_.load();
    }

private:
    void read_loop() {
        std::array<char, 4096> buffer {};

        while (!closed_.load()) {
            asio::error_code error;
            const auto count = socket_.read_some(asio::buffer(buffer), error);
            if (count > 0) {
                {
                    const std::lock_guard lock(mutex_);
                    buffer_.append(buffer.data(), count);
                    while (true) {
                        const auto end = buffer_.find("</Hermes>");
                        if (end == std::string::npos) { break; }
                        documents_.push_back(buffer_.substr(0, end + 9));
                        buffer_.erase(0, end + 9);
                    }
                }
                cv_.notify_all();
                continue;
            }
            if (error) {
                closed_.store(true);
                cv_.notify_all();
                return;
            }
        }
    }

    void close() {
        if (!closed_.exchange(true)) {
            // Wake the blocking read before joining it. Closing the descriptor first would free
            // the descriptor while read_loop is still using it, which TSan correctly flags.
            // Use the native shutdown so close() does not touch the Asio socket object while the
            // reader is inside read_some().
#if defined(_WIN32)
            ::shutdown(socket_.native_handle(), SD_BOTH);
#else
            ::shutdown(socket_.native_handle(), SHUT_RDWR);
#endif
            cv_.notify_all();
        }
        if (reader_.joinable()) { reader_.join(); }

        asio::error_code ignored;
        socket_.close(ignored);
    }

    asio::io_context context_;
    asio::ip::tcp::socket socket_;
    std::thread reader_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<std::string> documents_;
    std::string buffer_;
    std::atomic<bool> closed_ { false };
};

downstream_settings raw_settings() {
    downstream_settings settings = make_downstream_settings();
    settings.port_ = 0;
    settings.check_alive_interval_ = 0ms;
    return settings;
}

constexpr const char* HANDSHAKE
    = "<Hermes><ServiceDescription MachineId=\"RawPeer\" LaneId=\"3\" Version=\"1.6\">"
      "<SupportedFeatures/></ServiceDescription></Hermes>";

std::optional<envelope> parse_document(const std::optional<std::string>& document) {
    if (!document) { return std::nullopt; }
    const auto parsed = parse(*document);
    if (!parsed) { return std::nullopt; }
    return *parsed;
}

TEST(raw_xml, waits_for_the_downstream_service_description) {
    recorder record;
    downstream server(raw_settings());
    record.attach(server);
    server.start();
    const auto port = wait_port(server);
    ASSERT_NE(port, 0U);

    raw_peer peer;
    ASSERT_TRUE(peer.connect(port));

    // The server machine answers first; it must not send before the downstream machine did.
    EXPECT_FALSE(peer.receive(300ms).has_value());
    EXPECT_TRUE(peer.send(HANDSHAKE));

    const auto document = peer.receive(3s);
    ASSERT_TRUE(document.has_value());
    EXPECT_NE(document->find("MachineId=\"UpstreamMachine\""), std::string::npos);
    EXPECT_NE(document->find("LaneId=\"1\""), std::string::npos);
    EXPECT_NE(document->find("Version=\"1.6\""), std::string::npos);
    EXPECT_NE(document->find("<FeatureCheckAliveResponse/>"), std::string::npos);

    ASSERT_TRUE(record.wait_state(channel_state::NOT_AVAILABLE_NOT_READY, 3s));
    EXPECT_EQ(record.state(), channel_state::NOT_AVAILABLE_NOT_READY);
}

TEST(raw_xml, writes_the_board_available_document_of_the_standard) {
    recorder record;
    downstream server(raw_settings());
    record.attach(server);
    server.start();
    const auto port = wait_port(server);
    ASSERT_NE(port, 0U);

    raw_peer peer;
    ASSERT_TRUE(peer.connect(port));
    ASSERT_TRUE(peer.send(HANDSHAKE));
    ASSERT_TRUE(peer.receive(3s).has_value());
    ASSERT_TRUE(record.wait_state(channel_state::NOT_AVAILABLE_NOT_READY, 3s));

    board_available board;
    board.board_id_ = "11111111-2222-3333-4444-555555555555";
    board.board_id_created_by_ = "UpstreamMachine";
    board.failed_board_ = board_quality::GOOD;
    board.flipped_board_ = flipped_board::TOP_SIDE_UP;
    board.length_ = 100.0;
    sub_board sub_board;
    sub_board.pos_ = 1;
    sub_board.barcode_ = "sub";
    sub_board.state_ = sub_board_state::GOOD;
    board.sub_boards_ = { sub_board };

    ASSERT_TRUE(server.send(record.session(), board).has_value());

    const auto document = peer.receive(3s);
    ASSERT_TRUE(document.has_value());
    EXPECT_NE(document->find("<SubBoards><SB Pos=\"1\" Bc=\"sub\" St=\"1\"/></SubBoards>"), std::string::npos);
    EXPECT_NE(document->find("Timestamp="), std::string::npos);

    const auto parsed = parse_document(document);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(std::get<board_available>(parsed->message_), board);

    ASSERT_TRUE(peer.send("<Hermes><MachineReady FailedBoard=\"1\"/></Hermes>"));
    ASSERT_TRUE(record.wait_message<machine_ready>(3s));
    EXPECT_EQ(record.state(), channel_state::AVAILABLE_AND_READY);
}

TEST(raw_xml, ignores_an_unknown_message) {
    recorder record;
    downstream server(raw_settings());
    record.attach(server);
    server.start();
    const auto port = wait_port(server);
    ASSERT_NE(port, 0U);

    raw_peer peer;
    ASSERT_TRUE(peer.connect(port));
    ASSERT_TRUE(peer.send(HANDSHAKE));
    ASSERT_TRUE(peer.receive(3s).has_value());
    ASSERT_TRUE(record.wait_state(channel_state::NOT_AVAILABLE_NOT_READY, 3s));

    ASSERT_TRUE(peer.send("<Hermes><SomeFutureMessage A=\"1\"/></Hermes>"));
    EXPECT_TRUE(record.saw_trace(trace_type::WARNING, "SomeFutureMessage", 3s));

    ASSERT_TRUE(
        peer.send("<Hermes><Notification NotificationCode=\"0\" Severity=\"4\" Description=\"still here\"/></Hermes>")
    );
    EXPECT_TRUE(record.wait_message<notification>(3s));
    EXPECT_FALSE(record.wait_disconnected(200ms));
}

TEST(raw_xml, reports_a_protocol_error_and_closes) {
    recorder record;
    downstream server(raw_settings());
    record.attach(server);
    server.start();
    const auto port = wait_port(server);
    ASSERT_NE(port, 0U);

    raw_peer peer;
    ASSERT_TRUE(peer.connect(port));
    ASSERT_TRUE(peer.send(HANDSHAKE));
    ASSERT_TRUE(peer.receive(3s).has_value());
    ASSERT_TRUE(record.wait_state(channel_state::NOT_AVAILABLE_NOT_READY, 3s));

    ASSERT_TRUE(peer.send("<Hermes><StartTransport BoardId=\"11111111-2222-3333-4444-555555555555\"/></Hermes>"));

    const auto reply = peer.receive(3s);
    ASSERT_TRUE(reply.has_value());
    const auto parsed = parse_document(reply);
    ASSERT_TRUE(parsed.has_value());
    const auto* note = std::get_if<notification>(&parsed->message_);
    ASSERT_NE(note, nullptr);
    EXPECT_EQ(note->notification_code_, notification_code::PROTOCOL_ERROR);
    EXPECT_EQ(note->severity_, severity::FATAL);

    EXPECT_TRUE(record.wait_disconnected(3s));
    EXPECT_TRUE(peer.wait_closed(3s));
}

TEST(raw_xml, closes_the_session_when_the_check_alive_response_is_missing) {
    downstream_settings settings = raw_settings();
    settings.check_alive_interval_ = 100ms;
    settings.check_alive_timeout_ = 300ms;

    recorder record;
    downstream server(settings);
    record.attach(server);
    server.start();
    const auto port = wait_port(server);
    ASSERT_NE(port, 0U);

    raw_peer peer;
    ASSERT_TRUE(peer.connect(port));
    ASSERT_TRUE(peer.send(
        "<Hermes><ServiceDescription MachineId=\"RawPeer\" LaneId=\"1\" Version=\"1.6\">"
        "<SupportedFeatures><FeatureCheckAliveResponse/></SupportedFeatures></ServiceDescription></Hermes>"
    ));
    ASSERT_TRUE(peer.receive(3s).has_value());

    EXPECT_TRUE(record.wait_disconnected(5s));
    EXPECT_EQ(record.disconnect_error().code_, error_code::TIMEOUT);
    EXPECT_TRUE(peer.wait_closed(5s));
}

TEST(raw_xml, closes_the_session_on_malformed_bytes) {
    recorder record;
    downstream server(raw_settings());
    record.attach(server);
    server.start();
    const auto port = wait_port(server);
    ASSERT_NE(port, 0U);

    raw_peer peer;
    ASSERT_TRUE(peer.connect(port));
    ASSERT_TRUE(peer.send("this is not an XML document"));

    EXPECT_TRUE(record.wait_disconnected(3s));
    EXPECT_EQ(record.disconnect_error().code_, error_code::MALFORMED);
    EXPECT_TRUE(peer.wait_closed(3s));
}

} // namespace
