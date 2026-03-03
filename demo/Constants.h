#ifndef __CONSTANTS_H__
#define __CONSTANTS_H__ 

#include <cstddef>

namespace {
constexpr size_t CACHE_LINE_SIZE = 64;
constexpr size_t LOG_BUFFER_SIZE = 16 * 1024 * 1024; // 16 MB
} // anonymous namespace
#endif // __CONSTANTS_H__
