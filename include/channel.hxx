// SPDX-License-Identifier: MIT
///
/// @file channel.hxx
/// @author BA7LYA (1042140025@qq.com)
/// @brief The callback types every facade reports through.
/// @version 0.1
/// @date 2026-10-18
/// @copyright Copyright (c) 2026
///
/// The callbacks are invoked on the I/O thread of the facade they are registered with. They may
/// call back into the same facade (send, post, ...) without deadlocking; they must not block.
///

#pragma once

#include <cstdint>
#include <functional>
#include <string_view>

#include "enums.hxx"
#include "error.hxx"
#include "messages.hxx"
#include "types.hxx"

namespace ba7lya::hermes {

/// @brief Called once a session's socket is up.
using connected_handler = std::function<void(session_id, const connection_info&)>;

/// @brief Called once a session's socket is down; the error_info carries the reason.
using disconnected_handler = std::function<void(session_id, const error_info&)>;

/// @brief Called whenever a horizontal session changes its state.
using state_handler = std::function<void(session_id, channel_state)>;

/// @brief Called whenever a vertical session changes its state.
using vertical_state_handler = std::function<void(session_id, vertical_state)>;

/// @brief Called for every message a horizontal session accepted; state is the state after it.
using message_handler = std::function<void(session_id, channel_state, const any_message&)>;

/// @brief Called for every message a vertical session accepted; state is the state after it.
using vertical_message_handler = std::function<void(session_id, vertical_state, const any_message&)>;

/// @brief Called for every raw document exchanged and for the library's own diagnostics.
using trace_handler = std::function<void(session_id, trace_type, std::string_view)>;

/// @brief Called once a server facade has bound its listening port.
using listening_handler = std::function<void(std::uint16_t)>;

/// @brief Called for errors that do not belong to one session (bind, resolve, ...).
using error_handler = std::function<void(const error_info&)>;

} // namespace ba7lya::hermes
