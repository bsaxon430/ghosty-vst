// SPDX-License-Identifier: AGPL-3.0-or-later
// Build glue for CloudSeedCore (third_party/cloudseed, MIT): the two things its own build supplies (BUFFER_SIZE, MSVC's strcpy_s).
#pragma once
#include <cstring>
#ifndef BUFFER_SIZE
#define BUFFER_SIZE 1024
#define PHANTOM_CLOUDSEED_DEFINED_BUFFER_SIZE 1
#endif
#ifndef MAX_STR_SIZE
#define MAX_STR_SIZE 32
#endif
#if ! defined (_MSC_VER) && ! defined (strcpy_s)
#define strcpy_s(dst, n, src) std::strncpy ((dst), (src), (n))
#endif
#include <cmath>
#if defined (__GLIBCXX__)   // libstdc++ (the Linux test build) has no std::sinf / sqrtf / expf; Apple's libc++ has them
namespace std { using ::sinf; using ::sqrtf; using ::expf; using ::cosf; using ::powf; }
#endif
