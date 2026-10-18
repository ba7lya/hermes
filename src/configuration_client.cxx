// SPDX-License-Identifier: MIT
///
/// @file configuration_client.cxx
/// @author BA7LYA (1042140025@qq.com)
/// @brief The client side of the configuration service.
/// @version 0.1
/// @date 2026-10-18
/// @copyright Copyright (c) 2026
///

#include "configuration_client.hxx"

#include <array>
#include <asio.hpp>
#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "error.hxx"
#include "expected.hxx"
#include "framing.hxx"
#include "xml.hxx"

namespace ba7lya::hermes {

namespace {

using tcp = asio::ip::tcp;
using namespace std::chrono_literals;

error_info timeout_error(std::string_view what) {
    return error_info { error_code::TIMEOUT, std::string { what } + " timed out" };
}

std::chrono::milliseconds remaining(std::chrono::steady_clock::time_point deadline) {
    const auto left
        = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
    return left.count() > 0 ? left : 1ms;
}

///
/// @brief One short lived configuration connection, driven synchronously by the free functions.
///
class exchange {
public:
    exchange()
        : socket_(context_)
        , timer_(context_) {}

    std::expected<void, error_info> connect(
        std::string_view host,
        std::uint16_t port,
        std::chrono::milliseconds timeout
    ) {
        tcp::resolver resolver(context_);
        asio::error_code error;
        const auto endpoints = resolver.resolve(std::string { host }, std::to_string(port), error);
        if (error) {
            return std::unexpected(
                error_info { error_code::NETWORK, "resolve " + std::string { host } + ": " + error.message() }
            );
        }

        context_.restart();
        bool finished = false;
        error_info failure;

        timer_.expires_after(timeout);
        timer_.async_wait([&](const asio::error_code& timer_error) {
            if (timer_error || finished) { return; }
            failure = timeout_error("connect");
            asio::error_code ignored;
            socket_.close(ignored);
        });

        asio::async_connect(socket_, endpoints, [&](const asio::error_code& connect_error, const tcp::endpoint&) {
            finished = true;
            if (connect_error && !failure) {
                failure = error_info { error_code::NETWORK, "connect: " + connect_error.message() };
            }
            asio::error_code ignored;
            timer_.cancel(ignored);
        });

        context_.run();

        if (!finished && !failure) { failure = timeout_error("connect"); }
        if (failure) { return std::unexpected(failure); }
        return {};
    }

    std::expected<void, error_info> write(std::string_view document, std::chrono::milliseconds timeout) {
        context_.restart();
        bool finished = false;
        error_info failure;

        timer_.expires_after(timeout);
        timer_.async_wait([&](const asio::error_code& timer_error) {
            if (timer_error || finished) { return; }
            failure = timeout_error("write");
            asio::error_code ignored;
            socket_.close(ignored);
        });

        asio::async_write(socket_, asio::buffer(document), [&](const asio::error_code& write_error, std::size_t) {
            finished = true;
            if (write_error && !failure) {
                failure = error_info { error_code::NETWORK, "write: " + write_error.message() };
            }
            asio::error_code ignored;
            timer_.cancel(ignored);
        });

        context_.run();

        if (!finished && !failure) { failure = timeout_error("write"); }
        if (failure) { return std::unexpected(failure); }
        return {};
    }

    std::expected<std::string, error_info> read(std::chrono::milliseconds timeout) {
        if (!pending_.empty()) {
            auto document = std::move(pending_.front());
            pending_.pop_front();
            return document;
        }

        context_.restart();
        bool finished = false;
        error_info failure;

        timer_.expires_after(timeout);
        timer_.async_wait([&](const asio::error_code& timer_error) {
            if (timer_error || finished) { return; }
            failure = timeout_error("read");
            asio::error_code ignored;
            socket_.close(ignored);
        });

        std::function<void()> read_some = [&]() {
            socket_.async_read_some(asio::buffer(buffer_), [&](const asio::error_code& read_error, std::size_t count) {
                if (finished) { return; }
                if (read_error) {
                    if (!failure) { failure = error_info { error_code::NETWORK, "read: " + read_error.message() }; }
                    finished = true;
                    asio::error_code ignored;
                    timer_.cancel(ignored);
                    return;
                }

                error_info framing;
                std::vector<std::string> extracted;
                if (!extractor_.feed(std::string_view { buffer_.data(), count }, extracted, framing)) {
                    failure = framing;
                    finished = true;
                    asio::error_code ignored;
                    timer_.cancel(ignored);
                    return;
                }
                for (auto& document : extracted) { pending_.push_back(std::move(document)); }

                if (!pending_.empty()) {
                    finished = true;
                    asio::error_code ignored;
                    timer_.cancel(ignored);
                    return;
                }

                read_some();
            });
        };
        read_some();

        context_.run();

        if (!pending_.empty()) {
            auto document = std::move(pending_.front());
            pending_.pop_front();
            return document;
        }
        if (failure) { return std::unexpected(failure); }
        return std::unexpected(timeout_error("read"));
    }

private:
    asio::io_context context_;
    tcp::socket socket_;
    asio::steady_timer timer_;
    detail::frame_extractor extractor_;
    std::deque<std::string> pending_;
    std::array<char, 8192> buffer_ {};
};

} // namespace

std::expected<configuration_reply, error_info> read_configuration(
    std::string_view address,
    std::uint16_t port,
    std::chrono::milliseconds timeout
) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;

    exchange channel;
    if (auto result = channel.connect(address, port, remaining(deadline)); !result) {
        return std::unexpected(result.error());
    }

    const auto request = to_xml(envelope { timestamp_now(), any_message { get_configuration {} } });
    if (auto result = channel.write(request, remaining(deadline)); !result) { return std::unexpected(result.error()); }

    configuration_reply reply;
    while (std::chrono::steady_clock::now() < deadline) {
        auto document = channel.read(remaining(deadline));
        if (!document) { return std::unexpected(document.error()); }

        auto envelope = parse(*document);
        if (!envelope) { return std::unexpected(envelope.error()); }

        if (const auto* configuration = std::get_if<current_configuration>(&envelope->message_)) {
            reply.configuration_ = *configuration;
            return reply;
        }
        if (const auto* note = std::get_if<notification>(&envelope->message_)) {
            reply.notifications_.push_back(*note);
            if (note->notification_code_ == notification_code::CONFIGURATION_ERROR) {
                return std::unexpected(error_info { error_code::PEER, note->description_ });
            }
        }
    }

    // A service that did not send a current_configuration but a notification answered with a
    // refusal; without either it did not answer at all.
    if (!reply.notifications_.empty()) {
        return std::unexpected(error_info { error_code::PEER, reply.notifications_.back().description_ });
    }
    return std::unexpected(timeout_error("GetConfiguration"));
}

std::expected<configuration_reply, error_info> write_configuration(
    std::string_view address,
    const set_configuration& configuration,
    std::uint16_t port,
    std::chrono::milliseconds timeout
) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;

    exchange channel;
    if (auto result = channel.connect(address, port, remaining(deadline)); !result) {
        return std::unexpected(result.error());
    }

    const auto request = to_xml(envelope { timestamp_now(), any_message { configuration } });
    if (auto result = channel.write(request, remaining(deadline)); !result) { return std::unexpected(result.error()); }

    configuration_reply reply;
    while (std::chrono::steady_clock::now() < deadline) {
        auto document = channel.read(remaining(deadline));
        if (!document) {
            // The standard does not ask the service to answer a set_configuration it accepted;
            // a read timeout therefore reports success, unless a failure already showed up.
            if (document.error().code_ == error_code::TIMEOUT) { return reply; }
            return std::unexpected(document.error());
        }

        auto envelope = parse(*document);
        if (!envelope) { return std::unexpected(envelope.error()); }

        if (const auto* current = std::get_if<current_configuration>(&envelope->message_)) {
            reply.configuration_ = *current;
            return reply;
        }
        if (const auto* note = std::get_if<notification>(&envelope->message_)) {
            reply.notifications_.push_back(*note);
            return std::unexpected(error_info { error_code::PEER, note->description_ });
        }
    }

    return reply;
}

} // namespace ba7lya::hermes
