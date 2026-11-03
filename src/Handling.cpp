#include "Handling.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <set>

using Text = std::string_view;

static Text trim(Text s) {
    const auto first = s.find_first_not_of(" \t\r");
    if (first == Text::npos) return {};
    return s.substr(first, s.find_last_not_of(" \t\r") - first + 1);
}

template<class Callback> static void lines(Text text, Callback callback) {
    while (!text.empty()) {
        const auto newline = text.find('\n');
        callback(text.substr(0, newline), newline != Text::npos);
        if (newline == Text::npos) break;
        text.remove_prefix(newline + 1);
    }
}

static std::set<Text> roadHandlingIds(Text vehicles) {
    std::set<Text> cars, others;
    bool inCars = false;
    lines(vehicles, [&](Text row, bool) {
        row = trim(row.substr(0, row.find('#')));
        if (row == "cars") { inCars = true; return; }
        if (row == "end") { inCars = false; return; }
        if (!inCars || row.empty()) return;
        std::array<Text, 5> columns;
        for (auto& column : columns) {
            const auto comma = row.find(',');
            if (comma == Text::npos) return;
            column = trim(row.substr(0, comma));
            row.remove_prefix(comma + 1);
        }
        (columns[3] == "car" ? cars : others).insert(columns[4]);
    });
    for (const auto id : others) cars.erase(id);
    return cars;
}

HandlingOverlay tuneSuspension(Text handling, Text vehicles, int percent) {
    HandlingOverlay result;
    result.text.reserve(handling.size());
    const auto cars = roadHandlingIds(vehicles);
    lines(handling, [&](Text row, bool newline) {
        std::array<Text, 36> tokens;
        Text remaining = trim(row.substr(0, row.find(';')));
        std::size_t count = 0;
        while (!remaining.empty() && count < tokens.size()) {
            const auto end = remaining.find_first_of(" \t\r");
            tokens[count++] = remaining.substr(0, end);
            remaining = end == Text::npos ? Text{} : trim(remaining.substr(end));
        }
        const auto append = [&](Text value) {
            result.text.append(value);
            if (newline) result.text.push_back('\n');
        };
        if (percent <= 0 || count != tokens.size() || !remaining.empty() || !cars.contains(tokens[0])) {
            append(row);
            return;
        }
        // SA's native handling row stores low-speed compression damping in
        // column 22. Keep the spring, travel, tyres and drivetrain as authored.
        const auto damping = tokens[22];
        float value = 0;
        const auto parsed = std::from_chars(damping.data(), damping.data() + damping.size(), value);
        if (parsed.ec != std::errc{} || parsed.ptr != damping.data() + damping.size() ||
            !std::isfinite(value) || value <= 0 || value >= 0.25f) {
            append(row);
            return;
        }
        const float adjusted = std::min(0.25f, value * (1.f + std::min(percent, 20) / 100.f));
        char number[32];
        const auto formatted = std::to_chars(std::begin(number), std::end(number), adjusted, std::chars_format::fixed, 6);
        if (formatted.ec != std::errc{}) {
            append(row);
            return;
        }
        const auto offset = static_cast<std::size_t>(damping.data() - row.data());
        result.text.append(row.substr(0, offset));
        result.text.append(number, formatted.ptr);
        append(row.substr(offset + damping.size()));
        ++result.adjusted;
    });
    return result;
}
