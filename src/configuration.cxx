// SPDX-License-Identifier: MIT
///
/// @file configuration.cxx
/// @author BA7LYA (1042140025@qq.com)
/// @brief The configuration service session.
/// @version 0.1
/// @date 2026-10-18
/// @copyright Copyright (c) 2026
///

#include "configuration.hxx"

#include <asio.hpp>
#include <string>
#include <utility>

#include "error.hxx"
#include "xml.hxx"

namespace ba7lya::hermes::detail {

namespace {

template<class F>
void guarded(F&& callback) noexcept {
    try {
        callback();
    }
    catch (...) {
    }
}

} // namespace

configuration_session::configuration_session(
    asio::io_context& context,
    session_id id,
    connection_info info,
    handlers callbacks
)
    : context_(context)
    , id_(id)
    , info_(std::move(info))
    , handlers_(std::move(callbacks)) {}

configuration_session::~configuration_session() = default;

std::shared_ptr<connection> configuration_session::connection_now() const {
    const std::lock_guard lock(mutex_);
    return connection_;
}

void configuration_session::attach(const std::shared_ptr<connection>& socket) {
    {
        const std::lock_guard lock(mutex_);
        connection_ = socket;
    }

    const auto weak = weak_from_this();
    socket->start(
        [weak](std::string document) {
            if (const auto self = weak.lock()) { self->on_frame(std::move(document)); }
        },
        [weak](const error_info& error) {
            if (const auto self = weak.lock()) { self->on_closed(error); }
        }
    );

    if (handlers_.connected_) {
        guarded([&] { handlers_.connected_(id_, info_); });
    }
}

void configuration_session::trace(trace_type type, std::string_view text) const noexcept {
    if (!handlers_.trace_) { return; }
    guarded([&] { handlers_.trace_(id_, type, text); });
}

void configuration_session::send(const any_message& message) {
    if (closed_.load(std::memory_order_relaxed) || closing_.load(std::memory_order_relaxed)) { return; }

    const auto socket = connection_now();
    if (!socket) { return; }

    const auto document = to_xml(envelope { timestamp_now(), message });
    trace(trace_type::SENT, document);
    socket->send(document);
}

void configuration_session::notify_error(const notification& notification, bool close_after) {
    const auto document = to_xml(envelope { timestamp_now(), any_message { notification } });
    trace(trace_type::SENT, document);

    if (close_after) {
        closing_ = true;
        if (const auto socket = connection_now()) {
            socket->send_and_close(document, error_info { error_code::PEER, notification.description_ });
            return;
        }
        on_closed(error_info { error_code::PEER, notification.description_ });
        return;
    }

    if (const auto socket = connection_now()) { socket->send(document); }
}

void configuration_session::on_frame(std::string document) {
    if (closed_.load(std::memory_order_relaxed)) { return; }

    trace(trace_type::RECEIVED, document);

    auto envelope = parse(document);
    if (!envelope) {
        notify_error(
            notification { notification_code::CONFIGURATION_ERROR, severity::FAILURE, envelope.error().text_ },
            true
        );
        return;
    }

    handle(envelope->message_);
}

void configuration_session::handle(const any_message& message) {
    if (const auto* set = std::get_if<set_configuration>(&message)) {
        handle_set(*set);
        return;
    }
    if (std::holds_alternative<get_configuration>(message)) {
        handle_get();
        return;
    }
    if (std::holds_alternative<notification>(message)) { return; }

    notify_error(
        notification { notification_code::CONFIGURATION_ERROR,
                       severity::FAILURE,
                       "the message " + std::string { message_name_of(message) } + " is not a configuration message" },
        true
    );
}

void configuration_session::handle_set(const set_configuration& value) {
    error_info error;
    if (handlers_.set_) {
        try {
            error = handlers_.set_(id_, info_, value);
        }
        catch (...) {
            error = error_info { error_code::IMPLEMENTATION, "the SetConfiguration handler threw" };
        }
    }
    else { error = error_info { error_code::IMPLEMENTATION, "the configuration service does not accept writes" }; }

    if (!error) { return; }

    notify_error(
        notification { notification_code::CONFIGURATION_ERROR,
                       severity::FAILURE,
                       error.text_.empty() ? std::string { "configuration rejected" } : error.text_ },
        false
    );
}

void configuration_session::handle_get() {
    current_configuration value;
    if (handlers_.get_) {
        try {
            value = handlers_.get_(id_, info_);
        }
        catch (...) {
            value = current_configuration {};
        }
    }
    send(any_message { std::move(value) });
}

void configuration_session::shutdown() {
    closing_ = true;
    const auto self = shared_from_this();
    asio::post(context_, [self]() {
        if (self->closed_.load(std::memory_order_relaxed)) { return; }
        if (const auto socket = self->connection_now()) {
            socket->close(error_info {});
            return;
        }
        self->on_closed(error_info {});
    });
}

void configuration_session::close(const error_info& reason) {
    closing_ = true;
    if (const auto socket = connection_now()) {
        socket->close(reason);
        return;
    }
    on_closed(reason);
}

void configuration_session::on_closed(const error_info& error) {
    if (closed_.exchange(true)) { return; }

    closing_ = true;
    if (handlers_.disconnected_) {
        guarded([&] { handlers_.disconnected_(id_, error); });
    }
}

} // namespace ba7lya::hermes::detail
