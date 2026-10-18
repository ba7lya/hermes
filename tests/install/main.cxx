// SPDX-License-Identifier: MIT
#include <ba7lya/hermes/hermes.hxx>
#include <iostream>
#include <string>

int main() {
    ba7lya::hermes::check_alive message;
    message.type_ = ba7lya::hermes::check_alive_type::PING;
    message.id_ = "consumer";

    const auto document = ba7lya::hermes::to_xml(ba7lya::hermes::any_message { message });
    std::cout << document << '\n';
    return document.find("<CheckAlive") == std::string::npos;
}
