// SPDX-License-Identifier: MIT
///
/// @file downstream_upstream_pair.cxx
/// @author BA7LYA (1042140025@qq.com)
/// @brief Runs one downstream server and one upstream client on loopback and walks a complete
/// board handover: connect, exchange service descriptions, offer and accept a board, transport it
/// and shut down again.
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

///
/// @brief Records what one facade reports and lets the example wait for the next step.
///
struct recorder {
    void on_state(session_id id, channel_state state) {
        {
            const std::lock_guard lock(mutex_);
            session_ = id;
            current_ = state;
        }
        cv_.notify_all();
    }

    bool wait_state(channel_state expected) {
        std::unique_lock lock(mutex_);
        return cv_.wait_for(lock, TIMEOUT, [&] { return current_ == expected; });
    }

    std::mutex mutex_;
    std::condition_variable cv_;
    session_id session_ {};
    channel_state current_ { channel_state::NOT_CONNECTED };
};

} // namespace

int main() {
    const std::string provider_id { "UpstreamMachine" };
    const std::string receiver_id { "DownstreamMachine" };

    downstream_settings server_settings;
    server_settings.machine_id_ = provider_id;
    server_settings.port_ = 0; // let the system choose a free port
    server_settings.features_.check_alive_response_ = true;

    downstream provider(server_settings);
    recorder provider_record;
    provider.on_state([&](session_id id, channel_state state) { provider_record.on_state(id, state); });
    provider.on_message([](session_id, channel_state state, const any_message& message) {
        std::cout << "  [provider] " << message_name_of(message) << " in " << to_string(state) << '\n';
    });
    std::mutex port_mutex;
    std::condition_variable port_cv;
    std::uint16_t port = 0;
    provider.on_listening([&](std::uint16_t value) {
        {
            const std::lock_guard lock(port_mutex);
            port = value;
        }
        port_cv.notify_all();
    });
    provider.start();
    {
        std::unique_lock lock(port_mutex);
        port_cv.wait_for(lock, 2s, [&] { return port != 0; });
    }
    if (port == 0) {
        std::cerr << "the server did not bind a port\n";
        return 1;
    }

    upstream_settings client_settings;
    client_settings.machine_id_ = receiver_id;
    client_settings.address_ = "127.0.0.1";
    client_settings.port_ = port;
    client_settings.features_.check_alive_response_ = true;

    upstream receiver(client_settings);
    recorder receiver_record;
    receiver.on_state([&](session_id id, channel_state state) { receiver_record.on_state(id, state); });
    receiver.on_message([](session_id, channel_state state, const any_message& message) {
        std::cout << "  [receiver] " << message_name_of(message) << " in " << to_string(state) << '\n';
    });
    receiver.start();

    bool ok = provider_record.wait_state(channel_state::NOT_AVAILABLE_NOT_READY)
           && receiver_record.wait_state(channel_state::NOT_AVAILABLE_NOT_READY);
    std::cout << "1. connected and handshaken on port " << port << ": " << (ok ? "yes" : "no") << '\n';

    board_available board;
    board.board_id_ = "11111111-2222-3333-4444-555555555555";
    board.board_id_created_by_ = provider_id;
    board.failed_board_ = board_quality::GOOD;
    board.flipped_board_ = flipped_board::TOP_SIDE_UP;
    board.length_ = 100.0;
    board.width_ = 50.0;

    machine_ready ready;
    ready.failed_board_ = board_quality::GOOD;

    if (ok) {
        ok = provider.send(provider_record.session_, board).has_value()
          && receiver.send(receiver_record.session_, ready).has_value()
          && provider_record.wait_state(channel_state::AVAILABLE_AND_READY)
          && receiver_record.wait_state(channel_state::AVAILABLE_AND_READY);
        std::cout << "2. board offered and accepted: " << (ok ? "yes" : "no") << '\n';
    }

    start_transport start;
    start.board_id_ = board.board_id_;
    start.conveyor_speed_ = 250.0;

    if (ok) {
        ok = receiver.send(receiver_record.session_, start).has_value()
          && provider_record.wait_state(channel_state::TRANSPORTING)
          && receiver_record.wait_state(channel_state::TRANSPORTING);
        std::cout << "3. transport started: " << (ok ? "yes" : "no") << '\n';
    }

    transport_finished finished;
    finished.board_id_ = start.board_id_;
    finished.transfer_state_ = transfer_state::COMPLETE;

    stop_transport stop;
    stop.board_id_ = start.board_id_;
    stop.transfer_state_ = transfer_state::COMPLETE;

    if (ok) {
        ok = provider.send(provider_record.session_, finished).has_value()
          && receiver.send(receiver_record.session_, stop).has_value()
          && provider_record.wait_state(channel_state::NOT_AVAILABLE_NOT_READY)
          && receiver_record.wait_state(channel_state::NOT_AVAILABLE_NOT_READY);
        std::cout << "4. transport finished: " << (ok ? "yes" : "no") << '\n';
    }

    const auto goodbye = notification { notification_code::MACHINE_SHUTDOWN, severity::INFO, "example finished" };
    receiver.stop(goodbye);
    provider.stop(goodbye);

    if (!ok) {
        std::cerr << "the board handover did not complete within the timeout\n";
        return 1;
    }

    std::cout << "5. disconnected\n";
    return 0;
}
