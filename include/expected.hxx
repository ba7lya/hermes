// SPDX-License-Identifier: MIT
///
/// @file expected.hxx
/// @author BA7LYA (1042140025@qq.com)
/// @brief Includes <expected> with a compatibility shim for Clang 17 and 18.
/// @version 0.1
/// @date 2026-10-19
/// @copyright Copyright (c) 2026
///

#pragma once

#if defined(__clang__) && __clang_major__ < 19 && __has_include(<bits/c++config.h>)
// Clang 17/18 report __cpp_concepts as 201907L, while libstdc++ gates <expected>
// on 202002L. Raise it only while including the header, then restore the compiler
// value so other libstdc++ headers keep their matching feature set.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wbuiltin-macro-redefined"
#pragma push_macro("__cpp_concepts")
#undef __cpp_concepts
#define __cpp_concepts 202002L
#include <expected>
#undef __cpp_concepts
#pragma pop_macro("__cpp_concepts")
#pragma clang diagnostic pop
#else
#include <expected>
#endif
