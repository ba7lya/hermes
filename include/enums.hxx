// SPDX-License-Identifier: MIT
///
/// @file enums.hxx
/// @author BA7LYA (1042140025@qq.com)
/// @brief The enumerations of The Hermes Standard and of the library itself.
/// @version 0.1
/// @date 2026-10-18
/// @copyright Copyright (c) 2026
///

#pragma once

#include <cstdint>
#include <ostream>
#include <string>
#include <string_view>

namespace ba7lya::hermes {

///
/// @brief The FailedBoard attribute of board_available, board_forecast, send_board_info and friends (3.6).
/// @note In machine_ready the same values describe the quality the downstream machine accepts:
/// 0 accepts any board, 1 only good boards, 2 only failed boards (3.8).
///
enum class board_quality : std::uint8_t {
    ANY = 0,   ///< Board of unknown quality / ready to accept any board.
    GOOD = 1,  ///< Good board / ready to accept good boards.
    FAILED = 2 ///< Failed board / ready to accept failed boards.
};

///
/// @brief The flipped_board attribute: which side of the board faces up (3.6).
///
enum class flipped_board : std::uint8_t {
    UNKNOWN = 0,       ///< Side up is unknown.
    TOP_SIDE_UP = 1,   ///< The board top side is up.
    BOTTOM_SIDE_UP = 2 ///< The board bottom side is up.
};

///
/// @brief The St attribute of a sub_board (3.6).
///
enum class sub_board_state : std::uint8_t {
    UNKNOWN = 0, ///< Subboard of unknown quality.
    GOOD = 1,    ///< Good subboard.
    FAILED = 2,  ///< Failed subboard.
    MISSING = 3, ///< Missing subboard.
    SKIP = 4     ///< Skip subboard.
};

///
/// @brief The transfer_state of stop_transport and transport_finished (3.11).
///
enum class transfer_state : std::uint8_t {
    NOT_STARTED = 1, ///< The PCB never left the upstream machine.
    INCOMPLETE = 2,  ///< The transfer was cancelled in progress.
    COMPLETE = 3     ///< The transfer ended successfully.
};

///
/// @brief The BoardTransfer attribute of board_arrived (3.20).
///
enum class arrival_transfer : std::uint8_t {
    TRANSFERRED = 1, ///< Board arrived from the upstream machine via Hermes or SMEMA.
    LOADED = 2,      ///< Board was loaded from a magazine or a stack of boards.
    INSERTED = 3     ///< Board was manually inserted into the machine.
};

///
/// @brief The BoardTransfer attribute of board_departed (3.21).
///
enum class departure_transfer : std::uint8_t {
    TRANSFERRED = 1, ///< Board moved to the downstream machine via Hermes or SMEMA.
    UNLOADED = 2,    ///< Board was unloaded into a magazine.
    REMOVED = 3      ///< Board was manually taken out of the machine.
};

///
/// @brief The Status of reply_work_order_info (3.24).
///
enum class reply_work_order_status : std::uint8_t {
    REJECTED = 0,           ///< Work order rejected.
    ACCEPTED_AND_READY = 1, ///< Work order accepted and ready for execution.
    ACCEPTED_AND_QUEUED = 2 ///< Work order accepted and queued for execution.
};

///
/// @brief The severity of a notification (3.5).
///
enum class severity : std::uint8_t {
    FATAL = 1,   ///< Fatal error.
    FAILURE = 2, ///< Failure.
    WARNING = 3, ///< Warning.
    INFO = 4     ///< Info.
};

///
/// @brief The notification_code of a notification (3.5).
/// @note Codes above 1000 are not defined by the standard and may be used by the application.
///
enum class notification_code : std::uint16_t {
    UNSPECIFIED = 0,         ///< No specific reason; an implementation value.
    PROTOCOL_ERROR = 1,      ///< Invalid transition in the corresponding state machine.
    CONNECTION_REFUSED = 2,  ///< Connection refused because of an established connection.
    CONNECTION_RESET = 3,    ///< Connection reset because of changed configuration.
    CONFIGURATION_ERROR = 4, ///< Configuration error.
    MACHINE_SHUTDOWN = 5,    ///< Machine shutdown.
    BOARD_FORECAST_ERROR = 6 ///< board_forecast error.
};

///
/// @brief The command attribute of a command message (3.25).
/// @note Values from 1000 on are customer defined.
///
enum class command_code : std::uint16_t {
    UNDEFINED = 0,             ///< command undefined; discard it.
    LOCK_INPUT_CONVEYOR = 1,   ///< Lock the input conveyor.
    UNLOCK_INPUT_CONVEYOR = 2, ///< Unlock the input conveyor.
    REQUEST_PAUSE = 3,         ///< Request pause.
    CONFIRM_PAUSE = 4,         ///< Confirm pause.
    RESUME_OPERATION = 5       ///< Resume operation.
};

///
/// @brief The Type attribute of check_alive (3.3).
///
enum class check_alive_type : std::uint8_t {
    PING = 1, ///< check_alive request.
    PONG = 2  ///< check_alive response.
};

///
/// @brief The state of one horizontal interface, as Figure 19 of the standard defines it (2.3.6).
/// @note The state describes the interface as a whole, not one of the two machines.
///
enum class channel_state : std::uint8_t {
    NOT_CONNECTED,                  ///< No socket.
    SOCKET_CONNECTED,               ///< The socket is up, the handshake has not started.
    SERVICE_DESCRIPTION_DOWNSTREAM, ///< The downstream service_description arrived.
    NOT_AVAILABLE_NOT_READY,        ///< The handshake is done; no board offered, none accepted.
    BOARD_AVAILABLE,                ///< The upstream machine offers a board.
    MACHINE_READY,                  ///< The downstream machine accepts a board.
    AVAILABLE_AND_READY,            ///< Board offered and accepted; a transport may start.
    TRANSPORTING,                   ///< The board is being transferred.
    TRANSPORT_STOPPED,              ///< The upstream machine finished the transport.
    TRANSPORT_FINISHED,             ///< The downstream machine finished the transport.
    DISCONNECTED                    ///< The socket is gone.
};

///
/// @brief The state of one vertical interface, as Figure 22 of the standard defines it (2.5.3).
///
enum class vertical_state : std::uint8_t {
    NOT_CONNECTED,                   ///< No socket.
    SOCKET_CONNECTED,                ///< The socket is up, the handshake has not started.
    SUPERVISORY_SERVICE_DESCRIPTION, ///< The supervisory system's description arrived.
    CONNECTED,                       ///< The handshake is done; the features may be used.
    DISCONNECTED                     ///< The socket is gone.
};

///
/// @brief The kind of line the library reports through its trace callback.
///
enum class trace_type : std::uint8_t {
    SENT,     ///< A raw document sent to the peer.
    RECEIVED, ///< A raw document received from the peer.
    DEBUG,    ///< A diagnostic message.
    INFO,     ///< An informational message.
    WARNING,  ///< Something unexpected but tolerated.
    FAILURE   ///< Something that made a session fail.
};

///
/// @brief Whether the state machine checks the messages the application sends as well (3.6).
///
enum class state_check : std::uint8_t {
    ALL,          ///< Check the documents sent and the documents received.
    RECEIVED_ONLY ///< Check only the documents received; trust the application otherwise.
};

///
/// @brief The name of @p value as the standard spells it.
///
inline std::string_view to_string(board_quality value) {
    switch (value) {
    case board_quality::ANY:    return "any";
    case board_quality::GOOD:   return "good";
    case board_quality::FAILED: return "failed";
    }
    return "unknown";
}

/// @brief The name of @p value as the standard spells it.
inline std::string_view to_string(flipped_board value) {
    switch (value) {
    case flipped_board::UNKNOWN:        return "unknown";
    case flipped_board::TOP_SIDE_UP:    return "top_side_up";
    case flipped_board::BOTTOM_SIDE_UP: return "bottom_side_up";
    }
    return "unknown";
}

/// @brief The name of @p value as the standard spells it.
inline std::string_view to_string(sub_board_state value) {
    switch (value) {
    case sub_board_state::UNKNOWN: return "unknown";
    case sub_board_state::GOOD:    return "good";
    case sub_board_state::FAILED:  return "failed";
    case sub_board_state::MISSING: return "missing";
    case sub_board_state::SKIP:    return "skip";
    }
    return "unknown";
}

/// @brief The name of @p value as the standard spells it.
inline std::string_view to_string(transfer_state value) {
    switch (value) {
    case transfer_state::NOT_STARTED: return "not_started";
    case transfer_state::INCOMPLETE:  return "incomplete";
    case transfer_state::COMPLETE:    return "complete";
    }
    return "unknown";
}

/// @brief The name of @p value as the standard spells it.
inline std::string_view to_string(arrival_transfer value) {
    switch (value) {
    case arrival_transfer::TRANSFERRED: return "transferred";
    case arrival_transfer::LOADED:      return "loaded";
    case arrival_transfer::INSERTED:    return "inserted";
    }
    return "unknown";
}

/// @brief The name of @p value as the standard spells it.
inline std::string_view to_string(departure_transfer value) {
    switch (value) {
    case departure_transfer::TRANSFERRED: return "transferred";
    case departure_transfer::UNLOADED:    return "unloaded";
    case departure_transfer::REMOVED:     return "removed";
    }
    return "unknown";
}

/// @brief The name of @p value as the standard spells it.
inline std::string_view to_string(reply_work_order_status value) {
    switch (value) {
    case reply_work_order_status::REJECTED:            return "rejected";
    case reply_work_order_status::ACCEPTED_AND_READY:  return "accepted_and_ready";
    case reply_work_order_status::ACCEPTED_AND_QUEUED: return "accepted_and_queued";
    }
    return "unknown";
}

/// @brief The name of @p value as the standard spells it.
inline std::string_view to_string(severity value) {
    switch (value) {
    case severity::FATAL:   return "fatal";
    case severity::FAILURE: return "error";
    case severity::WARNING: return "warning";
    case severity::INFO:    return "info";
    }
    return "unknown";
}

/// @brief The name of @p value as the standard spells it.
inline std::string_view to_string(notification_code value) {
    switch (value) {
    case notification_code::UNSPECIFIED:          return "unspecified";
    case notification_code::PROTOCOL_ERROR:       return "protocol_error";
    case notification_code::CONNECTION_REFUSED:   return "connection_refused";
    case notification_code::CONNECTION_RESET:     return "connection_reset";
    case notification_code::CONFIGURATION_ERROR:  return "configuration_error";
    case notification_code::MACHINE_SHUTDOWN:     return "machine_shutdown";
    case notification_code::BOARD_FORECAST_ERROR: return "board_forecast_error";
    }
    return "custom";
}

/// @brief The name of @p value as the standard spells it.
inline std::string_view to_string(command_code value) {
    switch (value) {
    case command_code::UNDEFINED:             return "undefined";
    case command_code::LOCK_INPUT_CONVEYOR:   return "lock_input_conveyor";
    case command_code::UNLOCK_INPUT_CONVEYOR: return "unlock_input_conveyor";
    case command_code::REQUEST_PAUSE:         return "request_pause";
    case command_code::CONFIRM_PAUSE:         return "confirm_pause";
    case command_code::RESUME_OPERATION:      return "resume_operation";
    }
    return "custom";
}

/// @brief The name of @p value as the standard spells it.
inline std::string_view to_string(check_alive_type value) {
    switch (value) {
    case check_alive_type::PING: return "ping";
    case check_alive_type::PONG: return "pong";
    }
    return "unknown";
}

/// @brief The name of @p value as the state chart spells it.
inline std::string_view to_string(channel_state value) {
    switch (value) {
    case channel_state::NOT_CONNECTED:                  return "not_connected";
    case channel_state::SOCKET_CONNECTED:               return "socket_connected";
    case channel_state::SERVICE_DESCRIPTION_DOWNSTREAM: return "service_description_downstream";
    case channel_state::NOT_AVAILABLE_NOT_READY:        return "not_available_not_ready";
    case channel_state::BOARD_AVAILABLE:                return "BoardAvailable";
    case channel_state::MACHINE_READY:                  return "MachineReady";
    case channel_state::AVAILABLE_AND_READY:            return "available_and_ready";
    case channel_state::TRANSPORTING:                   return "transporting";
    case channel_state::TRANSPORT_STOPPED:              return "transport_stopped";
    case channel_state::TRANSPORT_FINISHED:             return "TransportFinished";
    case channel_state::DISCONNECTED:                   return "disconnected";
    }
    return "unknown";
}

/// @brief The name of @p value as the state chart spells it.
inline std::string_view to_string(vertical_state value) {
    switch (value) {
    case vertical_state::NOT_CONNECTED:                   return "not_connected";
    case vertical_state::SOCKET_CONNECTED:                return "socket_connected";
    case vertical_state::SUPERVISORY_SERVICE_DESCRIPTION: return "SupervisoryServiceDescription";
    case vertical_state::CONNECTED:                       return "connected";
    case vertical_state::DISCONNECTED:                    return "disconnected";
    }
    return "unknown";
}

/// @brief The name of @p value as the library spells it.
inline std::string_view to_string(trace_type value) {
    switch (value) {
    case trace_type::SENT:     return "sent";
    case trace_type::RECEIVED: return "received";
    case trace_type::DEBUG:    return "debug";
    case trace_type::INFO:     return "info";
    case trace_type::WARNING:  return "warning";
    case trace_type::FAILURE:  return "error";
    }
    return "unknown";
}

/// @brief The name of @p value as the library spells it.
inline std::string_view to_string(state_check value) {
    switch (value) {
    case state_check::ALL:           return "all";
    case state_check::RECEIVED_ONLY: return "received_only";
    }
    return "unknown";
}

/// @brief Streams the standard name of @p value.
template<class E>
    requires requires(E value) { to_string(value); }
std::ostream& operator<<(std::ostream& stream, E value) {
    return stream << to_string(value);
}

} // namespace ba7lya::hermes
