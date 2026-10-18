// SPDX-License-Identifier: MIT
///
/// @file vertical_test.cxx
/// @author BA7LYA (1042140025@qq.com)
/// @brief The integration tests of the vertical channel over loopback TCP.
/// @version 0.1
/// @date 2026-10-18
/// @copyright Copyright (c) 2026
///

#include <gtest/gtest.h>
#include <memory>
#include <string>

#include "helpers.hxx"

namespace {

using namespace ba7lya::hermes;
using namespace hermes_test;

supervisory_features all_features() {
    supervisory_features features;
    features.configuration_ = true;
    features.check_alive_response_ = true;
    features.board_tracking_ = true;
    features.query_work_order_info_ = true;
    features.send_work_order_info_ = true;
    features.reply_work_order_info_ = true;
    features.query_hermes_capabilities_ = true;
    features.send_hermes_capabilities_ = true;
    return features;
}

vertical_service_settings service_settings() {
    vertical_service_settings settings;
    settings.system_id_ = "Machine-A";
    settings.address_ = "127.0.0.1";
    settings.port_ = 0;
    settings.features_ = all_features();
    settings.check_alive_interval_ = 150ms;
    settings.check_alive_timeout_ = 600ms;
    return settings;
}

vertical_client_settings client_settings(std::uint16_t port) {
    vertical_client_settings settings;
    settings.system_id_ = "Supervisor-A";
    settings.address_ = "127.0.0.1";
    settings.port_ = port;
    settings.features_ = all_features();
    settings.check_alive_interval_ = 150ms;
    settings.check_alive_timeout_ = 600ms;
    settings.reconnect_delay_ = 50ms;
    return settings;
}

class vertical_pair {
public:
    vertical_pair()
        : service_(service_settings()) {
        service_record_.attach(service_);
    }

    ~vertical_pair() {
        if (client_) { client_->stop(); }
        service_.stop();
    }

    vertical_pair(const vertical_pair&) = delete;
    vertical_pair& operator=(const vertical_pair&) = delete;

    bool start() {
        service_.start();
        port_ = wait_port(service_);
        if (port_ == 0) { return false; }

        client_ = std::make_unique<vertical_client>(client_settings(port_));
        client_record_.attach(*client_);
        client_->start();

        return service_record_.wait_vertical_state(vertical_state::CONNECTED, 8s)
            && client_record_.wait_vertical_state(vertical_state::CONNECTED, 8s);
    }

    vertical_service& service() { return service_; }

    vertical_client& client() { return *client_; }

    recorder& service_record() { return service_record_; }

    recorder& client_record() { return client_record_; }

private:
    recorder service_record_;
    recorder client_record_;
    vertical_service service_;
    std::unique_ptr<vertical_client> client_;
    std::uint16_t port_ {};
};

TEST(vertical, exchanges_the_service_descriptions) {
    vertical_pair peers;
    ASSERT_TRUE(peers.start());

    const auto supervisor = peers.service_record().message<supervisory_service_description>();
    ASSERT_TRUE(supervisor.has_value());
    EXPECT_EQ(supervisor->system_id_, "Supervisor-A");
    EXPECT_TRUE(supervisor->supported_features_.configuration_);

    const auto machine = peers.client_record().message<supervisory_service_description>();
    ASSERT_TRUE(machine.has_value());
    EXPECT_EQ(machine->system_id_, "Machine-A");
    EXPECT_TRUE(machine->supported_features_.board_tracking_);

    EXPECT_EQ(peers.service_record().current_vertical_state(), vertical_state::CONNECTED);
    EXPECT_EQ(peers.client_record().current_vertical_state(), vertical_state::CONNECTED);
}

TEST(vertical, reports_board_tracking) {
    vertical_pair peers;
    ASSERT_TRUE(peers.start());

    board_arrived arrived;
    arrived.machine_id_ = "Machine-A";
    arrived.upstream_lane_id_ = 1;
    arrived.board_transfer_ = arrival_transfer::TRANSFERRED;
    arrived.board_id_ = "11111111-2222-3333-4444-555555555555";
    arrived.board_id_created_by_ = "Machine-A";
    arrived.failed_board_ = board_quality::GOOD;
    arrived.flipped_board_ = flipped_board::TOP_SIDE_UP;

    ASSERT_TRUE(peers.service().send(peers.service_record().session(), arrived).has_value());
    EXPECT_TRUE(peers.client_record().wait_message<board_arrived>());

    board_departed departed;
    departed.machine_id_ = "Machine-A";
    departed.downstream_lane_id_ = 2;
    departed.board_transfer_ = departure_transfer::TRANSFERRED;
    departed.board_id_ = arrived.board_id_;
    departed.board_id_created_by_ = "Machine-A";
    departed.failed_board_ = board_quality::GOOD;
    departed.flipped_board_ = flipped_board::TOP_SIDE_UP;

    ASSERT_TRUE(peers.service().send(peers.service_record().session(), departed).has_value());
    EXPECT_TRUE(peers.client_record().wait_message<board_departed>());
    EXPECT_EQ(peers.client_record().current_vertical_state(), vertical_state::CONNECTED);
}

TEST(vertical, reads_the_configuration) {
    vertical_pair peers;
    ASSERT_TRUE(peers.start());

    ASSERT_TRUE(peers.client().send(peers.client_record().session(), get_configuration {}).has_value());
    EXPECT_TRUE(peers.service_record().wait_message<get_configuration>());

    current_configuration configuration;
    configuration.machine_id_ = "Machine-A";
    configuration.supervisory_system_port_ = 1250;
    ASSERT_TRUE(peers.service().send(peers.service_record().session(), configuration).has_value());
    EXPECT_TRUE(peers.client_record().wait_message<current_configuration>());
}

TEST(vertical, answers_a_work_order_query) {
    vertical_pair peers;
    ASSERT_TRUE(peers.start());

    query_work_order_info query;
    query.query_id_ = "query-1";
    query.machine_id_ = "Machine-A";
    query.barcode_ = "barcode-1";
    ASSERT_TRUE(peers.service().send(peers.service_record().session(), query).has_value());
    EXPECT_TRUE(peers.client_record().wait_message<query_work_order_info>());

    send_work_order_info answer;
    answer.query_id_ = "query-1";
    answer.work_order_id_ = "WorkOrder-1";
    answer.product_type_id_ = "Product-1";
    ASSERT_TRUE(peers.client().send(peers.client_record().session(), answer).has_value());
    EXPECT_TRUE(peers.service_record().wait_message<send_work_order_info>());

    reply_work_order_info reply;
    reply.work_order_id_ = "WorkOrder-1";
    reply.status_ = reply_work_order_status::ACCEPTED_AND_READY;
    ASSERT_TRUE(peers.service().send(peers.service_record().session(), reply).has_value());
    EXPECT_TRUE(peers.client_record().wait_message<reply_work_order_info>());
}

TEST(vertical, answers_a_query_hermes_capabilities) {
    vertical_pair peers;
    ASSERT_TRUE(peers.start());

    ASSERT_TRUE(peers.client().send(peers.client_record().session(), query_hermes_capabilities {}).has_value());
    EXPECT_TRUE(peers.service_record().wait_message<query_hermes_capabilities>());

    send_hermes_capabilities capabilities;
    capabilities.optional_messages_.command_ = true;
    capabilities.attributes_.action_ = 3;
    ASSERT_TRUE(peers.service().send(peers.service_record().session(), capabilities).has_value());
    EXPECT_TRUE(peers.client_record().wait_message<send_hermes_capabilities>());
}

TEST(vertical, answers_check_alive) {
    vertical_pair peers;
    ASSERT_TRUE(peers.start());

    ASSERT_TRUE(peers.client_record().wait_message<check_alive>(3s));
    ASSERT_TRUE(peers.service_record().wait_message<check_alive>(3s));
    EXPECT_FALSE(peers.client_record().wait_disconnected(400ms));
    EXPECT_FALSE(peers.service_record().wait_disconnected(400ms));
}

TEST(vertical, stops_with_a_notification) {
    vertical_pair peers;
    ASSERT_TRUE(peers.start());

    peers.client().stop(notification { notification_code::MACHINE_SHUTDOWN, severity::INFO, "bye" });

    EXPECT_TRUE(peers.service_record().wait_disconnected(5s));
    const auto note = peers.service_record().message<notification>();
    ASSERT_TRUE(note.has_value());
    EXPECT_EQ(note->notification_code_, notification_code::MACHINE_SHUTDOWN);
}

} // namespace
