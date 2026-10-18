// SPDX-License-Identifier: MIT
///
/// @file xml.cxx
/// @author BA7LYA (1042140025@qq.com)
/// @brief The XML codec of The Hermes Standard: one writer and one reader per message.
/// @version 0.1
/// @date 2026-10-18
/// @copyright Copyright (c) 2026
///
/// The standard puts every field of a message into an XML attribute of the message element; only
/// the lists (supported_features, SubBoards, optional_messages, Attributes and the two configuration
/// lists) are child elements. The functions below follow the message tables of chapter 3 field by
/// field, including which fields are optional and which are mandatory.
///

#include "xml.hxx"

#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <pugixml.hpp>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>

namespace ba7lya::hermes {

namespace {

//==============================================================================
// errors
//==============================================================================

/// @brief Keeps the first failure of a document; the later fields are still read to report the
/// most precise message, but the first one wins.
void keep_first(error_info& target, error_info candidate) {
    if (!target && candidate) { target = std::move(candidate); }
}

void fail(error_info& error, error_code code, std::string text) {
    keep_first(error, error_info { code, std::move(text) });
}

std::string quoted(std::string_view name) { return "'" + std::string { name } + "'"; }

//==============================================================================
// values
//==============================================================================

/// @brief Whether @p value is a value the standard defines for its enumeration.
constexpr bool is_valid(board_quality value) noexcept { return static_cast<unsigned>(value) <= 2U; }

constexpr bool is_valid(flipped_board value) noexcept { return static_cast<unsigned>(value) <= 2U; }

constexpr bool is_valid(sub_board_state value) noexcept { return static_cast<unsigned>(value) <= 4U; }

constexpr bool is_valid(transfer_state value) noexcept {
    return static_cast<unsigned>(value) >= 1U && static_cast<unsigned>(value) <= 3U;
}

constexpr bool is_valid(arrival_transfer value) noexcept {
    return static_cast<unsigned>(value) >= 1U && static_cast<unsigned>(value) <= 3U;
}

constexpr bool is_valid(departure_transfer value) noexcept {
    return static_cast<unsigned>(value) >= 1U && static_cast<unsigned>(value) <= 3U;
}

constexpr bool is_valid(reply_work_order_status value) noexcept { return static_cast<unsigned>(value) <= 2U; }

constexpr bool is_valid(severity value) noexcept {
    return static_cast<unsigned>(value) >= 1U && static_cast<unsigned>(value) <= 4U;
}

constexpr bool is_valid(check_alive_type value) noexcept {
    return static_cast<unsigned>(value) >= 1U && static_cast<unsigned>(value) <= 2U;
}

constexpr bool is_valid(notification_code) noexcept { return true; }

constexpr bool is_valid(command_code) noexcept { return true; }

/// @brief Formats a floating point value the shortest way that reads back unchanged.
std::string format_double(double value) {
    std::array<char, 64> buffer {};
    const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    if (result.ec != std::errc {}) { return "0"; }
    return std::string { buffer.data(), result.ptr };
}

//==============================================================================
// writer
//==============================================================================

/// @brief Writes @p value into the attribute @p name of @p node.
void set(pugi::xml_node node, const char* name, std::string_view value) {
    node.append_attribute(name).set_value(value.data(), static_cast<unsigned>(value.size()));
}

void set(pugi::xml_node node, const char* name, const std::string& value) {
    node.append_attribute(name).set_value(value.c_str());
}

void set(pugi::xml_node node, const char* name, const std::optional<std::string>& value) {
    if (value) { set(node, name, *value); }
}

void set(pugi::xml_node node, const char* name, double value) {
    node.append_attribute(name).set_value(format_double(value).c_str());
}

void set(pugi::xml_node node, const char* name, const std::optional<double>& value) {
    if (value) { set(node, name, *value); }
}

template<std::integral T>
void set(pugi::xml_node node, const char* name, T value) {
    node.append_attribute(name).set_value(static_cast<long long>(value));
}

template<std::integral T>
void set(pugi::xml_node node, const char* name, const std::optional<T>& value) {
    if (value) { set(node, name, *value); }
}

template<class E>
    requires std::is_enum_v<E>
void set(pugi::xml_node node, const char* name, E value) {
    node.append_attribute(name).set_value(static_cast<long long>(static_cast<std::underlying_type_t<E>>(value)));
}

template<class E>
    requires std::is_enum_v<E>
void set(pugi::xml_node node, const char* name, const std::optional<E>& value) {
    if (value) { set(node, name, *value); }
}

/// @brief Writes a list of subboards as one SubBoards element holding one SB child each.
void set(pugi::xml_node node, const std::vector<sub_board>& value) {
    if (value.empty()) { return; }

    auto list = node.append_child("SubBoards");
    for (const auto& sub_board : value) {
        auto entry = list.append_child("SB");
        set(entry, "Pos", sub_board.pos_);
        set(entry, "Bc", sub_board.barcode_);
        set(entry, "St", sub_board.state_);
    }
}

//==============================================================================
// reader
//==============================================================================

/// @brief The attribute @p name of @p node, or an empty attribute when it is absent.
pugi::xml_attribute attribute(pugi::xml_node node, const char* name) { return node.attribute(name); }

template<std::integral T>
bool parse_integer(std::string_view text, T& value) {
    if (text.empty()) { return false; }
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc {} && result.ptr == text.data() + text.size();
}

bool parse_double(std::string_view text, double& value) {
    if (text.empty()) { return false; }
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc {} && result.ptr == text.data() + text.size();
}

/// @brief Reads the mandatory attribute @p name into @p value.
void get(pugi::xml_node node, const char* name, std::string& value, error_info& error) {
    const auto attr = attribute(node, name);
    if (!attr) {
        fail(error, error_code::PEER, "missing attribute " + quoted(name));
        return;
    }
    value = attr.value();
}

/// @brief Reads the optional attribute @p name into @p value; an absent one stays unset.
void get(pugi::xml_node node, const char* name, std::optional<std::string>& value, error_info& error) {
    const auto attr = attribute(node, name);
    if (!attr) { return; }
    value = attr.value();
    (void)error;
}

/// @brief Reads the mandatory integer attribute @p name into @p value.
template<std::integral T>
void get(pugi::xml_node node, const char* name, T& value, error_info& error) {
    const auto attr = attribute(node, name);
    if (!attr) {
        fail(error, error_code::PEER, "missing attribute " + quoted(name));
        return;
    }
    T parsed {};
    if (!parse_integer(attr.value(), parsed)) {
        fail(error, error_code::PEER, "invalid integer in attribute " + quoted(name));
        return;
    }
    value = parsed;
}

/// @brief Reads the optional integer attribute @p name into @p value.
template<std::integral T>
void get(pugi::xml_node node, const char* name, std::optional<T>& value, error_info& error) {
    const auto attr = attribute(node, name);
    if (!attr) { return; }

    T parsed {};
    if (!parse_integer(attr.value(), parsed)) {
        fail(error, error_code::PEER, "invalid integer in attribute " + quoted(name));
        return;
    }
    value = parsed;
}

/// @brief Reads the mandatory floating point attribute @p name into @p value.
void get(pugi::xml_node node, const char* name, double& value, error_info& error) {
    const auto attr = attribute(node, name);
    if (!attr) {
        fail(error, error_code::PEER, "missing attribute " + quoted(name));
        return;
    }
    double parsed {};
    if (!parse_double(attr.value(), parsed)) {
        fail(error, error_code::PEER, "invalid number in attribute " + quoted(name));
        return;
    }
    value = parsed;
}

/// @brief Reads the optional floating point attribute @p name into @p value.
void get(pugi::xml_node node, const char* name, std::optional<double>& value, error_info& error) {
    const auto attr = attribute(node, name);
    if (!attr) { return; }

    double parsed {};
    if (!parse_double(attr.value(), parsed)) {
        fail(error, error_code::PEER, "invalid number in attribute " + quoted(name));
        return;
    }
    value = parsed;
}

/// @brief Reads the mandatory enumerator attribute @p name into @p value.
template<class E>
    requires std::is_enum_v<E>
void get(pugi::xml_node node, const char* name, E& value, error_info& error) {
    const auto attr = attribute(node, name);
    if (!attr) {
        fail(error, error_code::PEER, "missing attribute " + quoted(name));
        return;
    }

    std::underlying_type_t<E> parsed {};
    if (!parse_integer(attr.value(), parsed)) {
        fail(error, error_code::PEER, "invalid integer in attribute " + quoted(name));
        return;
    }

    const auto candidate = static_cast<E>(parsed);
    if (!is_valid(candidate)) {
        fail(error, error_code::PEER, "value out of range in attribute " + quoted(name));
        return;
    }
    value = candidate;
}

/// @brief Reads the optional enumerator attribute @p name into @p value; a value outside the
/// range of the standard is discarded, as a higher version of the standard may define it.
template<class E>
    requires std::is_enum_v<E>
void get(pugi::xml_node node, const char* name, std::optional<E>& value, error_info& error) {
    const auto attr = attribute(node, name);
    if (!attr) { return; }

    std::underlying_type_t<E> parsed {};
    if (!parse_integer(attr.value(), parsed)) {
        fail(error, error_code::PEER, "invalid integer in attribute " + quoted(name));
        return;
    }

    const auto candidate = static_cast<E>(parsed);
    if (!is_valid(candidate)) { return; }
    value = candidate;
}

/// @brief Reads the SubBoards element of @p node; a missing element leaves @p value empty.
void get(pugi::xml_node node, std::vector<sub_board>& value, error_info& error) {
    const auto list = node.child("SubBoards");
    if (!list) { return; }

    for (const auto& entry : list.children("SB")) {
        sub_board sub_board;
        get(entry, "Pos", sub_board.pos_, error);
        get(entry, "Bc", sub_board.barcode_, error);
        get(entry, "St", sub_board.state_, error);
        if (error) { return; }
        value.push_back(std::move(sub_board));
    }
}

//==============================================================================
// capabilities
//==============================================================================

void set(pugi::xml_node node, const supported_features& value) {
    auto features = node.append_child("SupportedFeatures");
    if (value.board_forecast_) { features.append_child("FeatureBoardForecast"); }
    if (value.check_alive_response_) { features.append_child("FeatureCheckAliveResponse"); }
    if (value.query_board_info_) { features.append_child("FeatureQueryBoardInfo"); }
    if (value.send_board_info_) { features.append_child("FeatureSendBoardInfo"); }
    if (value.command_) { features.append_child("FeatureCommand"); }
}

void get(pugi::xml_node node, supported_features& value, error_info&) {
    const auto features = node.child("SupportedFeatures");
    if (!features) { return; }

    value.board_forecast_ = static_cast<bool>(features.child("FeatureBoardForecast"));
    value.check_alive_response_ = static_cast<bool>(features.child("FeatureCheckAliveResponse"));
    value.query_board_info_ = static_cast<bool>(features.child("FeatureQueryBoardInfo"));
    value.send_board_info_ = static_cast<bool>(features.child("FeatureSendBoardInfo"));
    value.command_ = static_cast<bool>(features.child("FeatureCommand"));
}

void set(pugi::xml_node node, const supervisory_features& value) {
    auto features = node.append_child("SupportedFeatures");
    if (value.configuration_) { features.append_child("FeatureConfiguration"); }
    if (value.check_alive_response_) { features.append_child("FeatureCheckAliveResponse"); }
    if (value.board_tracking_) { features.append_child("FeatureBoardTracking"); }
    if (value.query_work_order_info_) { features.append_child("FeatureQueryWorkOrderInfo"); }
    if (value.send_work_order_info_) { features.append_child("FeatureSendWorkOrderInfo"); }
    if (value.reply_work_order_info_) { features.append_child("FeatureReplyWorkOrderInfo"); }
    if (value.query_hermes_capabilities_) { features.append_child("FeatureQueryHermesCapabilities"); }
    if (value.send_hermes_capabilities_) { features.append_child("FeatureSendHermesCapabilities"); }
}

void get(pugi::xml_node node, supervisory_features& value, error_info&) {
    const auto features = node.child("SupportedFeatures");
    if (!features) { return; }

    value.configuration_ = static_cast<bool>(features.child("FeatureConfiguration"));
    value.check_alive_response_ = static_cast<bool>(features.child("FeatureCheckAliveResponse"));
    value.board_tracking_ = static_cast<bool>(features.child("FeatureBoardTracking"));
    value.query_work_order_info_ = static_cast<bool>(features.child("FeatureQueryWorkOrderInfo"));
    value.send_work_order_info_ = static_cast<bool>(features.child("FeatureSendWorkOrderInfo"));
    value.reply_work_order_info_ = static_cast<bool>(features.child("FeatureReplyWorkOrderInfo"));
    value.query_hermes_capabilities_ = static_cast<bool>(features.child("FeatureQueryHermesCapabilities"));
    value.send_hermes_capabilities_ = static_cast<bool>(features.child("FeatureSendHermesCapabilities"));
}

void set(pugi::xml_node node, const optional_messages& value) {
    auto messages = node.append_child("OptionalMessages");
    if (value.check_alive_response_) { messages.append_child("MessageCheckAliveResponse"); }
    if (value.board_forecast_) { messages.append_child("MessageBoardForecast"); }
    if (value.query_board_info_) { messages.append_child("MessageQueryBoardInfo"); }
    if (value.send_board_info_) { messages.append_child("MessageSendBoardInfo"); }
    if (value.board_arrived_) { messages.append_child("MessageBoardArrived"); }
    if (value.board_departed_) { messages.append_child("MessageBoardDeparted"); }
    if (value.query_work_order_info_) { messages.append_child("MessageQueryWorkOrderInfo"); }
    if (value.reply_work_order_info_) { messages.append_child("MessageReplyWorkOrderInfo"); }
    if (value.command_) { messages.append_child("MessageCommand"); }
}

void get(pugi::xml_node node, optional_messages& value, error_info&) {
    const auto messages = node.child("OptionalMessages");
    if (!messages) { return; }

    value.check_alive_response_ = static_cast<bool>(messages.child("MessageCheckAliveResponse"));
    value.board_forecast_ = static_cast<bool>(messages.child("MessageBoardForecast"));
    value.query_board_info_ = static_cast<bool>(messages.child("MessageQueryBoardInfo"));
    value.send_board_info_ = static_cast<bool>(messages.child("MessageSendBoardInfo"));
    value.board_arrived_ = static_cast<bool>(messages.child("MessageBoardArrived"));
    value.board_departed_ = static_cast<bool>(messages.child("MessageBoardDeparted"));
    value.query_work_order_info_ = static_cast<bool>(messages.child("MessageQueryWorkOrderInfo"));
    value.reply_work_order_info_ = static_cast<bool>(messages.child("MessageReplyWorkOrderInfo"));
    value.command_ = static_cast<bool>(messages.child("MessageCommand"));
}

/// @brief Writes one attribute support value; 0 means unsupported and is left out.
void set_support(pugi::xml_node node, const char* name, std::uint8_t value) {
    if (value != 0U) { set(node, name, value); }
}

void set(pugi::xml_node node, const attribute_capabilities& value) {
    auto attributes = node.append_child("Attributes");
    set_support(attributes, "ProductTypeId", value.product_type_id_);
    set_support(attributes, "TopBarcode", value.top_barcode_);
    set_support(attributes, "BottomBarcode", value.bottom_barcode_);
    set_support(attributes, "Length", value.length_);
    set_support(attributes, "Width", value.width_);
    set_support(attributes, "Thickness", value.thickness_);
    set_support(attributes, "ConveyorSpeed", value.conveyor_speed_);
    set_support(attributes, "TopClearanceHeight", value.top_clearance_height_);
    set_support(attributes, "BottomClearanceHeight", value.bottom_clearance_height_);
    set_support(attributes, "Weight", value.weight_);
    set_support(attributes, "WorkOrderId", value.work_order_id_);
    set_support(attributes, "BatchId", value.batch_id_);
    set_support(attributes, "Route", value.route_);
    set_support(attributes, "Action", value.action_);
    set_support(attributes, "SubBoards", value.sub_boards_);
}

/// @brief Reads one attribute support value, which the standard limits to 0..7.
void get_support(pugi::xml_node node, const char* name, std::uint8_t& value, error_info& error) {
    const auto attr = attribute(node, name);
    if (!attr) { return; }

    int parsed {};
    if (!parse_integer(std::string_view { attr.value() }, parsed) || parsed < 0 || parsed > 7) {
        fail(error, error_code::PEER, "invalid attribute support value in " + quoted(name));
        return;
    }
    value = static_cast<std::uint8_t>(parsed);
}

void get(pugi::xml_node node, attribute_capabilities& value, error_info& error) {
    const auto attributes = node.child("Attributes");
    if (!attributes) { return; }

    get_support(attributes, "ProductTypeId", value.product_type_id_, error);
    get_support(attributes, "TopBarcode", value.top_barcode_, error);
    get_support(attributes, "BottomBarcode", value.bottom_barcode_, error);
    get_support(attributes, "Length", value.length_, error);
    get_support(attributes, "Width", value.width_, error);
    get_support(attributes, "Thickness", value.thickness_, error);
    get_support(attributes, "ConveyorSpeed", value.conveyor_speed_, error);
    get_support(attributes, "TopClearanceHeight", value.top_clearance_height_, error);
    get_support(attributes, "BottomClearanceHeight", value.bottom_clearance_height_, error);
    get_support(attributes, "Weight", value.weight_, error);
    get_support(attributes, "WorkOrderId", value.work_order_id_, error);
    get_support(attributes, "BatchId", value.batch_id_, error);
    get_support(attributes, "Route", value.route_, error);
    get_support(attributes, "Action", value.action_, error);
    get_support(attributes, "SubBoards", value.sub_boards_, error);
}

//==============================================================================
// configuration lists
//==============================================================================

void set(pugi::xml_node node, const upstream_configuration& value) {
    set(node, "UpstreamLaneId", value.upstream_lane_id_);
    set(node, "UpstreamInterfaceId", value.upstream_interface_id_);
    set(node, "HostAddress", value.host_address_);
    set(node, "Port", value.port_);
}

void get(pugi::xml_node node, upstream_configuration& value, error_info& error) {
    get(node, "UpstreamLaneId", value.upstream_lane_id_, error);
    get(node, "UpstreamInterfaceId", value.upstream_interface_id_, error);
    get(node, "HostAddress", value.host_address_, error);
    get(node, "Port", value.port_, error);
}

void set(pugi::xml_node node, const downstream_configuration& value) {
    set(node, "DownstreamLaneId", value.downstream_lane_id_);
    set(node, "DownstreamInterfaceId", value.downstream_interface_id_);
    set(node, "ClientAddress", value.client_address_);
    set(node, "Port", value.port_);
}

void get(pugi::xml_node node, downstream_configuration& value, error_info& error) {
    get(node, "DownstreamLaneId", value.downstream_lane_id_, error);
    get(node, "DownstreamInterfaceId", value.downstream_interface_id_, error);
    get(node, "ClientAddress", value.client_address_, error);
    get(node, "Port", value.port_, error);
}

void set(pugi::xml_node node, const char* name, const char* entry_name, const upstream_configurations& value) {
    if (value.empty()) { return; }

    auto list = node.append_child(name);
    for (const auto& item : value) { set(list.append_child(entry_name), item); }
}

void set(pugi::xml_node node, const char* name, const char* entry_name, const downstream_configurations& value) {
    if (value.empty()) { return; }

    auto list = node.append_child(name);
    for (const auto& item : value) { set(list.append_child(entry_name), item); }
}

/// @brief Reads the entries of @p list into @p value.
template<class T, class Get>
void get_entries(pugi::xml_node list, const char* entry_name, std::vector<T>& value, error_info& error, Get get_entry) {
    for (const auto& entry : list.children(entry_name)) {
        T item {};
        get_entry(entry, item, error);
        if (error) { return; }
        value.push_back(std::move(item));
    }
}

/// @brief Reads a configuration list, accepting the entries with and without their wrapper
/// element, as implementations differ in that detail.
template<class T, class Get>
void get_list(
    pugi::xml_node node,
    const char* name,
    const char* entry_name,
    std::vector<T>& value,
    error_info& error,
    Get get_entry
) {
    const auto list = node.child(name);
    if (list) {
        get_entries(list, entry_name, value, error, get_entry);
        return;
    }
    get_entries(node, entry_name, value, error, get_entry);
}

//==============================================================================
// messages: write
//==============================================================================

void write(pugi::xml_node node, const check_alive& value) {
    set(node, "Type", value.type_);
    set(node, "Id", value.id_);
}

void write(pugi::xml_node node, const service_description& value) {
    set(node, "MachineId", value.machine_id_);
    set(node, "LaneId", value.lane_id_);
    set(node, "InterfaceId", value.interface_id_);
    set(node, "Version", value.version_);
    set(node, value.supported_features_);
}

void write(pugi::xml_node node, const notification& value) {
    set(node, "NotificationCode", value.notification_code_);
    set(node, "Severity", value.severity_);
    set(node, "Description", value.description_);
}

void write(pugi::xml_node node, const board_available& value, bool include_sub_boards) {
    set(node, "BoardId", value.board_id_);
    set(node, "BoardIdCreatedBy", value.board_id_created_by_);
    set(node, "FailedBoard", value.failed_board_);
    set(node, "ProductTypeId", value.product_type_id_);
    set(node, "FlippedBoard", value.flipped_board_);
    set(node, "TopBarcode", value.top_barcode_);
    set(node, "BottomBarcode", value.bottom_barcode_);
    set(node, "Length", value.length_);
    set(node, "Width", value.width_);
    set(node, "Thickness", value.thickness_);
    set(node, "ConveyorSpeed", value.conveyor_speed_);
    set(node, "TopClearanceHeight", value.top_clearance_height_);
    set(node, "BottomClearanceHeight", value.bottom_clearance_height_);
    set(node, "Weight", value.weight_);
    set(node, "WorkOrderId", value.work_order_id_);
    set(node, "BatchId", value.batch_id_);
    set(node, "Route", value.route_);
    set(node, "Action", value.action_);
    if (include_sub_boards) { set(node, value.sub_boards_); }
}

void write(pugi::xml_node, const revoke_board_available&) {}

void write(pugi::xml_node node, const machine_ready& value) {
    set(node, "FailedBoard", value.failed_board_);
    set(node, "ForecastId", value.forecast_id_);
    set(node, "BoardId", value.board_id_);
    set(node, "ProductTypeId", value.product_type_id_);
    set(node, "FlippedBoard", value.flipped_board_);
    set(node, "Length", value.length_);
    set(node, "Width", value.width_);
    set(node, "Thickness", value.thickness_);
    set(node, "ConveyorSpeed", value.conveyor_speed_);
    set(node, "TopClearanceHeight", value.top_clearance_height_);
    set(node, "BottomClearanceHeight", value.bottom_clearance_height_);
    set(node, "Weight", value.weight_);
    set(node, "WorkOrderId", value.work_order_id_);
    set(node, "BatchId", value.batch_id_);
}

void write(pugi::xml_node, const revoke_machine_ready&) {}

void write(pugi::xml_node node, const start_transport& value) {
    set(node, "BoardId", value.board_id_);
    set(node, "ConveyorSpeed", value.conveyor_speed_);
}

void write(pugi::xml_node node, const stop_transport& value) {
    set(node, "TransferState", value.transfer_state_);
    set(node, "BoardId", value.board_id_);
}

void write(pugi::xml_node node, const transport_finished& value) {
    set(node, "TransferState", value.transfer_state_);
    set(node, "BoardId", value.board_id_);
}

void write(pugi::xml_node node, const set_configuration& value) {
    set(node, "MachineId", value.machine_id_);
    set(node, "SupervisorySystemPort", value.supervisory_system_port_);
    set(node, "UpstreamConfigurations", "UpstreamConfiguration", value.upstream_configurations_);
    set(node, "DownstreamConfigurations", "DownstreamConfiguration", value.downstream_configurations_);
}

void write(pugi::xml_node, const get_configuration&) {}

void write(pugi::xml_node node, const current_configuration& value) {
    set(node, "MachineId", value.machine_id_);
    set(node, "SupervisorySystemPort", value.supervisory_system_port_);
    set(node, "UpstreamConfigurations", "UpstreamConfiguration", value.upstream_configurations_);
    set(node, "DownstreamConfigurations", "DownstreamConfiguration", value.downstream_configurations_);
}

void write(pugi::xml_node node, const board_forecast& value) {
    set(node, "ForecastId", value.forecast_id_);
    set(node, "TimeUntilAvailable", value.time_until_available_);
    set(node, "BoardId", value.board_id_);
    set(node, "BoardIdCreatedBy", value.board_id_created_by_);
    set(node, "FailedBoard", value.failed_board_);
    set(node, "ProductTypeId", value.product_type_id_);
    set(node, "FlippedBoard", value.flipped_board_);
    set(node, "TopBarcode", value.top_barcode_);
    set(node, "BottomBarcode", value.bottom_barcode_);
    set(node, "Length", value.length_);
    set(node, "Width", value.width_);
    set(node, "Thickness", value.thickness_);
    set(node, "ConveyorSpeed", value.conveyor_speed_);
    set(node, "TopClearanceHeight", value.top_clearance_height_);
    set(node, "BottomClearanceHeight", value.bottom_clearance_height_);
    set(node, "Weight", value.weight_);
    set(node, "WorkOrderId", value.work_order_id_);
    set(node, "BatchId", value.batch_id_);
}

void write(pugi::xml_node node, const query_board_info& value) {
    set(node, "TopBarcode", value.top_barcode_);
    set(node, "BottomBarcode", value.bottom_barcode_);
}

void write(pugi::xml_node node, const send_board_info& value, bool include_sub_boards) {
    set(node, "BoardId", value.board_id_);
    set(node, "BoardIdCreatedBy", value.board_id_created_by_);
    set(node, "FailedBoard", value.failed_board_);
    set(node, "ProductTypeId", value.product_type_id_);
    set(node, "FlippedBoard", value.flipped_board_);
    set(node, "TopBarcode", value.top_barcode_);
    set(node, "BottomBarcode", value.bottom_barcode_);
    set(node, "Length", value.length_);
    set(node, "Width", value.width_);
    set(node, "Thickness", value.thickness_);
    set(node, "TopClearanceHeight", value.top_clearance_height_);
    set(node, "BottomClearanceHeight", value.bottom_clearance_height_);
    set(node, "Weight", value.weight_);
    set(node, "WorkOrderId", value.work_order_id_);
    set(node, "BatchId", value.batch_id_);
    set(node, "Route", value.route_);
    set(node, "Action", value.action_);
    if (include_sub_boards) { set(node, value.sub_boards_); }
}

void write(pugi::xml_node node, const supervisory_service_description& value) {
    set(node, "SystemId", value.system_id_);
    set(node, "Version", value.version_);
    set(node, value.supported_features_);
}

void write(pugi::xml_node node, const board_arrived& value, bool include_sub_boards) {
    set(node, "MachineId", value.machine_id_);
    set(node, "UpstreamLaneId", value.upstream_lane_id_);
    set(node, "UpstreamInterfaceId", value.upstream_interface_id_);
    set(node, "MagazineId", value.magazine_id_);
    set(node, "SlotId", value.slot_id_);
    set(node, "BoardTransfer", value.board_transfer_);
    set(node, "BoardId", value.board_id_);
    set(node, "BoardIdCreatedBy", value.board_id_created_by_);
    set(node, "FailedBoard", value.failed_board_);
    set(node, "ProductTypeId", value.product_type_id_);
    set(node, "FlippedBoard", value.flipped_board_);
    set(node, "TopBarcode", value.top_barcode_);
    set(node, "BottomBarcode", value.bottom_barcode_);
    set(node, "Length", value.length_);
    set(node, "Width", value.width_);
    set(node, "Thickness", value.thickness_);
    set(node, "ConveyorSpeed", value.conveyor_speed_);
    set(node, "TopClearanceHeight", value.top_clearance_height_);
    set(node, "BottomClearanceHeight", value.bottom_clearance_height_);
    set(node, "Weight", value.weight_);
    set(node, "WorkOrderId", value.work_order_id_);
    set(node, "BatchId", value.batch_id_);
    set(node, "Route", value.route_);
    set(node, "Action", value.action_);
    if (include_sub_boards) { set(node, value.sub_boards_); }
}

void write(pugi::xml_node node, const board_departed& value, bool include_sub_boards) {
    set(node, "MachineId", value.machine_id_);
    set(node, "DownstreamLaneId", value.downstream_lane_id_);
    set(node, "DownstreamInterfaceId", value.downstream_interface_id_);
    set(node, "MagazineId", value.magazine_id_);
    set(node, "SlotId", value.slot_id_);
    set(node, "BoardTransfer", value.board_transfer_);
    set(node, "BoardId", value.board_id_);
    set(node, "BoardIdCreatedBy", value.board_id_created_by_);
    set(node, "FailedBoard", value.failed_board_);
    set(node, "ProductTypeId", value.product_type_id_);
    set(node, "FlippedBoard", value.flipped_board_);
    set(node, "TopBarcode", value.top_barcode_);
    set(node, "BottomBarcode", value.bottom_barcode_);
    set(node, "Length", value.length_);
    set(node, "Width", value.width_);
    set(node, "Thickness", value.thickness_);
    set(node, "ConveyorSpeed", value.conveyor_speed_);
    set(node, "TopClearanceHeight", value.top_clearance_height_);
    set(node, "BottomClearanceHeight", value.bottom_clearance_height_);
    set(node, "Weight", value.weight_);
    set(node, "WorkOrderId", value.work_order_id_);
    set(node, "BatchId", value.batch_id_);
    set(node, "Route", value.route_);
    set(node, "Action", value.action_);
    if (include_sub_boards) { set(node, value.sub_boards_); }
}

void write(pugi::xml_node node, const query_work_order_info& value) {
    set(node, "QueryId", value.query_id_);
    set(node, "MachineId", value.machine_id_);
    set(node, "MagazineId", value.magazine_id_);
    set(node, "SlotId", value.slot_id_);
    set(node, "Barcode", value.barcode_);
    set(node, "WorkOrderId", value.work_order_id_);
    set(node, "BatchId", value.batch_id_);
}

void write(pugi::xml_node node, const send_work_order_info& value, bool include_sub_boards) {
    set(node, "QueryId", value.query_id_);
    set(node, "WorkOrderId", value.work_order_id_);
    set(node, "BatchId", value.batch_id_);
    set(node, "BoardId", value.board_id_);
    set(node, "BoardIdCreatedBy", value.board_id_created_by_);
    set(node, "FailedBoard", value.failed_board_);
    set(node, "ProductTypeId", value.product_type_id_);
    set(node, "FlippedBoard", value.flipped_board_);
    set(node, "TopBarcode", value.top_barcode_);
    set(node, "BottomBarcode", value.bottom_barcode_);
    set(node, "Length", value.length_);
    set(node, "Width", value.width_);
    set(node, "Thickness", value.thickness_);
    set(node, "ConveyorSpeed", value.conveyor_speed_);
    set(node, "TopClearanceHeight", value.top_clearance_height_);
    set(node, "BottomClearanceHeight", value.bottom_clearance_height_);
    set(node, "Weight", value.weight_);
    set(node, "Route", value.route_);
    if (include_sub_boards) { set(node, value.sub_boards_); }
}

void write(pugi::xml_node node, const reply_work_order_info& value) {
    set(node, "WorkOrderId", value.work_order_id_);
    set(node, "BatchId", value.batch_id_);
    set(node, "Status", value.status_);
}

void write(pugi::xml_node node, const command& value) { set(node, "Command", value.command_); }

void write(pugi::xml_node, const query_hermes_capabilities&) {}

void write(pugi::xml_node node, const send_hermes_capabilities& value) {
    set(node, value.optional_messages_);
    set(node, value.attributes_);
}

/// @brief Writes @p message into @p node, leaving the SubBoards list out when asked to.
void write_any(pugi::xml_node node, const any_message& message, bool include_sub_boards) {
    std::visit(
        [&](const auto& value) {
            using message_type = std::remove_cvref_t<decltype(value)>;
            if constexpr (requires { write(node, value, include_sub_boards); }) {
                write(node, value, include_sub_boards);
            }
            else { write(node, value); }
        },
        message
    );
}

//==============================================================================
// messages: read
//==============================================================================

void read(pugi::xml_node node, check_alive& value, error_info& error) {
    get(node, "Type", value.type_, error);
    get(node, "Id", value.id_, error);
}

void read(pugi::xml_node node, service_description& value, error_info& error) {
    get(node, "MachineId", value.machine_id_, error);
    get(node, "LaneId", value.lane_id_, error);
    get(node, "InterfaceId", value.interface_id_, error);
    get(node, "Version", value.version_, error);
    get(node, value.supported_features_, error);
}

void read(pugi::xml_node node, notification& value, error_info& error) {
    get(node, "NotificationCode", value.notification_code_, error);
    get(node, "Severity", value.severity_, error);
    get(node, "Description", value.description_, error);
}

void read(pugi::xml_node node, board_available& value, error_info& error) {
    get(node, "BoardId", value.board_id_, error);
    get(node, "BoardIdCreatedBy", value.board_id_created_by_, error);
    get(node, "FailedBoard", value.failed_board_, error);
    get(node, "ProductTypeId", value.product_type_id_, error);
    get(node, "FlippedBoard", value.flipped_board_, error);
    get(node, "TopBarcode", value.top_barcode_, error);
    get(node, "BottomBarcode", value.bottom_barcode_, error);
    get(node, "Length", value.length_, error);
    get(node, "Width", value.width_, error);
    get(node, "Thickness", value.thickness_, error);
    get(node, "ConveyorSpeed", value.conveyor_speed_, error);
    get(node, "TopClearanceHeight", value.top_clearance_height_, error);
    get(node, "BottomClearanceHeight", value.bottom_clearance_height_, error);
    get(node, "Weight", value.weight_, error);
    get(node, "WorkOrderId", value.work_order_id_, error);
    get(node, "BatchId", value.batch_id_, error);
    get(node, "Route", value.route_, error);
    get(node, "Action", value.action_, error);
    get(node, value.sub_boards_, error);
}

void read(pugi::xml_node, revoke_board_available&, error_info&) {}

void read(pugi::xml_node node, machine_ready& value, error_info& error) {
    get(node, "FailedBoard", value.failed_board_, error);
    get(node, "ForecastId", value.forecast_id_, error);
    get(node, "BoardId", value.board_id_, error);
    get(node, "ProductTypeId", value.product_type_id_, error);
    get(node, "FlippedBoard", value.flipped_board_, error);
    get(node, "Length", value.length_, error);
    get(node, "Width", value.width_, error);
    get(node, "Thickness", value.thickness_, error);
    get(node, "ConveyorSpeed", value.conveyor_speed_, error);
    get(node, "TopClearanceHeight", value.top_clearance_height_, error);
    get(node, "BottomClearanceHeight", value.bottom_clearance_height_, error);
    get(node, "Weight", value.weight_, error);
    get(node, "WorkOrderId", value.work_order_id_, error);
    get(node, "BatchId", value.batch_id_, error);
}

void read(pugi::xml_node, revoke_machine_ready&, error_info&) {}

void read(pugi::xml_node node, start_transport& value, error_info& error) {
    get(node, "BoardId", value.board_id_, error);
    get(node, "ConveyorSpeed", value.conveyor_speed_, error);
}

void read(pugi::xml_node node, stop_transport& value, error_info& error) {
    get(node, "TransferState", value.transfer_state_, error);
    get(node, "BoardId", value.board_id_, error);
}

void read(pugi::xml_node node, transport_finished& value, error_info& error) {
    get(node, "TransferState", value.transfer_state_, error);
    get(node, "BoardId", value.board_id_, error);
}

void read(pugi::xml_node node, set_configuration& value, error_info& error) {
    get(node, "MachineId", value.machine_id_, error);
    get(node, "SupervisorySystemPort", value.supervisory_system_port_, error);
    get_list(
        node,
        "UpstreamConfigurations",
        "UpstreamConfiguration",
        value.upstream_configurations_,
        error,
        [](pugi::xml_node entry, upstream_configuration& item, error_info& e) { get(entry, item, e); }
    );
    get_list(
        node,
        "DownstreamConfigurations",
        "DownstreamConfiguration",
        value.downstream_configurations_,
        error,
        [](pugi::xml_node entry, downstream_configuration& item, error_info& e) { get(entry, item, e); }
    );
}

void read(pugi::xml_node, get_configuration&, error_info&) {}

void read(pugi::xml_node node, current_configuration& value, error_info& error) {
    get(node, "MachineId", value.machine_id_, error);
    get(node, "SupervisorySystemPort", value.supervisory_system_port_, error);
    get_list(
        node,
        "UpstreamConfigurations",
        "UpstreamConfiguration",
        value.upstream_configurations_,
        error,
        [](pugi::xml_node entry, upstream_configuration& item, error_info& e) { get(entry, item, e); }
    );
    get_list(
        node,
        "DownstreamConfigurations",
        "DownstreamConfiguration",
        value.downstream_configurations_,
        error,
        [](pugi::xml_node entry, downstream_configuration& item, error_info& e) { get(entry, item, e); }
    );
}

void read(pugi::xml_node node, board_forecast& value, error_info& error) {
    get(node, "ForecastId", value.forecast_id_, error);
    get(node, "TimeUntilAvailable", value.time_until_available_, error);
    get(node, "BoardId", value.board_id_, error);
    get(node, "BoardIdCreatedBy", value.board_id_created_by_, error);
    get(node, "FailedBoard", value.failed_board_, error);
    get(node, "ProductTypeId", value.product_type_id_, error);
    get(node, "FlippedBoard", value.flipped_board_, error);
    get(node, "TopBarcode", value.top_barcode_, error);
    get(node, "BottomBarcode", value.bottom_barcode_, error);
    get(node, "Length", value.length_, error);
    get(node, "Width", value.width_, error);
    get(node, "Thickness", value.thickness_, error);
    get(node, "ConveyorSpeed", value.conveyor_speed_, error);
    get(node, "TopClearanceHeight", value.top_clearance_height_, error);
    get(node, "BottomClearanceHeight", value.bottom_clearance_height_, error);
    get(node, "Weight", value.weight_, error);
    get(node, "WorkOrderId", value.work_order_id_, error);
    get(node, "BatchId", value.batch_id_, error);
}

void read(pugi::xml_node node, query_board_info& value, error_info& error) {
    get(node, "TopBarcode", value.top_barcode_, error);
    get(node, "BottomBarcode", value.bottom_barcode_, error);
}

void read(pugi::xml_node node, send_board_info& value, error_info& error) {
    get(node, "BoardId", value.board_id_, error);
    get(node, "BoardIdCreatedBy", value.board_id_created_by_, error);
    get(node, "FailedBoard", value.failed_board_, error);
    get(node, "ProductTypeId", value.product_type_id_, error);
    get(node, "FlippedBoard", value.flipped_board_, error);
    get(node, "TopBarcode", value.top_barcode_, error);
    get(node, "BottomBarcode", value.bottom_barcode_, error);
    get(node, "Length", value.length_, error);
    get(node, "Width", value.width_, error);
    get(node, "Thickness", value.thickness_, error);
    get(node, "TopClearanceHeight", value.top_clearance_height_, error);
    get(node, "BottomClearanceHeight", value.bottom_clearance_height_, error);
    get(node, "Weight", value.weight_, error);
    get(node, "WorkOrderId", value.work_order_id_, error);
    get(node, "BatchId", value.batch_id_, error);
    get(node, "Route", value.route_, error);
    get(node, "Action", value.action_, error);
    get(node, value.sub_boards_, error);
}

void read(pugi::xml_node node, supervisory_service_description& value, error_info& error) {
    get(node, "SystemId", value.system_id_, error);
    get(node, "Version", value.version_, error);
    get(node, value.supported_features_, error);
}

void read(pugi::xml_node node, board_arrived& value, error_info& error) {
    get(node, "MachineId", value.machine_id_, error);
    get(node, "UpstreamLaneId", value.upstream_lane_id_, error);
    get(node, "UpstreamInterfaceId", value.upstream_interface_id_, error);
    get(node, "MagazineId", value.magazine_id_, error);
    get(node, "SlotId", value.slot_id_, error);
    get(node, "BoardTransfer", value.board_transfer_, error);
    get(node, "BoardId", value.board_id_, error);
    get(node, "BoardIdCreatedBy", value.board_id_created_by_, error);
    get(node, "FailedBoard", value.failed_board_, error);
    get(node, "ProductTypeId", value.product_type_id_, error);
    get(node, "FlippedBoard", value.flipped_board_, error);
    get(node, "TopBarcode", value.top_barcode_, error);
    get(node, "BottomBarcode", value.bottom_barcode_, error);
    get(node, "Length", value.length_, error);
    get(node, "Width", value.width_, error);
    get(node, "Thickness", value.thickness_, error);
    get(node, "ConveyorSpeed", value.conveyor_speed_, error);
    get(node, "TopClearanceHeight", value.top_clearance_height_, error);
    get(node, "BottomClearanceHeight", value.bottom_clearance_height_, error);
    get(node, "Weight", value.weight_, error);
    get(node, "WorkOrderId", value.work_order_id_, error);
    get(node, "BatchId", value.batch_id_, error);
    get(node, "Route", value.route_, error);
    get(node, "Action", value.action_, error);
    get(node, value.sub_boards_, error);
}

void read(pugi::xml_node node, board_departed& value, error_info& error) {
    get(node, "MachineId", value.machine_id_, error);
    get(node, "DownstreamLaneId", value.downstream_lane_id_, error);
    get(node, "DownstreamInterfaceId", value.downstream_interface_id_, error);
    get(node, "MagazineId", value.magazine_id_, error);
    get(node, "SlotId", value.slot_id_, error);
    get(node, "BoardTransfer", value.board_transfer_, error);
    get(node, "BoardId", value.board_id_, error);
    get(node, "BoardIdCreatedBy", value.board_id_created_by_, error);
    get(node, "FailedBoard", value.failed_board_, error);
    get(node, "ProductTypeId", value.product_type_id_, error);
    get(node, "FlippedBoard", value.flipped_board_, error);
    get(node, "TopBarcode", value.top_barcode_, error);
    get(node, "BottomBarcode", value.bottom_barcode_, error);
    get(node, "Length", value.length_, error);
    get(node, "Width", value.width_, error);
    get(node, "Thickness", value.thickness_, error);
    get(node, "ConveyorSpeed", value.conveyor_speed_, error);
    get(node, "TopClearanceHeight", value.top_clearance_height_, error);
    get(node, "BottomClearanceHeight", value.bottom_clearance_height_, error);
    get(node, "Weight", value.weight_, error);
    get(node, "WorkOrderId", value.work_order_id_, error);
    get(node, "BatchId", value.batch_id_, error);
    get(node, "Route", value.route_, error);
    get(node, "Action", value.action_, error);
    get(node, value.sub_boards_, error);
}

void read(pugi::xml_node node, query_work_order_info& value, error_info& error) {
    get(node, "QueryId", value.query_id_, error);
    get(node, "MachineId", value.machine_id_, error);
    get(node, "MagazineId", value.magazine_id_, error);
    get(node, "SlotId", value.slot_id_, error);
    get(node, "Barcode", value.barcode_, error);
    get(node, "WorkOrderId", value.work_order_id_, error);
    get(node, "BatchId", value.batch_id_, error);
}

void read(pugi::xml_node node, send_work_order_info& value, error_info& error) {
    get(node, "QueryId", value.query_id_, error);
    get(node, "WorkOrderId", value.work_order_id_, error);
    get(node, "BatchId", value.batch_id_, error);
    get(node, "BoardId", value.board_id_, error);
    get(node, "BoardIdCreatedBy", value.board_id_created_by_, error);
    get(node, "FailedBoard", value.failed_board_, error);
    get(node, "ProductTypeId", value.product_type_id_, error);
    get(node, "FlippedBoard", value.flipped_board_, error);
    get(node, "TopBarcode", value.top_barcode_, error);
    get(node, "BottomBarcode", value.bottom_barcode_, error);
    get(node, "Length", value.length_, error);
    get(node, "Width", value.width_, error);
    get(node, "Thickness", value.thickness_, error);
    get(node, "ConveyorSpeed", value.conveyor_speed_, error);
    get(node, "TopClearanceHeight", value.top_clearance_height_, error);
    get(node, "BottomClearanceHeight", value.bottom_clearance_height_, error);
    get(node, "Weight", value.weight_, error);
    get(node, "Route", value.route_, error);
    get(node, value.sub_boards_, error);
}

void read(pugi::xml_node node, reply_work_order_info& value, error_info& error) {
    get(node, "WorkOrderId", value.work_order_id_, error);
    get(node, "BatchId", value.batch_id_, error);
    get(node, "Status", value.status_, error);
}

void read(pugi::xml_node node, command& value, error_info& error) { get(node, "Command", value.command_, error); }

void read(pugi::xml_node, query_hermes_capabilities&, error_info&) {}

void read(pugi::xml_node node, send_hermes_capabilities& value, error_info& error) {
    get(node, value.optional_messages_, error);
    get(node, value.attributes_, error);
}

/// @brief Reads the body @p node of @p message; returns false when @p tag is not a message this
/// implementation knows.
bool read_message(std::string_view tag, pugi::xml_node node, any_message& message, error_info& error) {
    const auto assign = [&](auto value) {
        read(node, value, error);
        if (error) { return false; }
        message = std::move(value);
        return true;
    };

    if (tag == message_name<check_alive>) { return assign(check_alive {}); }
    if (tag == message_name<service_description>) { return assign(service_description {}); }
    if (tag == message_name<notification>) { return assign(notification {}); }
    if (tag == message_name<board_available>) { return assign(board_available {}); }
    if (tag == message_name<revoke_board_available>) { return assign(revoke_board_available {}); }
    if (tag == message_name<machine_ready>) { return assign(machine_ready {}); }
    if (tag == message_name<revoke_machine_ready>) { return assign(revoke_machine_ready {}); }
    if (tag == message_name<start_transport>) { return assign(start_transport {}); }
    if (tag == message_name<stop_transport>) { return assign(stop_transport {}); }
    if (tag == message_name<transport_finished>) { return assign(transport_finished {}); }
    if (tag == message_name<set_configuration>) { return assign(set_configuration {}); }
    if (tag == message_name<get_configuration>) { return assign(get_configuration {}); }
    if (tag == message_name<current_configuration>) { return assign(current_configuration {}); }
    if (tag == message_name<board_forecast>) { return assign(board_forecast {}); }
    if (tag == message_name<query_board_info>) { return assign(query_board_info {}); }
    if (tag == message_name<send_board_info>) { return assign(send_board_info {}); }
    if (tag == message_name<supervisory_service_description>) { return assign(supervisory_service_description {}); }
    if (tag == message_name<board_arrived>) { return assign(board_arrived {}); }
    if (tag == message_name<board_departed>) { return assign(board_departed {}); }
    if (tag == message_name<query_work_order_info>) { return assign(query_work_order_info {}); }
    if (tag == message_name<send_work_order_info>) { return assign(send_work_order_info {}); }
    if (tag == message_name<reply_work_order_info>) { return assign(reply_work_order_info {}); }
    if (tag == message_name<command>) { return assign(command {}); }
    if (tag == message_name<query_hermes_capabilities>) { return assign(query_hermes_capabilities {}); }
    if (tag == message_name<send_hermes_capabilities>) { return assign(send_hermes_capabilities {}); }
    return false;
}

/// @brief Serializes @p envelope, leaving the SubBoards lists out when @p include_sub_boards is
/// false.
std::string serialize(const envelope& envelope, bool include_sub_boards) {
    pugi::xml_document document;

    auto root = document.append_child("Hermes");
    if (envelope.timestamp_) { root.append_attribute("Timestamp").set_value(envelope.timestamp_->c_str()); }

    const auto tag = message_name_of(envelope.message_);
    auto node = root.append_child(std::string { tag }.c_str());
    write_any(node, envelope.message_, include_sub_boards);

    std::ostringstream stream;
    document.save(stream, "", pugi::format_raw | pugi::format_no_declaration, pugi::encoding_utf8);
    return stream.str();
}

} // namespace

//==============================================================================
// public API
//==============================================================================

std::string format_timestamp(std::chrono::system_clock::time_point time) {
    const auto seconds = std::chrono::time_point_cast<std::chrono::seconds>(time);
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(time - seconds).count();

    const std::time_t raw = std::chrono::system_clock::to_time_t(time);
    std::tm broken_down {};
#ifdef _WIN32
    gmtime_s(&broken_down, &raw);
#else
    gmtime_r(&raw, &broken_down);
#endif

    std::ostringstream stream;
    stream << std::put_time(&broken_down, "%Y-%m-%dT%H:%M:%S") << '.' << std::setw(3) << std::setfill('0')
           << milliseconds;
    return stream.str();
}

std::string timestamp_now() { return format_timestamp(std::chrono::system_clock::now()); }

std::string to_xml(const any_message& message) {
    auto document = serialize(envelope { std::nullopt, message }, true);
    if (document.size() <= MAX_MESSAGE_SIZE) { return document; }
    return serialize(envelope { std::nullopt, message }, false);
}

std::string to_xml(const envelope& envelope) {
    auto document = serialize(envelope, true);
    if (document.size() <= MAX_MESSAGE_SIZE) { return document; }
    return serialize(envelope, false);
}

std::expected<envelope, error_info> parse(std::string_view xml) {
    pugi::xml_document document;
    const auto result = document.load_buffer(xml.data(), xml.size(), pugi::parse_default, pugi::encoding_utf8);
    if (!result) {
        return std::unexpected(
            error_info { error_code::MALFORMED,
                         std::string { result.description() } + " at offset " + std::to_string(result.offset) }
        );
    }

    const auto root = document.document_element();
    if (!root) { return std::unexpected(error_info { error_code::MALFORMED, "no root element" }); }
    if (std::string_view { root.name() } != "Hermes") {
        return std::unexpected(error_info { error_code::MALFORMED, "root element is not Hermes" });
    }

    pugi::xml_node body;
    for (auto child = root.first_child(); child; child = child.next_sibling()) {
        if (child.type() != pugi::node_element) { continue; }
        if (body) {
            return std::unexpected(error_info { error_code::MALFORMED, "more than one message in one envelope" });
        }
        body = child;
    }
    if (!body) { return std::unexpected(error_info { error_code::MALFORMED, "empty envelope" }); }

    envelope envelope { std::nullopt, any_message { check_alive {} } };
    if (const auto timestamp = root.attribute("Timestamp")) { envelope.timestamp_ = timestamp.value(); }

    error_info error;
    if (!read_message(body.name(), body, envelope.message_, error)) {
        if (!error) { return std::unexpected(error_info { error_code::UNSUPPORTED, body.name() }); }
        return std::unexpected(error);
    }
    if (error) { return std::unexpected(error); }

    return envelope;
}

} // namespace ba7lya::hermes
