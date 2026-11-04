#pragma once

// Small text helpers for the FPP commands and the controller's web API, with no FPP dependencies so they can be
// unit tested on any machine (test/test_helpers.cpp).
//
//   base64()     HTTP Basic auth for a controller with an admin password
//   hexColour()  an FPP "color" command argument -> the controller's "RRGGBB"
//   argTrue()    an FPP "bool" command argument

#include <cctype>
#include <cstdint>
#include <string>

namespace n2r {

inline std::string base64(const std::string& in) {
    const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (size_t i = 0; i < in.size(); i += 3) {
        uint32_t v = (uint8_t)in[i] << 16 | (i + 1 < in.size() ? (uint8_t)in[i + 1] << 8 : 0) | (i + 2 < in.size() ? (uint8_t)in[i + 2] : 0);
        out += t[v >> 18 & 63];
        out += t[v >> 12 & 63];
        out += i + 1 < in.size() ? t[v >> 6 & 63] : '=';
        out += i + 2 < in.size() ? t[v & 63] : '=';
    }
    return out;
}

// "#FF8000", "0xff8000" or "FF8000" -> "FF8000"; empty when it isn't a colour.
inline std::string hexColour(std::string s) {
    if (s.rfind("#", 0) == 0) s.erase(0, 1);
    else if (s.rfind("0x", 0) == 0 || s.rfind("0X", 0) == 0) s.erase(0, 2);
    if (s.size() != 6) return "";
    for (char& c : s) {
        if (!isxdigit((unsigned char)c)) return "";
        c = (char)toupper((unsigned char)c);
    }
    return s;
}

// FPP sends a "bool" argument as "true" / "false"; the API and presets may also say 1, on or yes.
inline bool argTrue(std::string s) {
    for (char& c : s) c = (char)tolower((unsigned char)c);
    return s == "true" || s == "1" || s == "on" || s == "yes";
}

}  // namespace n2r
