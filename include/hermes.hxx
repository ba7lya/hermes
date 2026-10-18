// SPDX-License-Identifier: MIT
///
/// @file hermes.hxx
/// @author BA7LYA (1042140025@qq.com)
/// @brief The umbrella header that pulls in the complete public API.
/// @version 0.1
/// @date 2026-10-18
/// @copyright Copyright (c) 2026
///
/// Include only the facade you need when you do not need all of them:
///
///   #include "downstream.hxx"
///
/// and this header when you want everything:
///
///   #include "hermes.hxx"
///

#pragma once

#include "channel.hxx"
#include "configuration_client.hxx"
#include "configuration_service.hxx"
#include "downstream.hxx"
#include "enums.hxx"
#include "error.hxx"
#include "messages.hxx"
#include "settings.hxx"
#include "types.hxx"
#include "upstream.hxx"
#include "version.hxx"
#include "vertical_client.hxx"
#include "vertical_service.hxx"
#include "xml.hxx"
