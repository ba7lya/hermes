// SPDX-License-Identifier: MIT
///
/// @file configuration_example.cxx
/// @author BA7LYA (1042140025@qq.com)
/// @brief Offers a configuration service on loopback and reads and writes it with the client
/// functions of the library.
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
    downstream.downstream_lane_id_ = 1;
    downstream.port_ = 50101;
    configuration.downstream_configurations_ = { downstream };

    return configuration;
}

void print(const current_configuration& configuration) {
    std::cout << "   machine=" << configuration.machine_id_.value_or("<unset>") << " supervisory-port="
              << (configuration.supervisory_system_port_ ? std::to_string(*configuration.supervisory_system_port_)
                                                         : "-")
              << '\n';
    for (const auto& upstream : configuration.upstream_configurations_) {
        std::cout << "   upstream " << upstream << '\n';
    }
    for (const auto& downstream : configuration.downstream_configurations_) {
        std::cout << "   downstream " << downstream << '\n';
    }
}

} // namespace

int main() {
    configuration_service_settings settings;
    settings.address_ = "127.0.0.1";
    settings.port_ = 0; // let the system choose a free port

    configuration_service service(settings);
    current_configuration stored = make_configuration("Machine-A");

    service.on_get_configuration([&](session_id, const connection_info&) { return stored; });
    service.on_set_configuration([&](session_id, const connection_info&, const set_configuration& value) {
        if (value.upstream_configurations_.empty()) {
            return error_info { error_code::PEER, "no upstream configuration" };
        }

        stored.machine_id_ = value.machine_id_;
        stored.supervisory_system_port_ = value.supervisory_system_port_;
        stored.upstream_configurations_ = value.upstream_configurations_;
        stored.downstream_configurations_ = value.downstream_configurations_;
        return error_info {};
    });

    std::mutex port_mutex;
    std::condition_variable port_cv;
    std::uint16_t port = 0;
    service.on_listening([&](std::uint16_t value) {
        {
            const std::lock_guard lock(port_mutex);
            port = value;
        }
        port_cv.notify_all();
    });
    service.start();
    {
        std::unique_lock lock(port_mutex);
        port_cv.wait_for(lock, 2s, [&] { return port != 0; });
    }
    if (port == 0) {
        std::cerr << "the configuration service did not bind a port\n";
        return 1;
    }
    std::cout << "configuration service on 127.0.0.1:" << port << '\n';

    std::cout << "1. read the configuration\n";
    const auto read = read_configuration("127.0.0.1", port, 3s);
    if (!read) {
        std::cerr << "   read failed: " << read.error() << '\n';
        return 1;
    }
    if (read->configuration_) { print(*read->configuration_); }

    std::cout << "2. write a new configuration\n";
    set_configuration write;
    write.machine_id_ = "Machine-B";
    write.supervisory_system_port_ = 1300;
    write.upstream_configurations_ = make_configuration("ignored").upstream_configurations_;
    write.downstream_configurations_ = make_configuration("ignored").downstream_configurations_;

    // A service does not answer an accepted set_configuration, so the client waits for the timeout
    // and reports success when no notification arrived meanwhile.
    const auto written = write_configuration("127.0.0.1", write, port, 500ms);
    if (!written) {
        std::cerr << "   write failed: " << written.error() << '\n';
        return 1;
    }
    std::cout << "   accepted\n";

    std::cout << "3. read it back\n";
    const auto again = read_configuration("127.0.0.1", port, 3s);
    if (!again) {
        std::cerr << "   read failed: " << again.error() << '\n';
        return 1;
    }
    if (again->configuration_) { print(*again->configuration_); }

    std::cout << "4. a rejected configuration\n";
    set_configuration invalid;
    invalid.machine_id_ = "Machine-C";
    const auto rejected = write_configuration("127.0.0.1", invalid, port, 3s);
    if (rejected) {
        std::cerr << "   the invalid configuration was accepted\n";
        return 1;
    }
    std::cout << "   rejected: " << rejected.error() << '\n';

    service.stop();
    std::cout << "5. done\n";
    return 0;
}
