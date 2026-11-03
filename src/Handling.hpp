#pragma once
#include <cstddef>
#include <string>
#include <string_view>

struct HandlingOverlay {
    std::string text;
    std::size_t adjusted = 0;
};
HandlingOverlay tuneSuspension(std::string_view handling, std::string_view vehicles, int percent);
