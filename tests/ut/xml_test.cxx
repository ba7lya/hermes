// SPDX-License-Identifier: MIT
///
/// @file xml_test.cxx
/// @author BA7LYA (1042140025@qq.com)
/// @brief The unit tests of the XML codec.
/// @version 0.1
/// @date 2026-10-18
/// @copyright Copyright (c) 2026
///

#include <gtest/gtest.h>
#include <string>
#include <variant>
#include <vector>

#include "hermes.hxx"

namespace {

using namespace ba7lya::hermes;
using namespace std::string_literals;

/// @brief A message with every attribute and list of the standard set.
std::vector<any_message> all_messages() {
    std::vector<any_message> messages;

    check_alive check_alive;
    check_alive.type_ = check_alive_type::PING;
    check_alive.id_ = "ping-1";
    messages.emplace_back(check_alive);

    service_description description;
    description.machine_id_ = "Machine-1";
    description.lane_id_ = 2;
    description.interface_id_ = "Interface-1";
    description.version_ = "1.6";
    description.supported_features_ = supported_features { true, true, true, true, true };
    messages.emplace_back(description);

    messages.emplace_back(notification { notification_code::PROTOCOL_ERROR, severity::FATAL, "a description" });
    messages.emplace_back(revoke_board_available {});
    messages.emplace_back(revoke_machine_ready {});
    messages.emplace_back(get_configuration {});
    messages.emplace_back(query_hermes_capabilities {});

    sub_board first;
    first.pos_ = 1;
    first.barcode_ = "sub-1";
    first.state_ = sub_board_state::GOOD;
    sub_board second;
    second.pos_ = 2;
    second.state_ = sub_board_state::MISSING;

    board_available available;
    available.board_id_ = "11111111-2222-3333-4444-555555555555";
    available.board_id_created_by_ = "Machine-1";
    available.failed_board_ = board_quality::GOOD;
    available.product_type_id_ = "Product-1";
    available.flipped_board_ = flipped_board::TOP_SIDE_UP;
    available.top_barcode_ = "top";
    available.bottom_barcode_ = "bottom";
    available.length_ = 100.5;
    available.width_ = 50.25;
    available.thickness_ = 1.6;
    available.conveyor_speed_ = 250.0;
    available.top_clearance_height_ = 30.0;
    available.bottom_clearance_height_ = 12.5;
    available.weight_ = 42.0;
    available.work_order_id_ = "WorkOrder-1";
    available.batch_id_ = "Batch-1";
    available.route_ = 7;
    available.action_ = 1;
    available.sub_boards_ = { first, second };
    messages.emplace_back(available);

    machine_ready ready;
    ready.failed_board_ = board_quality::FAILED;
    ready.forecast_id_ = "forecast-1";
    ready.board_id_ = "11111111-2222-3333-4444-555555555555";
    ready.product_type_id_ = "Product-1";
    ready.flipped_board_ = flipped_board::BOTTOM_SIDE_UP;
    ready.length_ = 100.0;
    ready.width_ = 50.0;
    ready.thickness_ = 1.0;
    ready.conveyor_speed_ = 200.0;
    ready.top_clearance_height_ = 20.0;
    ready.bottom_clearance_height_ = 10.0;
    ready.weight_ = 40.0;
    ready.work_order_id_ = "WorkOrder-1";
    ready.batch_id_ = "Batch-1";
    messages.emplace_back(ready);

    start_transport start;
    start.board_id_ = available.board_id_;
    start.conveyor_speed_ = 250.0;
    messages.emplace_back(start);

    messages.emplace_back(stop_transport { transfer_state::INCOMPLETE, available.board_id_ });
    messages.emplace_back(transport_finished { transfer_state::COMPLETE, available.board_id_ });

    upstream_configuration upstream;
    upstream.upstream_lane_id_ = 1;
    upstream.upstream_interface_id_ = "Interface-1";
    upstream.host_address_ = "192.168.0.10";
    upstream.port_ = 50100;

    downstream_configuration downstream;
    downstream.downstream_lane_id_ = 2;
    downstream.downstream_interface_id_ = "Interface-2";
    downstream.client_address_ = "192.168.0.11";
    downstream.port_ = 50101;

    set_configuration set;
    set.machine_id_ = "Machine-1";
    set.supervisory_system_port_ = 1248;
    set.upstream_configurations_ = { upstream };
    set.downstream_configurations_ = { downstream };
    messages.emplace_back(set);

    current_configuration current;
    current.machine_id_ = "Machine-1";
    current.supervisory_system_port_ = 1248;
    current.upstream_configurations_ = { upstream };
    current.downstream_configurations_ = { downstream };
    messages.emplace_back(current);

    board_forecast forecast;
    forecast.forecast_id_ = "forecast-1";
    forecast.time_until_available_ = 12.5;
    forecast.board_id_ = available.board_id_;
    forecast.board_id_created_by_ = "Machine-1";
    forecast.failed_board_ = board_quality::ANY;
    forecast.product_type_id_ = "Product-1";
    forecast.flipped_board_ = flipped_board::UNKNOWN;
    forecast.top_barcode_ = "top";
    forecast.bottom_barcode_ = "bottom";
    forecast.length_ = 100.0;
    forecast.width_ = 50.0;
    forecast.thickness_ = 1.0;
    forecast.conveyor_speed_ = 200.0;
    forecast.top_clearance_height_ = 20.0;
    forecast.bottom_clearance_height_ = 10.0;
    forecast.weight_ = 40.0;
    forecast.work_order_id_ = "WorkOrder-1";
    forecast.batch_id_ = "Batch-1";
    messages.emplace_back(forecast);

    query_board_info query;
    query.top_barcode_ = "top";
    query.bottom_barcode_ = "bottom";
    messages.emplace_back(query);

    send_board_info send_board;
    send_board.board_id_ = available.board_id_;
    send_board.board_id_created_by_ = "Machine-1";
    send_board.failed_board_ = board_quality::GOOD;
    send_board.product_type_id_ = "Product-1";
    send_board.flipped_board_ = flipped_board::TOP_SIDE_UP;
    send_board.top_barcode_ = "top";
    send_board.bottom_barcode_ = "bottom";
    send_board.length_ = 100.0;
    send_board.width_ = 50.0;
    send_board.thickness_ = 1.0;
    send_board.top_clearance_height_ = 20.0;
    send_board.bottom_clearance_height_ = 10.0;
    send_board.weight_ = 40.0;
    send_board.work_order_id_ = "WorkOrder-1";
    send_board.batch_id_ = "Batch-1";
    send_board.route_ = 7;
    send_board.action_ = 2;
    send_board.sub_boards_ = { first };
    messages.emplace_back(send_board);

    supervisory_service_description supervisory;
    supervisory.system_id_ = "Supervisor-1";
    supervisory.version_ = "1.6";
    supervisory.supported_features_ = supervisory_features { true, true, true, true, true, true, true, true };
    messages.emplace_back(supervisory);

    board_arrived arrived;
    arrived.machine_id_ = "Machine-1";
    arrived.upstream_lane_id_ = 1;
    arrived.upstream_interface_id_ = "Interface-1";
    arrived.magazine_id_ = "Magazine-1";
    arrived.slot_id_ = 7;
    arrived.board_transfer_ = arrival_transfer::LOADED;
    arrived.board_id_ = available.board_id_;
    arrived.board_id_created_by_ = "Machine-1";
    arrived.failed_board_ = board_quality::GOOD;
    arrived.product_type_id_ = "Product-1";
    arrived.flipped_board_ = flipped_board::TOP_SIDE_UP;
    arrived.top_barcode_ = "top";
    arrived.bottom_barcode_ = "bottom";
    arrived.length_ = 100.0;
    arrived.width_ = 50.0;
    arrived.thickness_ = 1.0;
    arrived.conveyor_speed_ = 200.0;
    arrived.top_clearance_height_ = 20.0;
    arrived.bottom_clearance_height_ = 10.0;
    arrived.weight_ = 40.0;
    arrived.work_order_id_ = "WorkOrder-1";
    arrived.batch_id_ = "Batch-1";
    arrived.route_ = 7;
    arrived.action_ = 1;
    arrived.sub_boards_ = { first };
    messages.emplace_back(arrived);

    board_departed departed;
    departed.machine_id_ = "Machine-1";
    departed.downstream_lane_id_ = 2;
    departed.downstream_interface_id_ = "Interface-2";
    departed.magazine_id_ = "Magazine-1";
    departed.slot_id_ = 8;
    departed.board_transfer_ = departure_transfer::REMOVED;
    departed.board_id_ = available.board_id_;
    departed.board_id_created_by_ = "Machine-1";
    departed.failed_board_ = board_quality::FAILED;
    departed.product_type_id_ = "Product-1";
    departed.flipped_board_ = flipped_board::BOTTOM_SIDE_UP;
    departed.top_barcode_ = "top";
    departed.bottom_barcode_ = "bottom";
    departed.length_ = 100.0;
    departed.width_ = 50.0;
    departed.thickness_ = 1.0;
    departed.conveyor_speed_ = 200.0;
    departed.top_clearance_height_ = 20.0;
    departed.bottom_clearance_height_ = 10.0;
    departed.weight_ = 40.0;
    departed.work_order_id_ = "WorkOrder-1";
    departed.batch_id_ = "Batch-1";
    departed.route_ = 7;
    departed.action_ = 2;
    departed.sub_boards_ = { second };
    messages.emplace_back(departed);

    query_work_order_info query_work;
    query_work.query_id_ = "query-1";
    query_work.machine_id_ = "Machine-1";
    query_work.magazine_id_ = "Magazine-1";
    query_work.slot_id_ = 3;
    query_work.barcode_ = "barcode";
    query_work.work_order_id_ = "WorkOrder-1";
    query_work.batch_id_ = "Batch-1";
    messages.emplace_back(query_work);

    send_work_order_info send_work;
    send_work.query_id_ = "query-1";
    send_work.work_order_id_ = "WorkOrder-1";
    send_work.batch_id_ = "Batch-1";
    send_work.board_id_ = available.board_id_;
    send_work.board_id_created_by_ = "Machine-1";
    send_work.failed_board_ = board_quality::GOOD;
    send_work.product_type_id_ = "Product-1";
    send_work.flipped_board_ = flipped_board::TOP_SIDE_UP;
    send_work.top_barcode_ = "top";
    send_work.bottom_barcode_ = "bottom";
    send_work.length_ = 100.0;
    send_work.width_ = 50.0;
    send_work.thickness_ = 1.0;
    send_work.conveyor_speed_ = 200.0;
    send_work.top_clearance_height_ = 20.0;
    send_work.bottom_clearance_height_ = 10.0;
    send_work.weight_ = 40.0;
    send_work.route_ = 7;
    send_work.sub_boards_ = { first };
    messages.emplace_back(send_work);

    messages.emplace_back(
        reply_work_order_info { "WorkOrder-1", "Batch-1", reply_work_order_status::ACCEPTED_AND_QUEUED }
    );
    messages.emplace_back(command { command_code::LOCK_INPUT_CONVEYOR });

    send_hermes_capabilities capabilities;
    capabilities.optional_messages_.command_ = true;
    capabilities.optional_messages_.board_forecast_ = true;
    capabilities.attributes_.product_type_id_ = 1;
    capabilities.attributes_.sub_boards_ = 7;
    messages.emplace_back(capabilities);

    return messages;
}

TEST(xml, round_trips_every_message) {
    for (const auto& message : all_messages()) {
        const auto document = to_xml(message);
        const auto parsed = parse(document);
        ASSERT_TRUE(parsed.has_value()) << message_name_of(message) << ": " << to_xml(message);
        EXPECT_EQ(parsed->message_, message) << message_name_of(message);
    }
}

TEST(xml, writes_the_wire_format_of_the_standard) {
    EXPECT_EQ(
        to_xml(
            any_message {
                check_alive { check_alive_type::PING, "abc" }
    }
        ),
        "<Hermes><CheckAlive Type=\"1\" Id=\"abc\"/></Hermes>"
    );
    EXPECT_EQ(to_xml(any_message { get_configuration {} }), "<Hermes><GetConfiguration/></Hermes>");
    EXPECT_EQ(to_xml(any_message { revoke_board_available {} }), "<Hermes><RevokeBoardAvailable/></Hermes>");

    board_available available;
    available.board_id_ = "B";
    available.board_id_created_by_ = "C";
    EXPECT_EQ(
        to_xml(any_message { available }),
        "<Hermes><BoardAvailable BoardId=\"B\" BoardIdCreatedBy=\"C\" FailedBoard=\"0\" FlippedBoard=\"0\"/></Hermes>"
    );

    service_description description;
    description.machine_id_ = "M";
    description.lane_id_ = 2;
    description.interface_id_ = "IF";
    description.supported_features_.board_forecast_ = true;
    description.supported_features_.command_ = true;
    EXPECT_EQ(
        to_xml(any_message { description }),
        "<Hermes><ServiceDescription MachineId=\"M\" LaneId=\"2\" InterfaceId=\"IF\" Version=\"1.6\">"
        "<SupportedFeatures><FeatureBoardForecast/><FeatureCommand/></SupportedFeatures>"
        "</ServiceDescription></Hermes>"
    );

    send_hermes_capabilities capabilities;
    capabilities.optional_messages_.command_ = true;
    capabilities.attributes_.product_type_id_ = 1;
    capabilities.attributes_.sub_boards_ = 7;
    EXPECT_EQ(
        to_xml(any_message { capabilities }),
        "<Hermes><SendHermesCapabilities><OptionalMessages><MessageCommand/></OptionalMessages>"
        "<Attributes ProductTypeId=\"1\" SubBoards=\"7\"/></SendHermesCapabilities></Hermes>"
    );

    set_configuration configuration;
    configuration.machine_id_ = "M";
    upstream_configuration upstream;
    upstream.upstream_lane_id_ = 1;
    upstream.host_address_ = "1.2.3.4";
    upstream.port_ = 50100;
    configuration.upstream_configurations_ = { upstream };
    EXPECT_EQ(
        to_xml(any_message { configuration }),
        "<Hermes><SetConfiguration MachineId=\"M\"><UpstreamConfigurations><UpstreamConfiguration "
        "UpstreamLaneId=\"1\" HostAddress=\"1.2.3.4\" "
        "Port=\"50100\"/></UpstreamConfigurations></SetConfiguration></Hermes>"
    );
}

TEST(xml, leaves_optional_fields_out) {
    board_available available;
    available.board_id_ = "B";
    available.board_id_created_by_ = "C";
    const auto document = to_xml(any_message { available });
    EXPECT_EQ(document.find("ProductTypeId"), std::string::npos);
    EXPECT_EQ(document.find("SubBoards"), std::string::npos);

    const auto parsed = parse(document);
    ASSERT_TRUE(parsed.has_value());
    const auto& value = std::get<board_available>(parsed->message_);
    EXPECT_FALSE(value.product_type_id_.has_value());
    EXPECT_FALSE(value.route_.has_value());
    EXPECT_TRUE(value.sub_boards_.empty());
}

TEST(xml, reads_the_wire_format_of_the_standard) {
    const auto parsed = parse(
        "<Hermes><BoardAvailable BoardId=\"11111111-2222-3333-4444-555555555555\" BoardIdCreatedBy=\"M\" "
        "FailedBoard=\"1\" FlippedBoard=\"2\" Length=\"1.5\" Route=\"900\" Action=\"2\">"
        "<SubBoards><SB Pos=\"1\" Bc=\"a\" St=\"1\"/><SB Pos=\"2\" St=\"4\"/></SubBoards>"
        "</BoardAvailable></Hermes>"
    );
    ASSERT_TRUE(parsed.has_value());
    const auto& value = std::get<board_available>(parsed->message_);
    EXPECT_EQ(value.board_id_, "11111111-2222-3333-4444-555555555555");
    EXPECT_EQ(value.failed_board_, board_quality::GOOD);
    EXPECT_EQ(value.flipped_board_, flipped_board::BOTTOM_SIDE_UP);
    EXPECT_EQ(value.length_, 1.5);
    EXPECT_EQ(value.route_, 900);
    EXPECT_EQ(value.action_, 2);
    ASSERT_EQ(value.sub_boards_.size(), 2U);
    EXPECT_EQ(value.sub_boards_[0].state_, sub_board_state::GOOD);
    EXPECT_EQ(value.sub_boards_[0].barcode_, "a");
    EXPECT_FALSE(value.sub_boards_[1].barcode_.has_value());
    EXPECT_EQ(value.sub_boards_[1].state_, sub_board_state::SKIP);
}

TEST(xml, reads_the_configuration_lists_with_and_without_their_wrapper) {
    const auto wrapped = parse(
        "<Hermes><CurrentConfiguration><UpstreamConfigurations>"
        "<UpstreamConfiguration UpstreamLaneId=\"1\" HostAddress=\"h\" Port=\"1\"/>"
        "</UpstreamConfigurations></CurrentConfiguration></Hermes>"
    );
    ASSERT_TRUE(wrapped.has_value());
    EXPECT_EQ(std::get<current_configuration>(wrapped->message_).upstream_configurations_.size(), 1U);

    const auto flat = parse(
        "<Hermes><CurrentConfiguration>"
        "<UpstreamConfiguration UpstreamLaneId=\"1\" HostAddress=\"h\" Port=\"1\"/>"
        "</CurrentConfiguration></Hermes>"
    );
    ASSERT_TRUE(flat.has_value());
    EXPECT_EQ(std::get<current_configuration>(flat->message_).upstream_configurations_.size(), 1U);
}

TEST(xml, reports_a_missing_mandatory_field) {
    const auto parsed = parse("<Hermes><BoardAvailable BoardId=\"B\"/></Hermes>");
    ASSERT_FALSE(parsed.has_value());
    EXPECT_EQ(parsed.error().code_, error_code::PEER);
    EXPECT_NE(parsed.error().text_.find("BoardIdCreatedBy"), std::string::npos);
}

TEST(xml, reports_a_malformed_document) {
    const auto parsed = parse("this is not xml");
    ASSERT_FALSE(parsed.has_value());
    EXPECT_EQ(parsed.error().code_, error_code::MALFORMED);
}

TEST(xml, reports_an_unknown_message) {
    const auto parsed = parse("<Hermes><SomeFutureMessage A=\"1\"/></Hermes>");
    ASSERT_FALSE(parsed.has_value());
    EXPECT_EQ(parsed.error().code_, error_code::UNSUPPORTED);
    EXPECT_NE(parsed.error().text_.find("SomeFutureMessage"), std::string::npos);
}

TEST(xml, rejects_two_messages_in_one_envelope) {
    const auto parsed = parse("<Hermes><CheckAlive/><CheckAlive/></Hermes>");
    ASSERT_FALSE(parsed.has_value());
    EXPECT_EQ(parsed.error().code_, error_code::MALFORMED);
}

TEST(xml, keeps_the_timestamp) {
    const auto parsed = parse("<Hermes Timestamp=\"2017-07-16T19:20:30.452\"><CheckAlive/></Hermes>");
    ASSERT_TRUE(parsed.has_value());
    ASSERT_TRUE(parsed->timestamp_.has_value());
    EXPECT_EQ(*parsed->timestamp_, "2017-07-16T19:20:30.452");

    envelope envelope;
    envelope.timestamp_ = "2026-10-18T12:00:00.000";
    envelope.message_ = check_alive {};
    const auto document = to_xml(envelope);
    EXPECT_NE(document.find("Timestamp=\"2026-10-18T12:00:00.000\""), std::string::npos);
    EXPECT_TRUE(parse(document).has_value());
    EXPECT_EQ(
        format_timestamp(std::chrono::system_clock::time_point {}).size(),
        std::string("1970-01-01T00:00:00.000").size()
    );
}

TEST(xml, escapes_text_and_reads_it_back) {
    notification note;
    note.notification_code_ = notification_code::UNSPECIFIED;
    note.severity_ = severity::INFO;
    note.description_ = "a < b & \"c\" 'd'";

    const auto document = to_xml(any_message { note });
    EXPECT_EQ(document.find("a < b"), std::string::npos);
    const auto parsed = parse(document);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(std::get<notification>(parsed->message_).description_, note.description_);
}

TEST(xml, writes_floating_point_values_that_read_back_unchanged) {
    for (const double value : { 0.1, 1.0, 123456.789, 0.000001, 987654321.5 }) {
        board_available available;
        available.board_id_ = "B";
        available.board_id_created_by_ = "C";
        available.length_ = value;

        const auto parsed = parse(to_xml(any_message { available }));
        ASSERT_TRUE(parsed.has_value());
        EXPECT_EQ(std::get<board_available>(parsed->message_).length_, value);
    }
}

TEST(xml, drops_the_sub_boards_when_the_document_would_be_too_large) {
    board_available available;
    available.board_id_ = "B";
    available.board_id_created_by_ = "C";
    available.sub_boards_.reserve(500);
    for (std::uint16_t index = 0; index < 500; ++index) {
        sub_board sub_board;
        sub_board.pos_ = index;
        sub_board.state_ = sub_board_state::GOOD;
        sub_board.barcode_ = std::string(200, 'x');
        available.sub_boards_.push_back(std::move(sub_board));
    }

    const auto document = to_xml(any_message { available });
    EXPECT_LE(document.size(), MAX_MESSAGE_SIZE);

    const auto parsed = parse(document);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_TRUE(std::get<board_available>(parsed->message_).sub_boards_.empty());
}

TEST(xml, rejects_an_enum_value_outside_the_standard) {
    const auto parsed = parse(
        "<Hermes><BoardAvailable BoardId=\"B\" BoardIdCreatedBy=\"C\" FailedBoard=\"9\" FlippedBoard=\"0\"/></Hermes>"
    );
    ASSERT_FALSE(parsed.has_value());
    EXPECT_EQ(parsed.error().code_, error_code::PEER);
    EXPECT_NE(parsed.error().text_.find("FailedBoard"), std::string::npos);
}

TEST(xml, discards_an_unknown_value_of_an_optional_enum) {
    const auto parsed = parse("<Hermes><MachineReady FailedBoard=\"0\" FlippedBoard=\"9\"/></Hermes>");
    ASSERT_TRUE(parsed.has_value());
    EXPECT_FALSE(std::get<machine_ready>(parsed->message_).flipped_board_.has_value());
}

} // namespace
