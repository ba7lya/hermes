// SPDX-License-Identifier: MIT
///
/// @file net.hxx
/// @author BA7LYA (1042140025@qq.com)
/// @brief The asio building blocks the facades are assembled from.
/// @version 0.1
/// @date 2026-10-18
/// @copyright Copyright (c) 2026
///

#pragma once

#include <array>
#include <asio.hpp>
#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

#include "error.hxx"
#include "expected.hxx"
#include "framing.hxx"
#include "types.hxx"

namespace ba7lya::hermes::detail {

///
/// @brief Owns one asio io_context and the thread it runs on.
///
class io_runner {
public:
    io_runner();
    ~io_runner();

    io_runner(const io_runner&) = delete;
    io_runner& operator=(const io_runner&) = delete;

    /// @brief Starts the I/O thread.
    void start();

    /// @brief Lets the I/O thread return once the pending work is done, then joins it.
    void join();

    /// @brief Stops the I/O thread immediately and joins it.
    void halt();

    /// @brief Lets the I/O thread return once the pending work is done, without joining it.
    void retire();

    /// @brief Whether the calling thread is the I/O thread.
    bool on_thread() const noexcept;

    asio::io_context& context() noexcept { return context_; }

private:
    using work_guard = asio::executor_work_guard<asio::io_context::executor_type>;

    void run();

    asio::io_context context_;
    std::optional<work_guard> guard_;
    std::thread thread_;
    std::thread::id thread_id_;
};

///
/// @brief One connected TCP socket with a read loop, a write queue and document framing.
///
/// Every method runs on the I/O thread; the socket owns itself through shared_from_this while an
/// operation is outstanding, so a session may drop its reference at any time.
///
class connection : public std::enable_shared_from_this<connection> {
public:
    using frame_handler = std::function<void(std::string)>;
    using close_handler = std::function<void(const error_info&)>;

    connection(asio::ip::tcp::socket socket, connection_info info);

    /// @brief Starts reading; the handlers are invoked on the I/O thread.
    void start(frame_handler on_frame, close_handler on_close);

    /// @brief Queues @p document; writes are serialized in the order they were queued.
    void send(std::string_view document);

    /// @brief Queues @p document and closes the socket once it is written.
    void send_and_close(std::string_view document, const error_info& reason);

    /// @brief Closes the socket; a pending write is dropped.
    void close(const error_info& reason);

    bool closed() const noexcept { return closed_; }

    const connection_info& info() const noexcept { return info_; }

private:
    void read();
    void on_read(const asio::error_code& error, std::size_t count);
    void write_next();
    void on_written(const asio::error_code& error, std::size_t count);
    void finish(const error_info& reason);

    asio::ip::tcp::socket socket_;
    connection_info info_;
    frame_extractor extractor_;
    frame_handler on_frame_;
    close_handler on_close_;
    std::deque<std::string> queue_;
    std::array<char, 16384> read_buffer_ {};
    bool writing_ { false };
    bool closed_ { false };
    bool close_notified_ { false };
    bool close_after_write_ { false };
    error_info close_reason_;
};

///
/// @brief A TCP client that resolves, connects and retries cyclically.
///
class connector : public std::enable_shared_from_this<connector> {
public:
    using connect_handler = std::function<void(asio::ip::tcp::socket, connection_info)>;
    using failure_handler = std::function<void(const error_info&)>;

    connector(asio::io_context& context, std::string host, std::uint16_t port, std::chrono::milliseconds retry_delay);

    /// @brief Starts the connect attempts; may be called again after a connection was lost.
    void start(connect_handler on_connect, failure_handler on_failure);
    void stop();

private:
    void attempt();
    void retry(const error_info& error);
    void close_socket();

    asio::io_context& context_;
    asio::steady_timer timer_;
    asio::ip::tcp::resolver resolver_;
    std::string host_;
    std::uint16_t port_;
    std::chrono::milliseconds retry_delay_;
    connect_handler on_connect_;
    failure_handler on_failure_;
    std::shared_ptr<asio::ip::tcp::socket> socket_;
    bool stopped_ { true };
};

///
/// @brief A TCP server that accepts connections on one local endpoint.
///
class acceptor : public std::enable_shared_from_this<acceptor> {
public:
    using accept_handler = std::function<void(asio::ip::tcp::socket, connection_info)>;
    using error_handler = std::function<void(const error_info&)>;

    acceptor(asio::io_context& context, std::string address, std::uint16_t port);

    /// @brief Opens, binds and listens; runs synchronously on the I/O thread.
    std::expected<void, error_info> listen();

    /// @brief Starts accepting; the handler is invoked for every new connection.
    void start(accept_handler on_accept, error_handler on_error);

    /// @brief Stops accepting and closes the listening socket.
    void stop();

    /// @brief The port actually bound, useful when port 0 was requested.
    std::uint16_t port() const;

private:
    void do_accept();

    asio::io_context& context_;
    asio::ip::tcp::acceptor acceptor_;
    std::string address_;
    std::uint16_t port_;
    accept_handler on_accept_;
    error_handler on_error_;
    bool stopped_ { true };
};

/// @brief Resolves @p address and @p port into a local endpoint.
std::expected<asio::ip::tcp::endpoint, error_info> resolve_local(const std::string& address, std::uint16_t port);

} // namespace ba7lya::hermes::detail
