// SPDX-License-Identifier: MIT
///
/// @file vertical_example.cxx
/// @author BA7LYA (1042140025@qq.com)
/// @brief Runs a machine side vertical_service and a supervisory vertical_client on loopback and
/// exchanges configuration, board tracking and work order data.
/// @version 0.1
/// @date 2026-10-18
/// @copyright Copyright (c) 2026
///

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <string>

#include "hermes.hxx"

using namespace ba7lya::hermes;
using namespace std::chrono_literals;

namespace {

constexpr auto TIMEOUT = 5s;

struct recorder {
    void on_state(session_id id, vertical_state state) {
        {
            const std::lock_guard lock(mutex_);
            session_ = id;
            current_ = state;
        }
        cv_.notify_all();
    }

    bool wait_state(vertical_state expected) {
        std::unique_lock lock(mutex_);
        return cv_.wait_for(lock, TIMEOUT, [&] { return current_ == expected; });
    }

    std::mutex mutex_;
    std::condition_variable cv_;
    session_id session_ {};
    vertical_state current_ { vertical_state::NOT_CONNECTED };
};

} // namespace

int main() {
    const std::string machine_id { "Machine-A" };
    const std::string supervisor_id { "Supervisor-A" };

    supervisory_features features;
    features.configuration_ = true;
    features.board_tracking_ = true;
    features.query_work_order_info_ = true;
    features.send_work_order_info_ = true;
    features.reply_work_order_info_ = true;

    vertical_service_settings service_settings;
    service_settings.system_id_ = machine_id;
    service_settings.address_ = "127.0.0.1";
    service_settings.port_ = 0; // let the system choose a free port
    service_settings.features_ = features;

    vertical_service machine(service_settings);
    recorder machine_record;
    machine.on_state([&](session_id id, vertical_state state) { machine_record.on_state(id, state); });
    machine.on_message([&](session_id, vertical_state, const any_message& message) {
        std::cout << "  [machine] received " << message_name_of(message) << '\n';
    });
    std::mutex port_mutex;
    std::condition_variable port_cv;
    std::uint16_t port = 0;
    machine.on_listening([&](std::uint16_t value) {
        {
            const std::lock_guard lock(port_mutex);
            port = value;
        }
        port_cv.notify_all();
    });
    machine.start();
    {
        std::unique_lock lock(port_mutex);
        port_cv.wait_for(lock, 2s, [&] { return port != 0; });
    }
    if (port == 0) {
        std::cerr << "the vertical service did not bind a port\n";
        return 1;
    }

    vertical_client_settings client_settings;
    client_settings.system_id_ = supervisor_id;
    client_settings.address_ = "127.0.0.1";
    client_settings.port_ = port;
    client_settings.features_ = features;

    vertical_client supervisor(client_settings);
    recorder supervisor_record;
    supervisor.on_state([&](session_id id, vertical_state state) { supervisor_record.on_state(id, state); });
    supervisor.on_message([&](session_id, vertical_state, const any_message& message) {
        std::cout << "  [supervisor] received " << message_name_of(message) << '\n';
    });
    supervisor.start();

    bool ok = machine_record.wait_state(vertical_state::CONNECTED)
           && supervisor_record.wait_state(vertical_state::CONNECTED);
    std::cout << "1. vertical channel connected on port " << port << ": " << (ok ? "yes" : "no") << '\n';

    if (ok) {
        current_configuration configuration;
        configuration.machine_id_ = machine_id;
        configuration.supervisory_system_port_ = 1250;
        upstream_configuration upstream;
        upstream.upstream_lane_id_ = 1;
        upstream.host_address_ = "192.168.0.10";
        upstream.port_ = 50100;
        configuration.upstream_configurations_ = { upstream };

        ok = machine.send(machine_record.session_, configuration).has_value();
        std::cout << "2. machine reports its configuration: " << (ok ? "yes" : "no") << '\n';
    }

    if (ok) {
        board_arrived arrived;
        arrived.machine_id_ = machine_id;
        arrived.upstream_lane_id_ = 1;
        arrived.board_transfer_ = arrival_transfer::TRANSFERRED;
        arrived.board_id_ = "11111111-2222-3333-4444-555555555555";
        arrived.board_id_created_by_ = machine_id;
        arrived.failed_board_ = board_quality::GOOD;
        arrived.flipped_board_ = flipped_board::TOP_SIDE_UP;
        arrived.top_barcode_ = "TOP-1";

        ok = machine.send(machine_record.session_, arrived).has_value();
        std::cout << "3. machine reports an arrived board: " << (ok ? "yes" : "no") << '\n';
    }

    if (ok) {
        query_work_order_info query;
        query.query_id_ = "query-1";
        query.machine_id_ = machine_id;
        query.barcode_ = "TOP-1";

        send_work_order_info answer;
        answer.query_id_ = query.query_id_;
        answer.work_order_id_ = "WorkOrder-1";
        answer.product_type_id_ = "Product-1";

        ok = machine.send(machine_record.session_, query).has_value()
          && supervisor.send(supervisor_record.session_, answer).has_value();
        std::cout << "4. machine queries the work order: " << (ok ? "yes" : "no") << '\n';
    }

    if (ok) {
        reply_work_order_info reply;
        reply.work_order_id_ = "WorkOrder-1";
        reply.status_ = reply_work_order_status::ACCEPTED_AND_READY;
        ok = machine.send(machine_record.session_, reply).has_value();
        std::cout << "5. machine reports the work order status: " << (ok ? "yes" : "no") << '\n';
    }

    const auto goodbye = notification { notification_code::MACHINE_SHUTDOWN, severity::INFO, "example finished" };
    supervisor.stop(goodbye);
    machine.stop();

    if (!ok) {
        std::cerr << "the vertical exchange did not complete within the timeout\n";
        return 1;
    }

    std::cout << "6. disconnected\n";
    return 0;
}
