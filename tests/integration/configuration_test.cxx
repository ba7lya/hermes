// SPDX-License-Identifier: MIT
///
/// @file configuration_test.cxx
/// @author BA7LYA (1042140025@qq.com)
/// @brief The integration tests of the configuration service and its client.
/// @version 0.1
/// @date 2026-10-18
/// @copyright Copyright (c) 2026
///

#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <string>
#include <utility>

#include "helpers.hxx"

namespace {

using namespace ba7lya::hermes;
using namespace hermes_test;

configuration_service_settings configuration_settings() {
    configuration_service_settings settings;
    settings.address_ = "127.0.0.1";
    settings.port_ = 0;
    return settings;
}

current_configuration make_configuration(std::string machine_id) {
    current_configuration configuration;
    configuration.machine_id_ = std::move(machine_id);
    configuration.supervisory_system_port_ = 1250;

    upstream_configuration upstream;
    upstream.upstream_lane_id_ = 1;
    upstream.host_address_ = "192.168.0.10";
    upstream.port_ = 50100;
    configuration.upstream_configurations_ = { upstream };

    downstream_configuration downstream;
    downstream.downstream_lane_id_ = 2;
    downstream.port_ = 50101;
    configuration.downstream_configurations_ = { downstream };

    return configuration;
}

TEST(configuration, reads_and_writes_the_configuration) {
    configuration_service service(configuration_settings());
    current_configuration stored = make_configuration("Machine-A");
    std::atomic<int> writes { 0 };

    service.on_get_configuration([&](session_id, const connection_info&) { return stored; });
    service.on_set_configuration([&](session_id, const connection_info&, const set_configuration& value) {
        stored.machine_id_ = value.machine_id_;
        stored.supervisory_system_port_ = value.supervisory_system_port_;
        stored.upstream_configurations_ = value.upstream_configurations_;
        stored.downstream_configurations_ = value.downstream_configurations_;
        ++writes;
        return error_info {};
    });

    service.start();
    const auto port = wait_port(service);
    ASSERT_NE(port, 0U);

    const auto read = read_configuration("127.0.0.1", port, 3s);
    ASSERT_TRUE(read.has_value()) << read.error();
    ASSERT_TRUE(read->configuration_.has_value());
    EXPECT_EQ(read->configuration_->machine_id_, "Machine-A");
    ASSERT_EQ(read->configuration_->upstream_configurations_.size(), 1U);
    EXPECT_EQ(read->configuration_->upstream_configurations_.front().host_address_, "192.168.0.10");

    set_configuration write;
    write.machine_id_ = "Machine-B";
    write.supervisory_system_port_ = 1300;
    write.upstream_configurations_ = make_configuration("ignored").upstream_configurations_;
    write.downstream_configurations_ = make_configuration("ignored").downstream_configurations_;

    const auto written = write_configuration("127.0.0.1", write, port, 400ms);
    ASSERT_TRUE(written.has_value()) << written.error();
    EXPECT_EQ(writes.load(), 1);

    const auto again = read_configuration("127.0.0.1", port, 3s);
    ASSERT_TRUE(again.has_value()) << again.error();
    ASSERT_TRUE(again->configuration_.has_value());
    EXPECT_EQ(again->configuration_->machine_id_, "Machine-B");
    EXPECT_EQ(again->configuration_->supervisory_system_port_, 1300U);

    service.stop();
}

TEST(configuration, reports_a_rejected_configuration) {
    configuration_service service(configuration_settings());
    service.on_get_configuration([](session_id, const connection_info&) { return current_configuration {}; });
    service.on_set_configuration([](session_id, const connection_info&, const set_configuration&) {
        return error_info { error_code::PEER, "the port is already in use" };
    });

    service.start();
    const auto port = wait_port(service);
    ASSERT_NE(port, 0U);

    set_configuration write;
    write.machine_id_ = "Machine-B";
    const auto result = write_configuration("127.0.0.1", write, port, 3s);

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code_, error_code::PEER);
    EXPECT_NE(result.error().text_.find("already in use"), std::string::npos);

    service.stop();
}

TEST(configuration, reports_an_unreachable_service) {
    const auto result = read_configuration("127.0.0.1", 1, 300ms);
    ASSERT_FALSE(result.has_value());
    EXPECT_TRUE(result.error().code_ == error_code::NETWORK || result.error().code_ == error_code::TIMEOUT);
}

} // namespace
