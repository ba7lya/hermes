// SPDX-License-Identifier: MIT
///
/// @file horizontal_test.cxx
/// @author BA7LYA (1042140025@qq.com)
/// @brief The integration tests of the horizontal channel over loopback TCP.
/// @version 0.1
/// @date 2026-10-18
/// @copyright Copyright (c) 2026
///

#include <chrono>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <variant>

#include "helpers.hxx"

namespace {

using namespace ba7lya::hermes;
using namespace hermes_test;

constexpr const char* BOARD_ID = "11111111-2222-3333-4444-555555555555";

///
/// @brief One downstream server and one upstream client on loopback, connected and handshaken.
///
class pair {
public:
    pair()
        : server_(make_downstream_settings()) {
        server_record_.attach(server_);
    }

    ~pair() {
        if (client_) { client_->stop(); }
        server_.stop();
    }

    pair(const pair&) = delete;
    pair& operator=(const pair&) = delete;

    bool start() {
        server_.start();
        port_ = wait_port(server_);
        if (port_ == 0) { return false; }

        client_ = std::make_unique<upstream>(make_upstream_settings("DownstreamMachine", port_));
        client_record_.attach(*client_);
        client_->start();

        return server_record_.wait_state(channel_state::NOT_AVAILABLE_NOT_READY, 8s)
            && client_record_.wait_state(channel_state::NOT_AVAILABLE_NOT_READY, 8s);
    }

    bool send_from_server(const any_message& message) {
        return server_.send(server_record_.session(), message).has_value();
    }

    bool send_from_client(const any_message& message) {
        return client_->send(client_record_.session(), message).has_value();
    }

    downstream& server() { return server_; }

    upstream& client() { return *client_; }

    recorder& server_record() { return server_record_; }

    recorder& client_record() { return client_record_; }

    std::uint16_t port() const { return port_; }

private:
    recorder server_record_;
    recorder client_record_;
    downstream server_;
    std::unique_ptr<upstream> client_;
    std::uint16_t port_ {};
};

board_available make_board() {
    board_available board;
    board.board_id_ = BOARD_ID;
    board.board_id_created_by_ = "UpstreamMachine";
    board.failed_board_ = board_quality::GOOD;
    board.flipped_board_ = flipped_board::TOP_SIDE_UP;
    board.length_ = 100.0;
    return board;
}

TEST(horizontal, exchanges_the_service_descriptions_on_connect) {
    pair peers;
    ASSERT_TRUE(peers.start());

    const auto server_side = peers.server_record().message<service_description>();
    ASSERT_TRUE(server_side.has_value());
    EXPECT_EQ(server_side->machine_id_, "DownstreamMachine");
    EXPECT_EQ(server_side->lane_id_, 1U);
    EXPECT_EQ(server_side->version_, "1.6");
    EXPECT_TRUE(server_side->supported_features_.check_alive_response_);

    const auto client_side = peers.client_record().message<service_description>();
    ASSERT_TRUE(client_side.has_value());
    EXPECT_EQ(client_side->machine_id_, "UpstreamMachine");

    EXPECT_EQ(peers.server_record().state(), channel_state::NOT_AVAILABLE_NOT_READY);
    EXPECT_EQ(peers.client_record().state(), channel_state::NOT_AVAILABLE_NOT_READY);
}

TEST(horizontal, transports_a_board) {
    pair peers;
    ASSERT_TRUE(peers.start());

    ASSERT_TRUE(peers.send_from_server(make_board()));
    ASSERT_TRUE(peers.server_record().wait_state(channel_state::BOARD_AVAILABLE));
    ASSERT_TRUE(peers.client_record().wait_state(channel_state::BOARD_AVAILABLE));
    EXPECT_TRUE(peers.client_record().wait_message<board_available>());

    machine_ready ready;
    ready.failed_board_ = board_quality::GOOD;
    ASSERT_TRUE(peers.send_from_client(ready));
    ASSERT_TRUE(peers.server_record().wait_state(channel_state::AVAILABLE_AND_READY));
    ASSERT_TRUE(peers.client_record().wait_state(channel_state::AVAILABLE_AND_READY));

    start_transport start;
    start.board_id_ = BOARD_ID;
    start.conveyor_speed_ = 250.0;
    ASSERT_TRUE(peers.send_from_client(start));
    ASSERT_TRUE(peers.server_record().wait_state(channel_state::TRANSPORTING));
    ASSERT_TRUE(peers.client_record().wait_state(channel_state::TRANSPORTING));

    transport_finished finished;
    finished.board_id_ = BOARD_ID;
    finished.transfer_state_ = transfer_state::COMPLETE;
    ASSERT_TRUE(peers.send_from_server(finished));
    ASSERT_TRUE(peers.server_record().wait_state(channel_state::TRANSPORT_FINISHED));
    ASSERT_TRUE(peers.client_record().wait_state(channel_state::TRANSPORT_FINISHED));

    stop_transport stop;
    stop.board_id_ = BOARD_ID;
    stop.transfer_state_ = transfer_state::COMPLETE;
    ASSERT_TRUE(peers.send_from_client(stop));
    ASSERT_TRUE(peers.server_record().wait_state(channel_state::NOT_AVAILABLE_NOT_READY));
    ASSERT_TRUE(peers.client_record().wait_state(channel_state::NOT_AVAILABLE_NOT_READY));

    const auto received = peers.server_record().message<start_transport>();
    ASSERT_TRUE(received.has_value());
    EXPECT_EQ(received->conveyor_speed_, 250.0);
}

TEST(horizontal, transports_a_board_in_the_other_order) {
    pair peers;
    ASSERT_TRUE(peers.start());

    ASSERT_TRUE(peers.send_from_server(make_board()));
    ASSERT_TRUE(peers.send_from_client(machine_ready {}));
    ASSERT_TRUE(peers.server_record().wait_state(channel_state::AVAILABLE_AND_READY));

    start_transport start;
    start.board_id_ = BOARD_ID;
    ASSERT_TRUE(peers.send_from_client(start));
    ASSERT_TRUE(peers.server_record().wait_state(channel_state::TRANSPORTING));

    stop_transport stop;
    stop.board_id_ = BOARD_ID;
    stop.transfer_state_ = transfer_state::INCOMPLETE;
    ASSERT_TRUE(peers.send_from_client(stop));
    ASSERT_TRUE(peers.server_record().wait_state(channel_state::TRANSPORT_STOPPED));
    ASSERT_TRUE(peers.client_record().wait_state(channel_state::TRANSPORT_STOPPED));

    transport_finished finished;
    finished.board_id_ = BOARD_ID;
    finished.transfer_state_ = transfer_state::INCOMPLETE;
    ASSERT_TRUE(peers.send_from_server(finished));
    ASSERT_TRUE(peers.server_record().wait_state(channel_state::NOT_AVAILABLE_NOT_READY));
    ASSERT_TRUE(peers.client_record().wait_state(channel_state::NOT_AVAILABLE_NOT_READY));
}

TEST(horizontal, revokes_the_offer_and_the_readiness) {
    pair peers;
    ASSERT_TRUE(peers.start());

    ASSERT_TRUE(peers.send_from_server(make_board()));
    ASSERT_TRUE(peers.client_record().wait_state(channel_state::BOARD_AVAILABLE));

    ASSERT_TRUE(peers.send_from_server(revoke_board_available {}));
    ASSERT_TRUE(peers.server_record().wait_state(channel_state::NOT_AVAILABLE_NOT_READY));
    ASSERT_TRUE(peers.client_record().wait_state(channel_state::NOT_AVAILABLE_NOT_READY));
    EXPECT_TRUE(peers.client_record().wait_message<revoke_board_available>());

    ASSERT_TRUE(peers.send_from_client(machine_ready {}));
    ASSERT_TRUE(peers.server_record().wait_state(channel_state::MACHINE_READY));

    ASSERT_TRUE(peers.send_from_client(revoke_machine_ready {}));
    ASSERT_TRUE(peers.server_record().wait_state(channel_state::NOT_AVAILABLE_NOT_READY));
    ASSERT_TRUE(peers.client_record().wait_state(channel_state::NOT_AVAILABLE_NOT_READY));
}

TEST(horizontal, answers_a_board_forecast_with_its_id) {
    pair peers;
    ASSERT_TRUE(peers.start());

    board_forecast forecast;
    forecast.forecast_id_ = "forecast-1";
    forecast.time_until_available_ = 2.5;
    forecast.failed_board_ = board_quality::ANY;
    forecast.flipped_board_ = flipped_board::UNKNOWN;
    ASSERT_TRUE(peers.send_from_server(forecast));
    EXPECT_TRUE(peers.client_record().wait_message<board_forecast>());

    machine_ready ready;
    ready.forecast_id_ = "forecast-1";
    ASSERT_TRUE(peers.send_from_client(ready));
    ASSERT_TRUE(peers.server_record().wait_state(channel_state::MACHINE_READY));

    const auto received = peers.server_record().message<machine_ready>();
    ASSERT_TRUE(received.has_value());
    EXPECT_EQ(received->forecast_id_, "forecast-1");
}

TEST(horizontal, answers_a_query_board_info) {
    pair peers;
    ASSERT_TRUE(peers.start());

    query_board_info query;
    query.top_barcode_ = "top-1";
    ASSERT_TRUE(peers.send_from_client(query));
    EXPECT_TRUE(peers.server_record().wait_message<query_board_info>());

    send_board_info answer;
    answer.board_id_ = BOARD_ID;
    answer.board_id_created_by_ = "UpstreamMachine";
    answer.failed_board_ = board_quality::GOOD;
    answer.flipped_board_ = flipped_board::TOP_SIDE_UP;
    answer.top_barcode_ = "top-1";
    ASSERT_TRUE(peers.send_from_server(answer));
    EXPECT_TRUE(peers.client_record().wait_message<send_board_info>());

    EXPECT_EQ(peers.server_record().state(), channel_state::NOT_AVAILABLE_NOT_READY);
    EXPECT_EQ(peers.client_record().state(), channel_state::NOT_AVAILABLE_NOT_READY);
}

TEST(horizontal, carries_commands_and_notifications) {
    pair peers;
    ASSERT_TRUE(peers.start());

    ASSERT_TRUE(peers.send_from_server(command { command_code::REQUEST_PAUSE }));
    ASSERT_TRUE(peers.send_from_client(command { command_code::CONFIRM_PAUSE }));
    ASSERT_TRUE(peers.send_from_server(notification { notification_code::UNSPECIFIED, severity::INFO, "hello" }));

    EXPECT_TRUE(peers.client_record().wait_message<command>());
    EXPECT_TRUE(peers.server_record().wait_message<command>());
    EXPECT_TRUE(peers.client_record().wait_message<notification>());

    // Stateless messages must not move the state chart.
    EXPECT_EQ(peers.server_record().state(), channel_state::NOT_AVAILABLE_NOT_READY);
    EXPECT_EQ(peers.client_record().state(), channel_state::NOT_AVAILABLE_NOT_READY);
}

TEST(horizontal, answers_check_alive_and_keeps_the_connection) {
    pair peers;
    ASSERT_TRUE(peers.start());

    ASSERT_TRUE(peers.client_record().wait_message<check_alive>(3s));
    ASSERT_TRUE(peers.server_record().wait_message<check_alive>(3s));
    EXPECT_TRUE(peers.server_record().wait_pong());
    EXPECT_TRUE(peers.client_record().wait_pong());

    EXPECT_FALSE(peers.client_record().wait_disconnected(400ms));
    EXPECT_FALSE(peers.server_record().wait_disconnected(400ms));
}

TEST(horizontal, reports_a_protocol_error_and_closes) {
    pair peers;
    ASSERT_TRUE(peers.start());

    // A start_transport is no valid transition from NotAvailableNotReady.
    ASSERT_TRUE(peers.client().send_raw(
        peers.client_record().session(),
        "<Hermes><StartTransport BoardId=\"11111111-2222-3333-4444-555555555555\"/></Hermes>"
    ));

    const auto note = peers.client_record().message<notification>(5s);
    ASSERT_TRUE(note.has_value());
    EXPECT_EQ(note->notification_code_, notification_code::PROTOCOL_ERROR);
    EXPECT_EQ(note->severity_, severity::FATAL);

    EXPECT_TRUE(peers.client_record().wait_disconnected(5s));
    EXPECT_TRUE(peers.server_record().wait_disconnected(5s));
    EXPECT_TRUE(peers.server_record().saw_trace(trace_type::FAILURE, "StartTransport"));
}

TEST(horizontal, rejects_an_illegal_local_send) {
    pair peers;
    ASSERT_TRUE(peers.start());

    // machine_ready belongs to the client side.
    const auto wrong_role = peers.server().send(peers.server_record().session(), machine_ready {});
    ASSERT_FALSE(wrong_role.has_value());
    EXPECT_EQ(wrong_role.error().code_, error_code::CLIENT);

    // transport_finished is the server's message, but not in this state.
    const auto wrong_state = peers.server().send(peers.server_record().session(), transport_finished {});
    ASSERT_FALSE(wrong_state.has_value());
    EXPECT_EQ(wrong_state.error().code_, error_code::CLIENT);

    EXPECT_EQ(peers.server_record().state(), channel_state::NOT_AVAILABLE_NOT_READY);
}

TEST(horizontal, checks_the_board_id_of_the_transport) {
    pair peers;
    ASSERT_TRUE(peers.start());

    ASSERT_TRUE(peers.send_from_server(make_board()));
    ASSERT_TRUE(peers.send_from_client(machine_ready {}));
    ASSERT_TRUE(peers.server_record().wait_state(channel_state::AVAILABLE_AND_READY));

    start_transport start;
    start.board_id_ = BOARD_ID;
    ASSERT_TRUE(peers.send_from_client(start));
    ASSERT_TRUE(peers.server_record().wait_state(channel_state::TRANSPORTING));

    transport_finished wrong;
    wrong.board_id_ = "99999999-2222-3333-4444-555555555555";
    const auto result = peers.server().send(peers.server_record().session(), wrong);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code_, error_code::CLIENT);
}

TEST(horizontal, reconnects_after_a_configuration_change) {
    pair peers;
    ASSERT_TRUE(peers.start());

    const auto first_session = peers.client_record().session();

    downstream_settings updated = make_downstream_settings();
    updated.port_ = peers.port();
    peers.server().apply(updated);

    ASSERT_TRUE(peers.client_record().wait_connections(2, 8s));
    ASSERT_TRUE(peers.client_record().wait_state(channel_state::NOT_AVAILABLE_NOT_READY, 8s));
    ASSERT_TRUE(peers.server_record().wait_connections(2, 8s));
    EXPECT_NE(peers.client_record().session(), first_session);
}

TEST(horizontal, stops_with_a_notification) {
    pair peers;
    ASSERT_TRUE(peers.start());

    const auto session = peers.client_record().session();
    peers.client().stop(notification { notification_code::MACHINE_SHUTDOWN, severity::INFO, "bye" });

    EXPECT_TRUE(peers.server_record().wait_disconnected(5s));
    const auto note = peers.server_record().message<notification>();
    ASSERT_TRUE(note.has_value());
    EXPECT_EQ(note->notification_code_, notification_code::MACHINE_SHUTDOWN);
    EXPECT_EQ(note->description_, "bye");
    (void)session;
}

TEST(horizontal, refuses_a_second_connection) {
    pair peers;
    ASSERT_TRUE(peers.start());

    recorder second_record;
    upstream second(make_upstream_settings("SecondMachine", peers.port()));
    second_record.attach(second);
    second.start();

    const auto note = second_record.message<notification>(5s);
    ASSERT_TRUE(note.has_value());
    EXPECT_EQ(note->notification_code_, notification_code::CONNECTION_REFUSED);
    EXPECT_TRUE(second_record.wait_disconnected(5s));

    // The established connection is retained.
    EXPECT_EQ(peers.server_record().state(), channel_state::NOT_AVAILABLE_NOT_READY);
    EXPECT_EQ(peers.client_record().state(), channel_state::NOT_AVAILABLE_NOT_READY);

    second.stop();
}

} // namespace
