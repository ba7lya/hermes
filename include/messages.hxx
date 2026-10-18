// SPDX-License-Identifier: MIT
///
/// @file messages.hxx
/// @author BA7LYA (1042140025@qq.com)
/// @brief The messages of The Hermes Standard, one plain data type per message.
/// @version 0.1
/// @date 2026-10-18
/// @copyright Copyright (c) 2026
///
/// Every message is a struct whose members map one to one onto the attributes and child elements
/// of the standard's message table: a mandatory field is a plain value, an optional field is a
/// std::optional that is left out of the document when it is empty. The type name is the element
/// name on the wire; the field names of the standard are restored by the codec in xml.hxx, so a
/// member may be spelled in the library's own snake_case.
///

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

#include "enums.hxx"
#include "types.hxx"
#include "version.hxx"

namespace ba7lya::hermes {

//==============================================================================
// 3.3 check_alive
//==============================================================================

///
/// @brief The check_alive message: a ping or pong used to detect connection loss (3.3).
///
struct check_alive {
    std::optional<check_alive_type> type_; ///< Ping or pong; a receiver ignores a message without one.
    std::optional<std::string> id_;        ///< A unique identifier; a pong echoes the ping's id.

    bool operator==(const check_alive&) const = default;
};

//==============================================================================
// 3.4 service_description
//==============================================================================

///
/// @brief The service_description message: the handshake of the horizontal channel (3.4).
///
struct service_description {
    std::string machine_id_;                   ///< The id of the sending machine.
    std::uint16_t lane_id_ {};                 ///< The lane this connection relates to.
    std::optional<std::string> interface_id_;  ///< The transport interface this connection relates to.
    std::string version_ { PROTOCOL_VERSION }; ///< The implemented interface version, e.g. "1.6".
    supported_features supported_features_;    ///< The optional features the machine implements.

    bool operator==(const service_description&) const = default;
};

//==============================================================================
// 3.5 notification
//==============================================================================

///
/// @brief The notification message: the reason a connection is closed or a general report (3.5).
///
struct notification {
    notification_code notification_code_ { notification_code::UNSPECIFIED }; ///< The machine readable code.
    severity severity_ { severity::FAILURE };                                ///< How severe the report is.
    std::string description_;                                                ///< An English description.

    bool operator==(const notification&) const = default;
};

/// @brief Builds a notification with the arguments in the order of the standard's table.
inline notification make_notification(notification_code code, severity severity, std::string description) {
    return notification { code, severity, std::move(description) };
}

//==============================================================================
// 3.6 board_available
//==============================================================================

///
/// @brief The board_available message: the upstream machine offers a board (3.6).
///
struct board_available {
    std::string board_id_;                                   ///< The GUID of the offered board.
    std::string board_id_created_by_;                        ///< The machine that created the board id.
    board_quality failed_board_ { board_quality::ANY };      ///< The quality of the board.
    std::optional<std::string> product_type_id_;             ///< The product type the board belongs to.
    flipped_board flipped_board_ { flipped_board::UNKNOWN }; ///< Which side of the board is up.
    std::optional<std::string> top_barcode_;                 ///< The barcode of the top side.
    std::optional<std::string> bottom_barcode_;              ///< The barcode of the bottom side.
    std::optional<double> length_;                           ///< The length of the board in mm.
    std::optional<double> width_;                            ///< The width of the board in mm.
    std::optional<double> thickness_;                        ///< The thickness of the board in mm.
    std::optional<double> conveyor_speed_;                   ///< The preferred conveyor speed in mm/s.
    std::optional<double> top_clearance_height_;             ///< The clearance above the board in mm.
    std::optional<double> bottom_clearance_height_;          ///< The clearance below the board in mm.
    std::optional<double> weight_;                           ///< The weight of the board in grams.
    std::optional<std::string> work_order_id_;               ///< The work order the board belongs to.
    std::optional<std::string> batch_id_;                    ///< The batch within the work order.
    std::optional<std::uint16_t> route_;                     ///< A routing instruction; see namespace route.
    std::optional<std::uint16_t> action_;                    ///< An action instruction; see namespace action.
    std::vector<sub_board> sub_boards_;                      ///< The subboards of the board, if any.

    bool operator==(const board_available&) const = default;
};

//==============================================================================
// 3.7 revoke_board_available
//==============================================================================

///
/// @brief The revoke_board_available message: the upstream machine withdraws its offer (3.7).
///
struct revoke_board_available {
    bool operator==(const revoke_board_available&) const = default;
};

//==============================================================================
// 3.8 machine_ready
//==============================================================================

///
/// @brief The machine_ready message: the downstream machine accepts a board (3.8).
///
struct machine_ready {
    board_quality failed_board_ { board_quality::ANY }; ///< The board quality the machine accepts.
    std::optional<std::string> forecast_id_;            ///< The id of the board_forecast this answers.
    std::optional<std::string> board_id_;               ///< The board that will be handed over next.
    std::optional<std::string> product_type_id_;        ///< The product type of that board.
    std::optional<flipped_board> flipped_board_;        ///< Which side of that board is up.
    std::optional<double> length_;                      ///< The length of the board in mm.
    std::optional<double> width_;                       ///< The width of the board in mm.
    std::optional<double> thickness_;                   ///< The thickness of the board in mm.
    std::optional<double> conveyor_speed_;              ///< The conveyor speed in mm/s.
    std::optional<double> top_clearance_height_;        ///< The clearance above the board in mm.
    std::optional<double> bottom_clearance_height_;     ///< The clearance below the board in mm.
    std::optional<double> weight_;                      ///< The weight of the board in grams.
    std::optional<std::string> work_order_id_;          ///< The work order of that board.
    std::optional<std::string> batch_id_;               ///< The batch of that board.

    bool operator==(const machine_ready&) const = default;
};

//==============================================================================
// 3.9 revoke_machine_ready
//==============================================================================

///
/// @brief The revoke_machine_ready message: the downstream machine withdraws its readiness (3.9).
///
struct revoke_machine_ready {
    bool operator==(const revoke_machine_ready&) const = default;
};

//==============================================================================
// 3.10 start_transport
//==============================================================================

///
/// @brief The start_transport message: the downstream machine starts the handover (3.10).
///
struct start_transport {
    std::string board_id_;                 ///< The board to transport.
    std::optional<double> conveyor_speed_; ///< The selected conveyor speed in mm/s.

    bool operator==(const start_transport&) const = default;
};

//==============================================================================
// 3.11 stop_transport
//==============================================================================

///
/// @brief The stop_transport message: the downstream machine finished the handover (3.11).
///
struct stop_transport {
    transfer_state transfer_state_ { transfer_state::NOT_STARTED }; ///< How far the transfer got.
    std::string board_id_;                                          ///< The board the message relates to.

    bool operator==(const stop_transport&) const = default;
};

//==============================================================================
// 3.12 transport_finished
//==============================================================================

///
/// @brief The transport_finished message: the upstream machine finished the handover (3.12).
///
struct transport_finished {
    transfer_state transfer_state_ { transfer_state::NOT_STARTED }; ///< How far the transfer got.
    std::string board_id_;                                          ///< The board the message relates to.

    bool operator==(const transport_finished&) const = default;
};

//==============================================================================
// 3.13 set_configuration
//==============================================================================

///
/// @brief The set_configuration message: an engineering station writes a machine's configuration (3.13).
///
struct set_configuration {
    std::string machine_id_;                               ///< The id of the configured machine.
    std::optional<std::uint16_t> supervisory_system_port_; ///< The port supervisory systems connect to.
    upstream_configurations upstream_configurations_;      ///< The configuration of the upstream lanes.
    downstream_configurations downstream_configurations_;  ///< The configuration of the downstream lanes.

    bool operator==(const set_configuration&) const = default;
};

//==============================================================================
// 3.14 get_configuration
//==============================================================================

///
/// @brief The get_configuration message: an engineering station reads a machine's configuration (3.14).
///
struct get_configuration {
    bool operator==(const get_configuration&) const = default;
};

//==============================================================================
// 3.15 current_configuration
//==============================================================================

///
/// @brief The current_configuration message: a machine reports its configuration (3.15).
///
struct current_configuration {
    std::optional<std::string> machine_id_;                ///< The id of the machine; unset until configured.
    std::optional<std::uint16_t> supervisory_system_port_; ///< The port supervisory systems connect to.
    upstream_configurations upstream_configurations_;      ///< The configuration of the upstream lanes.
    downstream_configurations downstream_configurations_;  ///< The configuration of the downstream lanes.

    bool operator==(const current_configuration&) const = default;
};

//==============================================================================
// 3.16 board_forecast
//==============================================================================

///
/// @brief The board_forecast message: the upstream machine announces the next board (3.16).
///
struct board_forecast {
    std::optional<std::string> forecast_id_;                 ///< The id a machine_ready has to echo.
    std::optional<double> time_until_available_;             ///< Seconds until a board may be available.
    std::optional<std::string> board_id_;                    ///< The GUID of the announced board.
    std::optional<std::string> board_id_created_by_;         ///< The machine that created the board id.
    board_quality failed_board_ { board_quality::ANY };      ///< The quality of the board.
    std::optional<std::string> product_type_id_;             ///< The product type of the board.
    flipped_board flipped_board_ { flipped_board::UNKNOWN }; ///< Which side of the board is up.
    std::optional<std::string> top_barcode_;                 ///< The barcode of the top side.
    std::optional<std::string> bottom_barcode_;              ///< The barcode of the bottom side.
    std::optional<double> length_;                           ///< The length of the board in mm.
    std::optional<double> width_;                            ///< The width of the board in mm.
    std::optional<double> thickness_;                        ///< The thickness of the board in mm.
    std::optional<double> conveyor_speed_;                   ///< The preferred conveyor speed in mm/s.
    std::optional<double> top_clearance_height_;             ///< The clearance above the board in mm.
    std::optional<double> bottom_clearance_height_;          ///< The clearance below the board in mm.
    std::optional<double> weight_;                           ///< The weight of the board in grams.
    std::optional<std::string> work_order_id_;               ///< The work order of the board.
    std::optional<std::string> batch_id_;                    ///< The batch of the board.

    bool operator==(const board_forecast&) const = default;
};

//==============================================================================
// 3.17 query_board_info
//==============================================================================

///
/// @brief The query_board_info message: the downstream machine asks for stored board data (3.17).
///
struct query_board_info {
    std::optional<std::string> top_barcode_;    ///< The top barcode to search for.
    std::optional<std::string> bottom_barcode_; ///< The bottom barcode to search for.

    bool operator==(const query_board_info&) const = default;
};

//==============================================================================
// 3.18 send_board_info
//==============================================================================

///
/// @brief The send_board_info message: the upstream machine answers a query_board_info (3.18).
///
struct send_board_info {
    std::optional<std::string> board_id_;            ///< The board the data belongs to; unset if not found.
    std::optional<std::string> board_id_created_by_; ///< The machine that created the board id.
    std::optional<board_quality> failed_board_;      ///< The quality of the board.
    std::optional<std::string> product_type_id_;     ///< The product type of the board.
    std::optional<flipped_board> flipped_board_;     ///< Which side of the board is up.
    std::optional<std::string> top_barcode_;         ///< The barcode of the top side.
    std::optional<std::string> bottom_barcode_;      ///< The barcode of the bottom side.
    std::optional<double> length_;                   ///< The length of the board in mm.
    std::optional<double> width_;                    ///< The width of the board in mm.
    std::optional<double> thickness_;                ///< The thickness of the board in mm.
    std::optional<double> top_clearance_height_;     ///< The clearance above the board in mm.
    std::optional<double> bottom_clearance_height_;  ///< The clearance below the board in mm.
    std::optional<double> weight_;                   ///< The weight of the board in grams.
    std::optional<std::string> work_order_id_;       ///< The work order of the board.
    std::optional<std::string> batch_id_;            ///< The batch of the board.
    std::optional<std::uint16_t> route_;             ///< A routing instruction; see namespace route.
    std::optional<std::uint16_t> action_;            ///< An action instruction; see namespace action.
    std::vector<sub_board> sub_boards_;              ///< The subboards of the board, if any.

    bool operator==(const send_board_info&) const = default;
};

//==============================================================================
// 3.19 supervisory_service_description
//==============================================================================

///
/// @brief The supervisory_service_description message: the handshake of the vertical channel (3.19).
///
struct supervisory_service_description {
    std::string system_id_;                    ///< The id of the sending machine or supervisory system.
    std::string version_ { PROTOCOL_VERSION }; ///< The implemented interface version, e.g. "1.6".
    supervisory_features supported_features_;  ///< The optional features the sender implements.

    bool operator==(const supervisory_service_description&) const = default;
};

//==============================================================================
// 3.20 board_arrived
//==============================================================================

///
/// @brief The board_arrived message: a board arrived at the machine (3.20).
///
struct board_arrived {
    std::string machine_id_;                           ///< The id of the sending machine.
    std::uint16_t upstream_lane_id_ {};                ///< The lane the board arrived on.
    std::optional<std::string> upstream_interface_id_; ///< The transport interface the board arrived on.
    std::optional<std::string> magazine_id_;           ///< The magazine the board came from.
    std::optional<std::uint16_t> slot_id_;             ///< The slot of that magazine.
    arrival_transfer board_transfer_ { arrival_transfer::TRANSFERRED }; ///< How the board arrived.
    std::string board_id_;                                              ///< The GUID of the board.
    std::string board_id_created_by_;                                   ///< The machine that created the board id.
    board_quality failed_board_ { board_quality::ANY };                 ///< The quality of the board.
    std::optional<std::string> product_type_id_;                        ///< The product type of the board.
    flipped_board flipped_board_ { flipped_board::UNKNOWN };            ///< Which side of the board is up.
    std::optional<std::string> top_barcode_;                            ///< The barcode of the top side.
    std::optional<std::string> bottom_barcode_;                         ///< The barcode of the bottom side.
    std::optional<double> length_;                                      ///< The length of the board in mm.
    std::optional<double> width_;                                       ///< The width of the board in mm.
    std::optional<double> thickness_;                                   ///< The thickness of the board in mm.
    std::optional<double> conveyor_speed_;                              ///< The conveyor speed used in mm/s.
    std::optional<double> top_clearance_height_;                        ///< The clearance above the board in mm.
    std::optional<double> bottom_clearance_height_;                     ///< The clearance below the board in mm.
    std::optional<double> weight_;                                      ///< The weight of the board in grams.
    std::optional<std::string> work_order_id_;                          ///< The work order of the board.
    std::optional<std::string> batch_id_;                               ///< The batch of the board.
    std::optional<std::uint16_t> route_;                                ///< A routing instruction; see namespace route.
    std::optional<std::uint16_t> action_; ///< An action instruction; see namespace action.
    std::vector<sub_board> sub_boards_;   ///< The subboards of the board, if any.

    bool operator==(const board_arrived&) const = default;
};

//==============================================================================
// 3.21 board_departed
//==============================================================================

///
/// @brief The board_departed message: a board left the machine (3.21).
///
struct board_departed {
    std::string machine_id_;                             ///< The id of the sending machine.
    std::uint16_t downstream_lane_id_ {};                ///< The lane the board left on.
    std::optional<std::string> downstream_interface_id_; ///< The transport interface the board left on.
    std::optional<std::string> magazine_id_;             ///< The magazine the board went to.
    std::optional<std::uint16_t> slot_id_;               ///< The slot of that magazine.
    departure_transfer board_transfer_ { departure_transfer::TRANSFERRED }; ///< How the board left.
    std::string board_id_;                                                  ///< The GUID of the board.
    std::string board_id_created_by_;                                       ///< The machine that created the board id.
    board_quality failed_board_ { board_quality::ANY };                     ///< The quality of the board.
    std::optional<std::string> product_type_id_;                            ///< The product type of the board.
    flipped_board flipped_board_ { flipped_board::UNKNOWN };                ///< Which side of the board is up.
    std::optional<std::string> top_barcode_;                                ///< The barcode of the top side.
    std::optional<std::string> bottom_barcode_;                             ///< The barcode of the bottom side.
    std::optional<double> length_;                                          ///< The length of the board in mm.
    std::optional<double> width_;                                           ///< The width of the board in mm.
    std::optional<double> thickness_;                                       ///< The thickness of the board in mm.
    std::optional<double> conveyor_speed_;                                  ///< The conveyor speed used in mm/s.
    std::optional<double> top_clearance_height_;                            ///< The clearance above the board in mm.
    std::optional<double> bottom_clearance_height_;                         ///< The clearance below the board in mm.
    std::optional<double> weight_;                                          ///< The weight of the board in grams.
    std::optional<std::string> work_order_id_;                              ///< The work order of the board.
    std::optional<std::string> batch_id_;                                   ///< The batch of the board.
    std::optional<std::uint16_t> route_;  ///< A routing instruction; see namespace route.
    std::optional<std::uint16_t> action_; ///< An action instruction; see namespace action.
    std::vector<sub_board> sub_boards_;   ///< The subboards of the board, if any.

    bool operator==(const board_departed&) const = default;
};

//==============================================================================
// 3.22 query_work_order_info
//==============================================================================

///
/// @brief The query_work_order_info message: a machine asks for work order data (3.22).
///
struct query_work_order_info {
    std::optional<std::string> query_id_;      ///< The id a send_work_order_info has to echo.
    std::string machine_id_;                   ///< The id of the requesting machine.
    std::optional<std::string> magazine_id_;   ///< The magazine the boards come from.
    std::optional<std::uint16_t> slot_id_;     ///< The slot of that magazine.
    std::optional<std::string> barcode_;       ///< The barcode of the board.
    std::optional<std::string> work_order_id_; ///< The known work order of the board.
    std::optional<std::string> batch_id_;      ///< The known batch of the board.

    bool operator==(const query_work_order_info&) const = default;
};

//==============================================================================
// 3.23 send_work_order_info
//==============================================================================

///
/// @brief The send_work_order_info message: a supervisory system supplies work order data (3.23).
///
struct send_work_order_info {
    std::optional<std::string> query_id_;            ///< The id of the query_work_order_info this answers.
    std::optional<std::string> work_order_id_;       ///< The work order of the boards.
    std::optional<std::string> batch_id_;            ///< The batch of the boards.
    std::optional<std::string> board_id_;            ///< The GUID of the board.
    std::optional<std::string> board_id_created_by_; ///< The machine that created the board id.
    std::optional<board_quality> failed_board_;      ///< The quality of the board.
    std::optional<std::string> product_type_id_;     ///< The product type of the board.
    std::optional<flipped_board> flipped_board_;     ///< Which side of the board is up.
    std::optional<std::string> top_barcode_;         ///< The barcode of the top side.
    std::optional<std::string> bottom_barcode_;      ///< The barcode of the bottom side.
    std::optional<double> length_;                   ///< The length of the board in mm.
    std::optional<double> width_;                    ///< The width of the board in mm.
    std::optional<double> thickness_;                ///< The thickness of the board in mm.
    std::optional<double> conveyor_speed_;           ///< The conveyor speed in mm/s.
    std::optional<double> top_clearance_height_;     ///< The clearance above the board in mm.
    std::optional<double> bottom_clearance_height_;  ///< The clearance below the board in mm.
    std::optional<double> weight_;                   ///< The weight of the board in grams.
    std::optional<std::uint16_t> route_;             ///< A routing instruction; see namespace route.
    std::vector<sub_board> sub_boards_;              ///< The subboards of the board, if any.

    bool operator==(const send_work_order_info&) const = default;
};

//==============================================================================
// 3.24 reply_work_order_info
//==============================================================================

///
/// @brief The reply_work_order_info message: a machine reports the status of a work order (3.24).
///
struct reply_work_order_info {
    std::string work_order_id_;                                            ///< The work order the reply is about.
    std::optional<std::string> batch_id_;                                  ///< The batch the reply is about.
    reply_work_order_status status_ { reply_work_order_status::REJECTED }; ///< The result of the execution.

    bool operator==(const reply_work_order_info&) const = default;
};

//==============================================================================
// 3.25 command
//==============================================================================

///
/// @brief The command message: a machine asks its neighbour to perform an activity (3.25).
///
struct command {
    command_code command_ { command_code::UNDEFINED }; ///< The activity to perform.

    bool operator==(const command&) const = default;
};

//==============================================================================
// 3.26 query_hermes_capabilities
//==============================================================================

///
/// @brief The query_hermes_capabilities message: a supervisory system asks for capabilities (3.26).
///
struct query_hermes_capabilities {
    bool operator==(const query_hermes_capabilities&) const = default;
};

//==============================================================================
// 3.27 send_hermes_capabilities
//==============================================================================

///
/// @brief The send_hermes_capabilities message: a machine reports what it supports (3.27).
///
struct send_hermes_capabilities {
    optional_messages optional_messages_; ///< The optional messages the machine implements.
    attribute_capabilities attributes_;   ///< The support of each board attribute.

    bool operator==(const send_hermes_capabilities&) const = default;
};

//==============================================================================
// message traits
//==============================================================================

///
/// @brief The traits of one message: the element name it travels in on the wire.
/// @note Only messages the standard names have a specialization; the concept below detects them.
///
template<class M>
struct message_traits;

template<>
struct message_traits<check_alive> {
    static constexpr std::string_view name { "CheckAlive" };
};

template<>
struct message_traits<service_description> {
    static constexpr std::string_view name { "ServiceDescription" };
};

template<>
struct message_traits<notification> {
    static constexpr std::string_view name { "Notification" };
};

template<>
struct message_traits<board_available> {
    static constexpr std::string_view name { "BoardAvailable" };
};

template<>
struct message_traits<revoke_board_available> {
    static constexpr std::string_view name { "RevokeBoardAvailable" };
};

template<>
struct message_traits<machine_ready> {
    static constexpr std::string_view name { "MachineReady" };
};

template<>
struct message_traits<revoke_machine_ready> {
    static constexpr std::string_view name { "RevokeMachineReady" };
};

template<>
struct message_traits<start_transport> {
    static constexpr std::string_view name { "StartTransport" };
};

template<>
struct message_traits<stop_transport> {
    static constexpr std::string_view name { "StopTransport" };
};

template<>
struct message_traits<transport_finished> {
    static constexpr std::string_view name { "TransportFinished" };
};

template<>
struct message_traits<set_configuration> {
    static constexpr std::string_view name { "SetConfiguration" };
};

template<>
struct message_traits<get_configuration> {
    static constexpr std::string_view name { "GetConfiguration" };
};

template<>
struct message_traits<current_configuration> {
    static constexpr std::string_view name { "CurrentConfiguration" };
};

template<>
struct message_traits<board_forecast> {
    static constexpr std::string_view name { "BoardForecast" };
};

template<>
struct message_traits<query_board_info> {
    static constexpr std::string_view name { "QueryBoardInfo" };
};

template<>
struct message_traits<send_board_info> {
    static constexpr std::string_view name { "SendBoardInfo" };
};

template<>
struct message_traits<supervisory_service_description> {
    static constexpr std::string_view name { "SupervisoryServiceDescription" };
};

template<>
struct message_traits<board_arrived> {
    static constexpr std::string_view name { "BoardArrived" };
};

template<>
struct message_traits<board_departed> {
    static constexpr std::string_view name { "BoardDeparted" };
};

template<>
struct message_traits<query_work_order_info> {
    static constexpr std::string_view name { "QueryWorkOrderInfo" };
};

template<>
struct message_traits<send_work_order_info> {
    static constexpr std::string_view name { "SendWorkOrderInfo" };
};

template<>
struct message_traits<reply_work_order_info> {
    static constexpr std::string_view name { "ReplyWorkOrderInfo" };
};

template<>
struct message_traits<command> {
    static constexpr std::string_view name { "Command" };
};

template<>
struct message_traits<query_hermes_capabilities> {
    static constexpr std::string_view name { "QueryHermesCapabilities" };
};

template<>
struct message_traits<send_hermes_capabilities> {
    static constexpr std::string_view name { "SendHermesCapabilities" };
};

///
/// @brief The element name of the message @p M travels in.
///
template<class M>
inline constexpr std::string_view message_name = message_traits<std::remove_cvref_t<M>>::name;

///
/// @brief Whether @p M is one of the messages of the standard.
///
template<class M>
concept message = requires { message_traits<std::remove_cvref_t<M>>::name; };

//==============================================================================
// any_message
//==============================================================================

///
/// @brief Any message of the standard, the type a session sends and reports.
/// @note The order is the order of the standard's message definition chapter.
///
using any_message = std::variant<
    check_alive,
    service_description,
    notification,
    board_available,
    revoke_board_available,
    machine_ready,
    revoke_machine_ready,
    start_transport,
    stop_transport,
    transport_finished,
    set_configuration,
    get_configuration,
    current_configuration,
    board_forecast,
    query_board_info,
    send_board_info,
    supervisory_service_description,
    board_arrived,
    board_departed,
    query_work_order_info,
    send_work_order_info,
    reply_work_order_info,
    command,
    query_hermes_capabilities,
    send_hermes_capabilities>;

///
/// @brief The element name of the message @p message travels in.
///
inline std::string_view message_name_of(const any_message& message) {
    return std::visit([](const auto& value) { return message_name<std::remove_cvref_t<decltype(value)>>; }, message);
}

} // namespace ba7lya::hermes
