// SPDX-License-Identifier: MIT
///
/// @file net.cxx
/// @author BA7LYA (1042140025@qq.com)
/// @brief The asio implementation of the transport.
/// @version 0.1
/// @date 2026-10-18
/// @copyright Copyright (c) 2026
///

#include "net.hxx"

#include <asio.hpp>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "error.hxx"

namespace ba7lya::hermes::detail {

namespace {

using tcp = asio::ip::tcp;

error_info network_error(std::string_view what, const asio::error_code& error) {
    return error_info { error_code::NETWORK, std::string { what } + ": " + error.message() };
}

} // namespace

//==============================================================================
// io_runner
//==============================================================================

io_runner::io_runner() = default;

io_runner::~io_runner() { halt(); }

void io_runner::run() {
    try {
        context_.run();
    }
    catch (...) {
        // A handler must not take the process down; the library guards its callbacks, so
        // anything left here is a library bug that a throwing handler would otherwise hide.
    }
}

void io_runner::start() {
    if (thread_.joinable()) { return; }
    context_.restart();
    guard_.emplace(context_.get_executor());
    // A member-function pointer avoids the single-argument tuple constructor that Clang 17/18
    // cannot compile against the libstdc++ that ships with GCC 14.
    thread_ = std::thread(&io_runner::run, this);
    // Published by the creating thread: on_thread() may be called while the I/O thread runs.
    thread_id_ = thread_.get_id();
}

void io_runner::join() {
    guard_.reset();
    if (thread_.joinable()) { thread_.join(); }
}

void io_runner::halt() {
    guard_.reset();
    context_.stop();
    if (thread_.joinable()) { thread_.join(); }
}

void io_runner::retire() { guard_.reset(); }

bool io_runner::on_thread() const noexcept {
    return thread_id_ != std::thread::id {} && thread_id_ == std::this_thread::get_id();
}

//==============================================================================
// connection
//==============================================================================

connection::connection(tcp::socket socket, connection_info info)
    : socket_(std::move(socket))
    , info_(std::move(info)) {}

void connection::start(frame_handler on_frame, close_handler on_close) {
    on_frame_ = std::move(on_frame);
    on_close_ = std::move(on_close);

    asio::error_code error;
    socket_.set_option(tcp::no_delay(true), error);
    read();
}

void connection::send(std::string_view document) {
    if (closed_) { return; }

    queue_.emplace_back(document);
    if (!writing_) { write_next(); }
}

void connection::send_and_close(std::string_view document, const error_info& reason) {
    if (closed_) { return; }

    close_after_write_ = true;
    close_reason_ = reason;
    queue_.emplace_back(document);
    if (!writing_) { write_next(); }
}

void connection::close(const error_info& reason) {
    if (closed_) { return; }

    closed_ = true;
    // A pending async_write still references the front of the queue, so the strings may only be
    // destroyed once that operation completed (on_written) or when no write is in flight at all.
    // Destroying them here would leave asio with a dangling buffer.
    if (!writing_) { queue_.clear(); }

    asio::error_code ignored;
    socket_.shutdown(tcp::socket::shutdown_both, ignored);
    socket_.close(ignored);

    if (!close_notified_) {
        close_notified_ = true;
        if (on_close_) {
            auto callback = on_close_;
            callback(reason);
        }
    }
}

void connection::read() {
    if (closed_) { return; }

    auto self = shared_from_this();
    socket_.async_read_some(asio::buffer(read_buffer_), [self](const asio::error_code& error, std::size_t count) {
        self->on_read(error, count);
    });
}

void connection::on_read(const asio::error_code& error, std::size_t count) {
    if (closed_) { return; }

    if (error) {
        if (error == asio::error::operation_aborted) { return; }
        if (error == asio::error::eof || error == asio::error::connection_reset) {
            close(error_info { error_code::NETWORK, "the peer closed the connection" });
            return;
        }
        close(network_error("read", error));
        return;
    }

    std::vector<std::string> documents;
    error_info framing_error;
    if (!extractor_.feed(std::string_view { read_buffer_.data(), count }, documents, framing_error)) {
        close(framing_error);
        return;
    }

    for (auto& document : documents) {
        if (closed_) { return; }
        auto self = shared_from_this();
        on_frame_(std::move(document));
    }

    if (!closed_) { read(); }
}

void connection::write_next() {
    if (closed_) { return; }
    if (queue_.empty()) {
        writing_ = false;
        return;
    }

    writing_ = true;
    auto self = shared_from_this();
    asio::async_write(socket_, asio::buffer(queue_.front()), [self](const asio::error_code& error, std::size_t count) {
        self->on_written(error, count);
    });
}

void connection::on_written(const asio::error_code& error, std::size_t) {
    if (closed_) {
        // The write completed while the connection was being closed; the buffer is no longer
        // referenced, so the queue can be released now.
        writing_ = false;
        queue_.clear();
        return;
    }

    if (error) {
        close(network_error("write", error));
        return;
    }

    queue_.pop_front();

    if (queue_.empty()) {
        if (close_after_write_) {
            close(close_reason_);
            return;
        }
        writing_ = false;
        return;
    }

    write_next();
}

//==============================================================================
// connector
//==============================================================================

connector::connector(
    asio::io_context& context,
    std::string host,
    std::uint16_t port,
    std::chrono::milliseconds retry_delay
)
    : context_(context)
    , timer_(context)
    , resolver_(context)
    , host_(std::move(host))
    , port_(port)
    , retry_delay_(retry_delay) {}

void connector::start(connect_handler on_connect, failure_handler on_failure) {
    on_connect_ = std::move(on_connect);
    on_failure_ = std::move(on_failure);
    stopped_ = false;

    close_socket();
    attempt();
}

void connector::stop() {
    if (stopped_) { return; }

    stopped_ = true;
    asio::error_code ignored;
    timer_.cancel();
    resolver_.cancel();
    close_socket();
}

void connector::close_socket() {
    if (socket_) {
        asio::error_code ignored;
        socket_->close(ignored);
        socket_.reset();
    }
}

void connector::attempt() {
    if (stopped_) { return; }

    auto self = shared_from_this();
    resolver_.async_resolve(
        host_,
        std::to_string(port_),
        [self](const asio::error_code& error, const tcp::resolver::results_type& results) {
            if (self->stopped_) { return; }
            if (error) {
                self->retry(error_info { error_code::NETWORK, "resolve " + self->host_ + ": " + error.message() });
                return;
            }

            self->socket_ = std::make_shared<tcp::socket>(self->context_);
            auto socket = self->socket_;
            asio::async_connect(
                *socket,
                results,
                [self, socket](const asio::error_code& connect_error, const tcp::endpoint& endpoint) {
                    if (self->stopped_) {
                        self->close_socket();
                        return;
                    }
                    if (connect_error) {
                        self->retry(
                            error_info { error_code::NETWORK,
                                         "connect " + self->host_ + ": " + connect_error.message() }
                        );
                        return;
                    }

                    auto connected = std::move(*socket);
                    if (self->socket_ == socket) { self->socket_.reset(); }
                    connection_info info { endpoint.address().to_string(), endpoint.port() };
                    if (self->on_connect_) {
                        auto callback = self->on_connect_;
                        callback(std::move(connected), std::move(info));
                    }
                }
            );
        }
    );
}

void connector::retry(const error_info& error) {
    close_socket();
    if (on_failure_) { on_failure_(error); }
    if (stopped_) { return; }

    timer_.expires_after(retry_delay_);
    auto self = shared_from_this();
    timer_.async_wait([self](const asio::error_code& error) {
        if (error || self->stopped_) { return; }
        self->attempt();
    });
}

//==============================================================================
// acceptor
//==============================================================================

acceptor::acceptor(asio::io_context& context, std::string address, std::uint16_t port)
    : context_(context)
    , acceptor_(context)
    , address_(std::move(address))
    , port_(port) {}

std::expected<void, error_info> acceptor::listen() {
    const auto endpoint = resolve_local(address_, port_);
    if (!endpoint) { return std::unexpected(endpoint.error()); }

    asio::error_code error;
    acceptor_.open(endpoint->protocol(), error);
    if (error) { return std::unexpected(network_error("open", error)); }

    acceptor_.set_option(tcp::acceptor::reuse_address(true), error);
    if (error) { return std::unexpected(network_error("set_option", error)); }

    acceptor_.bind(*endpoint, error);
    if (error) { return std::unexpected(network_error("bind", error)); }

    acceptor_.listen(asio::socket_base::max_listen_connections, error);
    if (error) { return std::unexpected(network_error("listen", error)); }

    return {};
}

void acceptor::start(accept_handler on_accept, error_handler on_error) {
    on_accept_ = std::move(on_accept);
    on_error_ = std::move(on_error);
    if (!stopped_) { return; }

    stopped_ = false;
    do_accept();
}

void acceptor::stop() {
    if (stopped_) { return; }

    stopped_ = true;
    asio::error_code ignored;
    acceptor_.close(ignored);
}

void acceptor::do_accept() {
    if (stopped_) { return; }

    auto self = shared_from_this();
    acceptor_.async_accept([self](const asio::error_code& error, tcp::socket socket) {
        if (self->stopped_) { return; }

        if (error) {
            if (error != asio::error::operation_aborted) {
                if (self->on_error_) { self->on_error_(network_error("accept", error)); }
            }
            self->do_accept();
            return;
        }

        asio::error_code endpoint_error;
        const auto endpoint = socket.remote_endpoint(endpoint_error);
        connection_info info;
        if (!endpoint_error) {
            info.address_ = endpoint.address().to_string();
            info.port_ = endpoint.port();
        }

        if (self->on_accept_) { self->on_accept_(std::move(socket), std::move(info)); }
        self->do_accept();
    });
}

std::uint16_t acceptor::port() const {
    asio::error_code error;
    const auto endpoint = acceptor_.local_endpoint(error);
    return error ? 0 : endpoint.port();
}

//==============================================================================
// helpers
//==============================================================================

std::expected<tcp::endpoint, error_info> resolve_local(const std::string& address, std::uint16_t port) {
    asio::error_code error;
    const auto parsed = asio::ip::make_address(address.empty() ? "0.0.0.0" : address, error);
    if (!error) { return tcp::endpoint(parsed, port); }

    asio::io_context context;
    tcp::resolver resolver(context);
    const auto results = resolver.resolve(address.empty() ? "0.0.0.0" : address, std::to_string(port), error);
    if (error || results.empty()) {
        return std::unexpected(
            error_info { error_code::NETWORK,
                         "resolve " + address + ": " + (error ? error.message() : std::string { "no endpoint" }) }
        );
    }
    return *results.begin();
}

} // namespace ba7lya::hermes::detail
