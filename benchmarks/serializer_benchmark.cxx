// SPDX-License-Identifier: MIT
///
/// @file serializer_benchmark.cxx
/// @author BA7LYA (1042140025@qq.com)
/// @brief Benchmarks the XML codec of the library.
/// @version 0.1
/// @date 2026-10-18
/// @copyright Copyright (c) 2026
///

#include <benchmark/benchmark.h>
#include <cstdint>
#include <string>
#include <vector>

#include "hermes.hxx"

namespace {

using namespace ba7lya::hermes;

board_available make_board() {
    board_available board;
    board.board_id_ = "11111111-2222-3333-4444-555555555555";
    board.board_id_created_by_ = "Machine-A";
    board.failed_board_ = board_quality::GOOD;
    board.product_type_id_ = "Product-1";
    board.flipped_board_ = flipped_board::TOP_SIDE_UP;
    board.top_barcode_ = "TOP-1234567890";
    board.bottom_barcode_ = "BOTTOM-1234567890";
    board.length_ = 100.5;
    board.width_ = 50.25;
    board.thickness_ = 1.6;
    board.conveyor_speed_ = 250.0;
    board.top_clearance_height_ = 30.0;
    board.bottom_clearance_height_ = 12.5;
    board.weight_ = 42.0;
    board.work_order_id_ = "WorkOrder-1";
    board.batch_id_ = "Batch-1";
    board.route_ = 7;
    board.action_ = 1;

    board.sub_boards_.reserve(16);
    for (std::uint16_t index = 0; index < 16; ++index) {
        sub_board sub_board;
        sub_board.pos_ = index;
        sub_board.barcode_ = "SUB-" + std::to_string(index);
        sub_board.state_ = sub_board_state::GOOD;
        board.sub_boards_.push_back(std::move(sub_board));
    }
    return board;
}

send_hermes_capabilities make_capabilities() {
    send_hermes_capabilities capabilities;
    capabilities.optional_messages_.check_alive_response_ = true;
    capabilities.optional_messages_.board_forecast_ = true;
    capabilities.optional_messages_.query_board_info_ = true;
    capabilities.optional_messages_.send_board_info_ = true;
    capabilities.optional_messages_.command_ = true;
    capabilities.attributes_.product_type_id_ = 7;
    capabilities.attributes_.length_ = 7;
    capabilities.attributes_.width_ = 7;
    capabilities.attributes_.weight_ = 7;
    capabilities.attributes_.sub_boards_ = 5;
    return capabilities;
}

void benchmark_serialize_board_available(benchmark::State& state) {
    const any_message message { make_board() };
    for (auto _ : state) {
        const auto document = to_xml(message);
        benchmark::DoNotOptimize(document);
    }
}

BENCHMARK(benchmark_serialize_board_available);

void benchmark_parse_board_available(benchmark::State& state) {
    const auto document = to_xml(any_message { make_board() });
    for (auto _ : state) {
        const auto envelope = parse(document);
        benchmark::DoNotOptimize(envelope);
    }
}

BENCHMARK(benchmark_parse_board_available);

void benchmark_serialize_service_description(benchmark::State& state) {
    service_description description;
    description.machine_id_ = "Machine-A";
    description.lane_id_ = 1;
    description.interface_id_ = "Interface-1";
    description.supported_features_.check_alive_response_ = true;
    description.supported_features_.command_ = true;
    const any_message message { description };

    for (auto _ : state) {
        const auto document = to_xml(message);
        benchmark::DoNotOptimize(document);
    }
}

BENCHMARK(benchmark_serialize_service_description);

void benchmark_parse_send_hermes_capabilities(benchmark::State& state) {
    const auto document = to_xml(any_message { make_capabilities() });
    for (auto _ : state) {
        const auto envelope = parse(document);
        benchmark::DoNotOptimize(envelope);
    }
}

BENCHMARK(benchmark_parse_send_hermes_capabilities);

void benchmark_round_trip_board_available(benchmark::State& state) {
    for (auto _ : state) {
        const auto document = to_xml(any_message { make_board() });
        const auto envelope = parse(document);
        benchmark::DoNotOptimize(envelope);
    }
}

BENCHMARK(benchmark_round_trip_board_available);

} // namespace

BENCHMARK_MAIN();
