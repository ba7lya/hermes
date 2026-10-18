// SPDX-License-Identifier: MIT
///
/// @file state_machine_test.cxx
/// @author BA7LYA (1042140025@qq.com)
/// @brief The unit tests of the horizontal state chart of Figure 19.
/// @version 0.1
/// @date 2026-10-18
/// @copyright Copyright (c) 2026
///

#include <gtest/gtest.h>
#include <optional>
#include <string_view>
#include <vector>

#include "horizontal.hxx"
#include "messages.hxx"

namespace {

using namespace ba7lya::hermes;
using namespace ba7lya::hermes::detail;

struct transition {
    channel_state from_ {};
    std::string_view message_;
    std::optional<channel_state> to_;
};

const std::vector<std::string_view> STATE_MESSAGES {
    message_name<service_description>, message_name<board_available>,      message_name<revoke_board_available>,
    message_name<machine_ready>,       message_name<revoke_machine_ready>, message_name<start_transport>,
    message_name<stop_transport>,      message_name<transport_finished>,
};

const std::vector<channel_state> ALL_STATES {
    channel_state::NOT_CONNECTED,
    channel_state::SOCKET_CONNECTED,
    channel_state::SERVICE_DESCRIPTION_DOWNSTREAM,
    channel_state::NOT_AVAILABLE_NOT_READY,
    channel_state::BOARD_AVAILABLE,
    channel_state::MACHINE_READY,
    channel_state::AVAILABLE_AND_READY,
    channel_state::TRANSPORTING,
    channel_state::TRANSPORT_STOPPED,
    channel_state::TRANSPORT_FINISHED,
    channel_state::DISCONNECTED,
};

const std::vector<transition> TRANSITIONS {
    { channel_state::SOCKET_CONNECTED,
     message_name<service_description>,
     channel_state::SERVICE_DESCRIPTION_DOWNSTREAM                                                                                },
    { channel_state::SERVICE_DESCRIPTION_DOWNSTREAM,
     message_name<service_description>,
     channel_state::NOT_AVAILABLE_NOT_READY                                                                                       },

    { channel_state::NOT_AVAILABLE_NOT_READY,        message_name<board_available>,        channel_state::BOARD_AVAILABLE         },
    { channel_state::NOT_AVAILABLE_NOT_READY,        message_name<machine_ready>,          channel_state::MACHINE_READY           },

    { channel_state::BOARD_AVAILABLE,                message_name<revoke_board_available>, channel_state::NOT_AVAILABLE_NOT_READY },
    { channel_state::BOARD_AVAILABLE,                message_name<machine_ready>,          channel_state::AVAILABLE_AND_READY     },

    { channel_state::MACHINE_READY,                  message_name<revoke_machine_ready>,   channel_state::NOT_AVAILABLE_NOT_READY },
    { channel_state::MACHINE_READY,                  message_name<board_available>,        channel_state::AVAILABLE_AND_READY     },
    { channel_state::MACHINE_READY,                  message_name<start_transport>,        channel_state::TRANSPORTING            },

    { channel_state::AVAILABLE_AND_READY,            message_name<revoke_board_available>, channel_state::MACHINE_READY           },
    { channel_state::AVAILABLE_AND_READY,            message_name<revoke_machine_ready>,   channel_state::BOARD_AVAILABLE         },
    { channel_state::AVAILABLE_AND_READY,            message_name<start_transport>,        channel_state::TRANSPORTING            },

    { channel_state::TRANSPORTING,                   message_name<transport_finished>,     channel_state::TRANSPORT_FINISHED      },
    { channel_state::TRANSPORTING,                   message_name<stop_transport>,         channel_state::TRANSPORT_STOPPED       },
    { channel_state::TRANSPORTING,                   message_name<board_available>,        channel_state::TRANSPORTING            },
    { channel_state::TRANSPORTING,                   message_name<revoke_board_available>, channel_state::TRANSPORTING            },

    { channel_state::TRANSPORT_STOPPED,              message_name<transport_finished>,     channel_state::NOT_AVAILABLE_NOT_READY },
    { channel_state::TRANSPORT_STOPPED,              message_name<board_available>,        channel_state::TRANSPORT_STOPPED       },
    { channel_state::TRANSPORT_STOPPED,              message_name<revoke_board_available>, channel_state::TRANSPORT_STOPPED       },

    { channel_state::TRANSPORT_FINISHED,             message_name<stop_transport>,         channel_state::NOT_AVAILABLE_NOT_READY },
};

std::optional<channel_state> expected(channel_state from, std::string_view message) {
    for (const auto& entry : TRANSITIONS) {
        if (entry.from_ == from && entry.message_ == message) { return entry.to_; }
    }
    return std::nullopt;
}

TEST(state_machine, follows_the_state_chart) {
    for (const auto& entry : TRANSITIONS) {
        EXPECT_EQ(next_state(entry.from_, entry.message_), entry.to_)
            << to_string(entry.from_) << " + " << entry.message_;
    }
}

TEST(state_machine, has_no_transition_outside_the_chart) {
    for (const auto state : ALL_STATES) {
        for (const auto message : STATE_MESSAGES) {
            EXPECT_EQ(next_state(state, message), expected(state, message)) << to_string(state) << " + " << message;
        }
    }
}

TEST(state_machine, classifies_the_state_messages) {
    for (const auto message : STATE_MESSAGES) {
        EXPECT_TRUE(needs_transition(message)) << message;
        EXPECT_FALSE(is_anytime_message(message)) << message;
        EXPECT_FALSE(next_state(channel_state::DISCONNECTED, message).has_value()) << message;
    }
}

TEST(state_machine, lets_the_stateless_messages_travel_any_time) {
    for (const auto message : { message_name<notification>,
                                message_name<check_alive>,
                                message_name<command>,
                                message_name<query_board_info>,
                                message_name<send_board_info> }) {
        EXPECT_TRUE(is_anytime_message(message)) << message;
        EXPECT_FALSE(needs_transition(message)) << message;
        for (const auto state : ALL_STATES) { EXPECT_FALSE(next_state(state, message).has_value()) << message; }
    }
}

TEST(state_machine, assigns_every_message_to_the_side_that_may_send_it) {
    // The downstream interface is the board provider, the upstream interface the board receiver.
    const std::vector<std::string_view> downstream_sends {
        message_name<board_available>, message_name<revoke_board_available>, message_name<transport_finished>,
        message_name<board_forecast>,  message_name<send_board_info>,
    };
    const std::vector<std::string_view> upstream_sends {
        message_name<machine_ready>,  message_name<revoke_machine_ready>, message_name<start_transport>,
        message_name<stop_transport>, message_name<query_board_info>,
    };

    for (const auto message : downstream_sends) {
        EXPECT_TRUE(role_may_send(horizontal_role::DOWNSTREAM, message)) << message;
        EXPECT_FALSE(role_may_send(horizontal_role::UPSTREAM, message)) << message;
        EXPECT_TRUE(role_may_receive(horizontal_role::UPSTREAM, message)) << message;
        EXPECT_FALSE(role_may_receive(horizontal_role::DOWNSTREAM, message)) << message;
    }
    for (const auto message : upstream_sends) {
        EXPECT_TRUE(role_may_send(horizontal_role::UPSTREAM, message)) << message;
        EXPECT_FALSE(role_may_send(horizontal_role::DOWNSTREAM, message)) << message;
        EXPECT_TRUE(role_may_receive(horizontal_role::DOWNSTREAM, message)) << message;
        EXPECT_FALSE(role_may_receive(horizontal_role::UPSTREAM, message)) << message;
    }
}

TEST(state_machine, lets_both_sides_send_the_service_description_and_the_stateless_messages) {
    for (const auto message : { message_name<service_description>,
                                message_name<notification>,
                                message_name<check_alive>,
                                message_name<command> }) {
        EXPECT_TRUE(role_may_send(horizontal_role::DOWNSTREAM, message)) << message;
        EXPECT_TRUE(role_may_send(horizontal_role::UPSTREAM, message)) << message;
        EXPECT_TRUE(role_may_receive(horizontal_role::DOWNSTREAM, message)) << message;
        EXPECT_TRUE(role_may_receive(horizontal_role::UPSTREAM, message)) << message;
    }
}

TEST(state_machine, rejects_the_messages_of_the_other_channel) {
    for (const auto message : { message_name<board_arrived>,
                                message_name<query_work_order_info>,
                                message_name<get_configuration>,
                                message_name<send_hermes_capabilities> }) {
        EXPECT_FALSE(role_may_send(horizontal_role::DOWNSTREAM, message)) << message;
        EXPECT_FALSE(role_may_send(horizontal_role::UPSTREAM, message)) << message;
        EXPECT_FALSE(role_may_receive(horizontal_role::DOWNSTREAM, message)) << message;
        EXPECT_FALSE(role_may_receive(horizontal_role::UPSTREAM, message)) << message;
    }
}

} // namespace
