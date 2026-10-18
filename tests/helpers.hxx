// SPDX-License-Identifier: MIT
///
/// @file helpers.hxx
/// @author BA7LYA (1042140025@qq.com)
/// @brief The fixtures the test suites share: a recorder of facade callbacks and port helpers.
/// @version 0.1
/// @date 2026-10-18
/// @copyright Copyright (c) 2026
///

#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "hermes.hxx"

namespace hermes_test {

using namespace ba7lya::hermes;
using namespace std::chrono_literals;

/// @brief One trace line a facade reported.
struct trace_line {
    trace_type type_ {};
    std::string text_;

    bool operator==(const trace_line&) const = default;
};

///
/// @brief Records what a facade reports and lets a test wait for a condition.
///
/// The callbacks are invoked on the facade's I/O thread; everything the test reads is returned as
/// a copy under the recorder's mutex.
///
class recorder {
public:
    //--- registration -------------------------------------------------------

    void attach(downstream& facade) {
        facade.on_connected([this](session_id id, const connection_info& info) { record_connected(id, info); });
        facade.on_state([this](session_id id, channel_state state) { record_state(id, state); });
        facade.on_message([this](session_id id, channel_state state, const any_message& message) {
            record_horizontal_message(id, state, message);
        });
        facade.on_disconnected([this](session_id id, const error_info& error) { record_disconnected(id, error); });
        facade.on_trace([this](session_id id, trace_type type, std::string_view text) {
            record_trace(id, type, text);
        });
        facade.on_error([this](const error_info& error) { record_error(error); });
    }

    void attach(upstream& facade) {
        facade.on_connected([this](session_id id, const connection_info& info) { record_connected(id, info); });
        facade.on_state([this](session_id id, channel_state state) { record_state(id, state); });
        facade.on_message([this](session_id id, channel_state state, const any_message& message) {
            record_horizontal_message(id, state, message);
        });
        facade.on_disconnected([this](session_id id, const error_info& error) { record_disconnected(id, error); });
        facade.on_trace([this](session_id id, trace_type type, std::string_view text) {
            record_trace(id, type, text);
        });
        facade.on_error([this](const error_info& error) { record_error(error); });
    }

    void attach(vertical_service& facade) {
        facade.on_connected([this](session_id id, const connection_info& info) { record_connected(id, info); });
        facade.on_state([this](session_id id, vertical_state state) { record_vertical_state(id, state); });
        facade.on_message([this](session_id id, vertical_state state, const any_message& message) {
            record_vertical_message(id, state, message);
        });
        facade.on_disconnected([this](session_id id, const error_info& error) { record_disconnected(id, error); });
        facade.on_trace([this](session_id id, trace_type type, std::string_view text) {
            record_trace(id, type, text);
        });
        facade.on_error([this](const error_info& error) { record_error(error); });
    }

    void attach(vertical_client& facade) {
        facade.on_connected([this](session_id id, const connection_info& info) { record_connected(id, info); });
        facade.on_state([this](session_id id, vertical_state state) { record_vertical_state(id, state); });
        facade.on_message([this](session_id id, vertical_state state, const any_message& message) {
            record_vertical_message(id, state, message);
        });
        facade.on_disconnected([this](session_id id, const error_info& error) { record_disconnected(id, error); });
        facade.on_trace([this](session_id id, trace_type type, std::string_view text) {
            record_trace(id, type, text);
        });
        facade.on_error([this](const error_info& error) { record_error(error); });
    }

    //--- waiting ------------------------------------------------------------

    bool wait_state(channel_state expected, std::chrono::milliseconds timeout = 5s) {
        return wait(timeout, [this, expected] { return state_ == expected; });
    }

    bool wait_vertical_state(vertical_state expected, std::chrono::milliseconds timeout = 5s) {
        return wait(timeout, [this, expected] { return vertical_state_ == expected; });
    }

    bool wait_connected(std::chrono::milliseconds timeout = 5s) {
        return wait(timeout, [this] { return connected_; });
    }

    bool wait_connections(std::size_t count, std::chrono::milliseconds timeout = 8s) {
        return wait(timeout, [this, count] { return connections_ >= count; });
    }

    bool wait_disconnected(std::chrono::milliseconds timeout = 5s) {
        return wait(timeout, [this] { return disconnected_; });
    }

    bool wait_error(std::chrono::milliseconds timeout = 5s) {
        return wait(timeout, [this] { return static_cast<bool>(error_); });
    }

    bool wait_pong(std::chrono::milliseconds timeout = 3s) {
        return wait(timeout, [this] {
            for (const auto& message : messages_) {
                if (const auto* keep_alive = std::get_if<check_alive>(&message)) {
                    if (keep_alive->type_ == check_alive_type::PONG) { return true; }
                }
            }
            return false;
        });
    }

    template<class M>
    bool wait_message(std::chrono::milliseconds timeout = 5s) {
        return wait(timeout, [this] {
            for (const auto& message : messages_) {
                if (std::holds_alternative<M>(message)) { return true; }
            }
            return false;
        });
    }

    template<class M>
    std::optional<M> message(std::chrono::milliseconds timeout = 5s) {
        M found {};
        const bool ok = wait(timeout, [this] {
            for (const auto& message : messages_) {
                if (std::holds_alternative<M>(message)) { return true; }
            }
            return false;
        });
        if (!ok) { return std::nullopt; }

        const auto snapshot = take_messages();
        for (const auto& message : snapshot) {
            if (const auto* value = std::get_if<M>(&message)) { return *value; }
        }
        return std::nullopt;
    }

    template<class M>
    std::size_t count() const {
        const std::lock_guard lock(mutex_);
        std::size_t total = 0;
        for (const auto& message : messages_) {
            if (std::holds_alternative<M>(message)) { ++total; }
        }
        return total;
    }

    std::size_t count_traces(trace_type type) const {
        const std::lock_guard lock(mutex_);
        std::size_t total = 0;
        for (const auto& line : traces_) {
            if (line.type_ == type) { ++total; }
        }
        return total;
    }

    bool saw_trace(trace_type type, std::string_view needle, std::chrono::milliseconds timeout = 5s) {
        return wait(timeout, [this, type, needle] {
            for (const auto& line : traces_) {
                if (line.type_ == type && line.text_.find(needle) != std::string::npos) { return true; }
            }
            return false;
        });
    }

    //--- snapshots ----------------------------------------------------------

    channel_state state() const {
        const std::lock_guard lock(mutex_);
        return state_;
    }

    vertical_state current_vertical_state() const {
        const std::lock_guard lock(mutex_);
        return vertical_state_;
    }

    session_id session() const {
        const std::lock_guard lock(mutex_);
        return session_;
    }

    error_info last_error() const {
        const std::lock_guard lock(mutex_);
        return error_;
    }

    error_info disconnect_error() const {
        const std::lock_guard lock(mutex_);
        return disconnect_error_;
    }

    std::vector<channel_state> states() const {
        const std::lock_guard lock(mutex_);
        return states_;
    }

    std::vector<vertical_state> vertical_states() const {
        const std::lock_guard lock(mutex_);
        return vertical_states_;
    }

    std::vector<any_message> messages() const { return take_messages(); }

    std::vector<trace_line> traces() const {
        const std::lock_guard lock(mutex_);
        return traces_;
    }

    void reset() {
        const std::lock_guard lock(mutex_);
        state_ = channel_state::NOT_CONNECTED;
        vertical_state_ = vertical_state::NOT_CONNECTED;
        states_.clear();
        vertical_states_.clear();
        messages_.clear();
        traces_.clear();
        error_ = error_info {};
        disconnect_error_ = error_info {};
        connected_ = false;
        disconnected_ = false;
        connections_ = 0;
    }

private:
    template<class Predicate>
    bool wait(std::chrono::milliseconds timeout, Predicate predicate) {
        std::unique_lock lock(mutex_);
        return cv_.wait_for(lock, timeout, predicate);
    }

    std::vector<any_message> take_messages() const {
        const std::lock_guard lock(mutex_);
        return messages_;
    }

    void record_connected(session_id id, const connection_info& info) {
        {
            const std::lock_guard lock(mutex_);
            session_ = id;
            info_ = info;
            connected_ = true;
            ++connections_;
        }
        cv_.notify_all();
    }

    void record_state(session_id id, channel_state state) {
        {
            const std::lock_guard lock(mutex_);
            session_ = id;
            state_ = state;
            states_.push_back(state);
        }
        cv_.notify_all();
    }

    void record_vertical_state(session_id id, vertical_state state) {
        {
            const std::lock_guard lock(mutex_);
            session_ = id;
            vertical_state_ = state;
            vertical_states_.push_back(state);
        }
        cv_.notify_all();
    }

    void record_horizontal_message(session_id id, channel_state state, const any_message& message) {
        {
            const std::lock_guard lock(mutex_);
            session_ = id;
            state_ = state;
            messages_.push_back(message);
        }
        cv_.notify_all();
    }

    void record_vertical_message(session_id id, vertical_state state, const any_message& message) {
        {
            const std::lock_guard lock(mutex_);
            session_ = id;
            vertical_state_ = state;
            messages_.push_back(message);
        }
        cv_.notify_all();
    }

    void record_disconnected(session_id id, const error_info& error) {
        {
            const std::lock_guard lock(mutex_);
            session_ = id;
            disconnect_error_ = error;
            disconnected_ = true;
            state_ = channel_state::DISCONNECTED;
            vertical_state_ = vertical_state::DISCONNECTED;
        }
        cv_.notify_all();
    }

    void record_error(const error_info& error) {
        {
            const std::lock_guard lock(mutex_);
            error_ = error;
        }
        cv_.notify_all();
    }

    void record_trace(session_id, trace_type type, std::string_view text) {
        {
            const std::lock_guard lock(mutex_);
            traces_.push_back(trace_line { type, std::string { text } });
            if (traces_.size() > 512) { traces_.erase(traces_.begin()); }
        }
        cv_.notify_all();
    }

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    channel_state state_ { channel_state::NOT_CONNECTED };
    vertical_state vertical_state_ { vertical_state::NOT_CONNECTED };
    std::vector<channel_state> states_;
    std::vector<vertical_state> vertical_states_;
    std::vector<any_message> messages_;
    std::vector<trace_line> traces_;
    session_id session_ {};
    connection_info info_;
    error_info error_;
    error_info disconnect_error_;
    bool connected_ { false };
    bool disconnected_ { false };
    std::size_t connections_ { 0 };
};

///
/// @brief Waits until a server facade reports the port it bound, without polling.
///
template<class Facade>
std::uint16_t wait_port(Facade& facade, std::chrono::milliseconds timeout = 3s) {
    struct port_wait_state {
        std::mutex mutex_;
        std::condition_variable cv_;
        std::uint16_t port_ { 0 };
    };

    // The listener callback runs on the facade's I/O thread and may outlive the
    // wait itself; keep its state alive instead of capturing stack locals.
    const auto state = std::make_shared<port_wait_state>();

    facade.on_listening([state](std::uint16_t value) {
        {
            const std::lock_guard lock(state->mutex_);
            state->port_ = value;
        }
        state->cv_.notify_all();
    });

    std::unique_lock lock(state->mutex_);
    state->cv_.wait_for(lock, timeout, [&] { return state->port_ != 0; });
    const auto result = state->port_;
    lock.unlock();
    facade.on_listening({});
    return result;
}

///
/// @brief The settings of one end of a loopback pair with short timings, so a test does not wait
/// a production timeout.
///
inline downstream_settings make_downstream_settings(std::string machine_id = "UpstreamMachine") {
    downstream_settings settings;
    settings.machine_id_ = std::move(machine_id);
    settings.port_ = 0;
    settings.features_.check_alive_response_ = true;
    settings.check_alive_interval_ = 150ms;
    settings.check_alive_timeout_ = 600ms;
    return settings;
}

inline upstream_settings make_upstream_settings(std::string machine_id, std::uint16_t port) {
    upstream_settings settings;
    settings.machine_id_ = std::move(machine_id);
    settings.address_ = "127.0.0.1";
    settings.port_ = port;
    settings.features_.check_alive_response_ = true;
    settings.check_alive_interval_ = 150ms;
    settings.check_alive_timeout_ = 600ms;
    settings.reconnect_delay_ = 50ms;
    return settings;
}

} // namespace hermes_test
