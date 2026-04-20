#pragma once
// Compatibility shim for std::from_chars on floating-point types.
// libc++ (LLVM) has not implemented FP from_chars as of LLVM 22 — calls are
// marked =delete. This shim falls back to strtod/strtof on libc++ while
// using the standard library on libstdc++ / MSVC STL.

#include <charconv>
#include <cstdlib>
#include <cerrno>
#include <string>
#include <system_error>

namespace htmlayout {

inline std::from_chars_result from_chars_fp(const char* first, const char* last, double& value) {
#if defined(_LIBCPP_VERSION)
    std::string buf(first, last);
    char* end = nullptr;
    errno = 0;
    double v = std::strtod(buf.c_str(), &end);
    if (end == buf.c_str()) {
        return {first, std::errc::invalid_argument};
    }
    if (errno == ERANGE) {
        return {first + (end - buf.c_str()), std::errc::result_out_of_range};
    }
    value = v;
    return {first + (end - buf.c_str()), std::errc{}};
#else
    return std::from_chars(first, last, value);
#endif
}

inline std::from_chars_result from_chars_fp(const char* first, const char* last, float& value) {
#if defined(_LIBCPP_VERSION)
    std::string buf(first, last);
    char* end = nullptr;
    errno = 0;
    float v = std::strtof(buf.c_str(), &end);
    if (end == buf.c_str()) {
        return {first, std::errc::invalid_argument};
    }
    if (errno == ERANGE) {
        return {first + (end - buf.c_str()), std::errc::result_out_of_range};
    }
    value = v;
    return {first + (end - buf.c_str()), std::errc{}};
#else
    return std::from_chars(first, last, value);
#endif
}

} // namespace htmlayout
