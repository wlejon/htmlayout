#include "css/color.h"
#include <unordered_map>
#include <algorithm>
#include <cmath>
#include <charconv>
#include <vector>

namespace htmlayout::css {

namespace {

// CSS named colors (Level 4)
const std::unordered_map<std::string, Color>& namedColors() {
    static const std::unordered_map<std::string, Color> colors = {
        {"transparent",         {0, 0, 0, 0}},
        {"black",               {0, 0, 0, 255}},
        {"white",               {255, 255, 255, 255}},
        {"red",                 {255, 0, 0, 255}},
        {"green",               {0, 128, 0, 255}},
        {"blue",                {0, 0, 255, 255}},
        {"yellow",              {255, 255, 0, 255}},
        {"cyan",                {0, 255, 255, 255}},
        {"aqua",                {0, 255, 255, 255}},
        {"magenta",             {255, 0, 255, 255}},
        {"fuchsia",             {255, 0, 255, 255}},
        {"silver",              {192, 192, 192, 255}},
        {"gray",                {128, 128, 128, 255}},
        {"grey",                {128, 128, 128, 255}},
        {"maroon",              {128, 0, 0, 255}},
        {"olive",               {128, 128, 0, 255}},
        {"lime",                {0, 255, 0, 255}},
        {"teal",                {0, 128, 128, 255}},
        {"navy",                {0, 0, 128, 255}},
        {"purple",              {128, 0, 128, 255}},
        {"orange",              {255, 165, 0, 255}},
        {"pink",                {255, 192, 203, 255}},
        {"brown",               {165, 42, 42, 255}},
        {"coral",               {255, 127, 80, 255}},
        {"crimson",             {220, 20, 60, 255}},
        {"darkblue",            {0, 0, 139, 255}},
        {"darkgreen",           {0, 100, 0, 255}},
        {"darkred",             {139, 0, 0, 255}},
        {"darkgray",            {169, 169, 169, 255}},
        {"darkgrey",            {169, 169, 169, 255}},
        {"darkcyan",            {0, 139, 139, 255}},
        {"darkmagenta",         {139, 0, 139, 255}},
        {"darkorange",          {255, 140, 0, 255}},
        {"darkviolet",          {148, 0, 211, 255}},
        {"deeppink",            {255, 20, 147, 255}},
        {"deepskyblue",         {0, 191, 255, 255}},
        {"dimgray",             {105, 105, 105, 255}},
        {"dimgrey",             {105, 105, 105, 255}},
        {"dodgerblue",          {30, 144, 255, 255}},
        {"firebrick",           {178, 34, 34, 255}},
        {"forestgreen",         {34, 139, 34, 255}},
        {"gold",                {255, 215, 0, 255}},
        {"goldenrod",           {218, 165, 32, 255}},
        {"greenyellow",         {173, 255, 47, 255}},
        {"hotpink",             {255, 105, 180, 255}},
        {"indianred",           {205, 92, 92, 255}},
        {"indigo",              {75, 0, 130, 255}},
        {"ivory",               {255, 255, 240, 255}},
        {"khaki",               {240, 230, 140, 255}},
        {"lavender",            {230, 230, 250, 255}},
        {"lawngreen",           {124, 252, 0, 255}},
        {"lightblue",           {173, 216, 230, 255}},
        {"lightcoral",          {240, 128, 128, 255}},
        {"lightcyan",           {224, 255, 255, 255}},
        {"lightgray",           {211, 211, 211, 255}},
        {"lightgrey",           {211, 211, 211, 255}},
        {"lightgreen",          {144, 238, 144, 255}},
        {"lightpink",           {255, 182, 193, 255}},
        {"lightyellow",         {255, 255, 224, 255}},
        {"limegreen",           {50, 205, 50, 255}},
        {"linen",               {250, 240, 230, 255}},
        {"mediumblue",          {0, 0, 205, 255}},
        {"midnightblue",        {25, 25, 112, 255}},
        {"mintcream",           {245, 255, 250, 255}},
        {"mistyrose",           {255, 228, 225, 255}},
        {"moccasin",            {255, 228, 181, 255}},
        {"oldlace",             {253, 245, 230, 255}},
        {"olivedrab",           {107, 142, 35, 255}},
        {"orangered",           {255, 69, 0, 255}},
        {"orchid",              {218, 112, 214, 255}},
        {"papayawhip",          {255, 239, 213, 255}},
        {"peachpuff",           {255, 218, 185, 255}},
        {"peru",                {205, 133, 63, 255}},
        {"plum",                {221, 160, 221, 255}},
        {"powderblue",          {176, 224, 230, 255}},
        {"rebeccapurple",       {102, 51, 153, 255}},
        {"rosybrown",           {188, 143, 143, 255}},
        {"royalblue",           {65, 105, 225, 255}},
        {"saddlebrown",         {139, 69, 19, 255}},
        {"salmon",              {250, 128, 114, 255}},
        {"sandybrown",          {244, 164, 96, 255}},
        {"seagreen",            {46, 139, 87, 255}},
        {"seashell",            {255, 245, 238, 255}},
        {"sienna",              {160, 82, 45, 255}},
        {"skyblue",             {135, 206, 235, 255}},
        {"slateblue",           {106, 90, 205, 255}},
        {"slategray",           {112, 128, 144, 255}},
        {"slategrey",           {112, 128, 144, 255}},
        {"snow",                {255, 250, 250, 255}},
        {"springgreen",         {0, 255, 127, 255}},
        {"steelblue",           {70, 130, 180, 255}},
        {"tan",                 {210, 180, 140, 255}},
        {"thistle",             {216, 191, 216, 255}},
        {"tomato",              {255, 99, 71, 255}},
        {"turquoise",           {64, 224, 208, 255}},
        {"violet",              {238, 130, 238, 255}},
        {"wheat",               {245, 222, 179, 255}},
        {"whitesmoke",          {245, 245, 245, 255}},
        {"yellowgreen",         {154, 205, 50, 255}},
        {"aliceblue",           {240, 248, 255, 255}},
        {"antiquewhite",        {250, 235, 215, 255}},
        {"azure",               {240, 255, 255, 255}},
        {"beige",               {245, 245, 220, 255}},
        {"bisque",              {255, 228, 196, 255}},
        {"blanchedalmond",      {255, 235, 205, 255}},
        {"blueviolet",          {138, 43, 226, 255}},
        {"burlywood",           {222, 184, 135, 255}},
        {"cadetblue",           {95, 158, 160, 255}},
        {"chartreuse",          {127, 255, 0, 255}},
        {"chocolate",           {210, 105, 30, 255}},
        {"cornflowerblue",      {100, 149, 237, 255}},
        {"cornsilk",            {255, 248, 220, 255}},
        {"darkgoldenrod",       {184, 134, 11, 255}},
        {"darkkhaki",           {189, 183, 107, 255}},
        {"darkolivegreen",      {85, 107, 47, 255}},
        {"darkorchid",          {153, 50, 204, 255}},
        {"darksalmon",          {233, 150, 122, 255}},
        {"darkseagreen",        {143, 188, 143, 255}},
        {"darkslateblue",       {72, 61, 139, 255}},
        {"darkslategray",       {47, 79, 79, 255}},
        {"darkslategrey",       {47, 79, 79, 255}},
        {"darkturquoise",       {0, 206, 209, 255}},
        {"floralwhite",         {255, 250, 240, 255}},
        {"gainsboro",           {220, 220, 220, 255}},
        {"ghostwhite",          {248, 248, 255, 255}},
        {"honeydew",            {240, 255, 240, 255}},
        {"lavenderblush",       {255, 240, 245, 255}},
        {"lemonchiffon",        {255, 250, 205, 255}},
        {"lightsalmon",         {255, 160, 122, 255}},
        {"lightseagreen",       {32, 178, 170, 255}},
        {"lightskyblue",        {135, 206, 250, 255}},
        {"lightslategray",      {119, 136, 153, 255}},
        {"lightslategrey",      {119, 136, 153, 255}},
        {"lightsteelblue",      {176, 196, 222, 255}},
        {"mediumaquamarine",    {102, 205, 170, 255}},
        {"mediumorchid",        {186, 85, 211, 255}},
        {"mediumpurple",        {147, 111, 219, 255}},
        {"mediumseagreen",      {60, 179, 113, 255}},
        {"mediumslateblue",     {123, 104, 238, 255}},
        {"mediumspringgreen",   {0, 250, 154, 255}},
        {"mediumturquoise",     {72, 209, 204, 255}},
        {"mediumvioletred",     {199, 21, 133, 255}},
        {"navajowhite",         {255, 222, 173, 255}},
        {"palegoldenrod",       {238, 232, 170, 255}},
        {"palegreen",           {152, 251, 152, 255}},
        {"paleturquoise",       {175, 238, 238, 255}},
        {"palevioletred",       {219, 112, 147, 255}},
        {"lightgoldenrodyellow",{250, 250, 210, 255}},
        {"currentcolor",        {0, 0, 0, 255}},  // fallback
    };
    return colors;
}

std::string toLowerStr(const std::string& s) {
    std::string r = s;
    for (auto& c : r) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return r;
}

int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

uint8_t clampByte(int v) {
    return static_cast<uint8_t>(std::max(0, std::min(255, v)));
}

uint8_t clampByte(float v) {
    return static_cast<uint8_t>(std::max(0.0f, std::min(255.0f, std::round(v))));
}

// Parse a number from string, returns 0 on failure
float parseNum(const std::string& s) {
    float v = 0;
    std::string trimmed = s;
    while (!trimmed.empty() && trimmed.front() == ' ') trimmed.erase(0, 1);
    while (!trimmed.empty() && trimmed.back() == ' ') trimmed.pop_back();
    // Remove % sign for percentage handling later
    bool isPercent = false;
    if (!trimmed.empty() && trimmed.back() == '%') {
        trimmed.pop_back();
        isPercent = true;
    }
    auto [ptr, ec] = std::from_chars(trimmed.data(), trimmed.data() + trimmed.size(), v);
    if (ec != std::errc()) return 0;
    if (isPercent) v = v * 255.0f / 100.0f;
    return v;
}

bool isPercentStr(const std::string& s) {
    std::string t = s;
    while (!t.empty() && t.front() == ' ') t.erase(0, 1);
    while (!t.empty() && t.back() == ' ') t.pop_back();
    return !t.empty() && t.back() == '%';
}

// HSL to RGB conversion
Color hslToRgb(float h, float s, float l, float a) {
    // h in degrees, s and l in 0-1 range
    h = std::fmod(h, 360.0f);
    if (h < 0) h += 360.0f;
    s = std::max(0.0f, std::min(1.0f, s));
    l = std::max(0.0f, std::min(1.0f, l));

    auto hueToRgb = [](float p, float q, float t) -> float {
        if (t < 0) t += 1;
        if (t > 1) t -= 1;
        if (t < 1.0f/6) return p + (q - p) * 6 * t;
        if (t < 1.0f/2) return q;
        if (t < 2.0f/3) return p + (q - p) * (2.0f/3 - t) * 6;
        return p;
    };

    float r, g, b;
    if (s == 0) {
        r = g = b = l;
    } else {
        float q = l < 0.5f ? l * (1 + s) : l + s - l * s;
        float p = 2 * l - q;
        r = hueToRgb(p, q, h / 360.0f + 1.0f/3);
        g = hueToRgb(p, q, h / 360.0f);
        b = hueToRgb(p, q, h / 360.0f - 1.0f/3);
    }

    return {clampByte(r * 255), clampByte(g * 255), clampByte(b * 255),
            clampByte(a * 255)};
}

// Split "r, g, b" or "r, g, b, a" or "r g b" or "r g b / a" style args
std::vector<std::string> splitColorArgs(const std::string& args) {
    std::vector<std::string> parts;
    std::string current;
    for (size_t i = 0; i < args.size(); i++) {
        char c = args[i];
        if (c == ',' || c == '/') {
            if (!current.empty()) parts.push_back(current);
            current.clear();
        } else if (c == ' ') {
            // For space-separated syntax, check if we already have content
            if (!current.empty()) {
                // Look for slash-alpha syntax: "r g b / a"
                parts.push_back(current);
                current.clear();
            }
        } else {
            current += c;
        }
    }
    if (!current.empty()) parts.push_back(current);
    return parts;
}

} // anonymous namespace

Color parseColor(const std::string& value) {
    if (value.empty()) return {0, 0, 0, 0};

    std::string lower = toLowerStr(value);

    // Trim
    while (!lower.empty() && lower.front() == ' ') lower.erase(0, 1);
    while (!lower.empty() && lower.back() == ' ') lower.pop_back();

    // Named colors
    auto& names = namedColors();
    auto it = names.find(lower);
    if (it != names.end()) return it->second;

    // Hex colors
    if (!lower.empty() && lower[0] == '#') {
        std::string hex = lower.substr(1);
        if (hex.size() == 3) {
            // #RGB -> #RRGGBB
            return {
                static_cast<uint8_t>(hexDigit(hex[0]) * 17),
                static_cast<uint8_t>(hexDigit(hex[1]) * 17),
                static_cast<uint8_t>(hexDigit(hex[2]) * 17),
                255
            };
        }
        if (hex.size() == 4) {
            // #RGBA -> #RRGGBBAA
            return {
                static_cast<uint8_t>(hexDigit(hex[0]) * 17),
                static_cast<uint8_t>(hexDigit(hex[1]) * 17),
                static_cast<uint8_t>(hexDigit(hex[2]) * 17),
                static_cast<uint8_t>(hexDigit(hex[3]) * 17)
            };
        }
        if (hex.size() == 6) {
            return {
                static_cast<uint8_t>(hexDigit(hex[0]) * 16 + hexDigit(hex[1])),
                static_cast<uint8_t>(hexDigit(hex[2]) * 16 + hexDigit(hex[3])),
                static_cast<uint8_t>(hexDigit(hex[4]) * 16 + hexDigit(hex[5])),
                255
            };
        }
        if (hex.size() == 8) {
            return {
                static_cast<uint8_t>(hexDigit(hex[0]) * 16 + hexDigit(hex[1])),
                static_cast<uint8_t>(hexDigit(hex[2]) * 16 + hexDigit(hex[3])),
                static_cast<uint8_t>(hexDigit(hex[4]) * 16 + hexDigit(hex[5])),
                static_cast<uint8_t>(hexDigit(hex[6]) * 16 + hexDigit(hex[7]))
            };
        }
    }

    // rgb() / rgba()
    if (lower.size() > 4 && (lower.substr(0, 4) == "rgb(" || lower.substr(0, 5) == "rgba(")) {
        size_t start = lower.find('(');
        size_t end = lower.rfind(')');
        if (start != std::string::npos && end != std::string::npos && end > start) {
            std::string args = lower.substr(start + 1, end - start - 1);
            auto parts = splitColorArgs(args);
            if (parts.size() >= 3) {
                float r = parseNum(parts[0]);
                float g = parseNum(parts[1]);
                float b = parseNum(parts[2]);
                float a = parts.size() >= 4 ? parseNum(parts[3]) : 255.0f;
                // If alpha is in 0-1 range (not percent), scale it
                if (parts.size() >= 4 && !isPercentStr(parts[3])) {
                    float rawA = 0;
                    std::string trimA = parts[3];
                    while (!trimA.empty() && trimA.front() == ' ') trimA.erase(0, 1);
                    auto [p, e] = std::from_chars(trimA.data(), trimA.data() + trimA.size(), rawA);
                    if (e == std::errc()) {
                        if (rawA <= 1.0f) a = rawA * 255.0f;
                        else a = rawA;
                    }
                }
                return {clampByte(r), clampByte(g), clampByte(b), clampByte(a)};
            }
        }
    }

    // hsl() / hsla()
    if (lower.size() > 4 && (lower.substr(0, 4) == "hsl(" || lower.substr(0, 5) == "hsla(")) {
        size_t start = lower.find('(');
        size_t end = lower.rfind(')');
        if (start != std::string::npos && end != std::string::npos && end > start) {
            std::string args = lower.substr(start + 1, end - start - 1);
            auto parts = splitColorArgs(args);
            if (parts.size() >= 3) {
                // h in degrees, s and l as percentages
                float h = 0;
                std::string hStr = parts[0];
                while (!hStr.empty() && hStr.front() == ' ') hStr.erase(0, 1);
                while (!hStr.empty() && hStr.back() == ' ') hStr.pop_back();
                // Remove deg suffix if present
                if (hStr.size() > 3 && hStr.substr(hStr.size() - 3) == "deg") {
                    hStr = hStr.substr(0, hStr.size() - 3);
                }
                std::from_chars(hStr.data(), hStr.data() + hStr.size(), h);

                // s and l as percentages
                float s = 0, l = 0;
                std::string sStr = parts[1], lStr = parts[2];
                while (!sStr.empty() && sStr.front() == ' ') sStr.erase(0, 1);
                while (!sStr.empty() && sStr.back() == ' ') sStr.pop_back();
                if (!sStr.empty() && sStr.back() == '%') sStr.pop_back();
                std::from_chars(sStr.data(), sStr.data() + sStr.size(), s);
                s /= 100.0f;

                while (!lStr.empty() && lStr.front() == ' ') lStr.erase(0, 1);
                while (!lStr.empty() && lStr.back() == ' ') lStr.pop_back();
                if (!lStr.empty() && lStr.back() == '%') lStr.pop_back();
                std::from_chars(lStr.data(), lStr.data() + lStr.size(), l);
                l /= 100.0f;

                float a = 1.0f;
                if (parts.size() >= 4) {
                    std::string aStr = parts[3];
                    while (!aStr.empty() && aStr.front() == ' ') aStr.erase(0, 1);
                    while (!aStr.empty() && aStr.back() == ' ') aStr.pop_back();
                    if (!aStr.empty() && aStr.back() == '%') {
                        aStr.pop_back();
                        std::from_chars(aStr.data(), aStr.data() + aStr.size(), a);
                        a /= 100.0f;
                    } else {
                        std::from_chars(aStr.data(), aStr.data() + aStr.size(), a);
                    }
                }

                return hslToRgb(h, s, l, a);
            }
        }
    }

    return {0, 0, 0, 0}; // unrecognized
}

} // namespace htmlayout::css
