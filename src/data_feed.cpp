#include "qe/data_feed.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace qe {
namespace {

std::string trim(const std::string& s) {
    auto begin = s.find_first_not_of(" \t\r\n\"");
    if (begin == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\r\n\"");
    return s.substr(begin, end - begin + 1);
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::vector<std::string> split(const std::string& line) {
    std::vector<std::string> out;
    std::stringstream ss(line);
    std::string cell;
    while (std::getline(ss, cell, ',')) out.push_back(trim(cell));
    return out;
}

std::optional<double> parse_double(const std::string& s) {
    if (s.empty()) return std::nullopt;
    try {
        std::size_t used = 0;
        double v = std::stod(s, &used);
        if (used != s.size()) return std::nullopt;
        return v;
    } catch (...) {
        return std::nullopt;
    }
}

}  // namespace

std::vector<Bar> load_csv(const std::string& path, const std::string& symbol) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot open data file: " + path);

    std::string line;
    if (!std::getline(in, line)) throw std::runtime_error("empty data file: " + path);

    std::unordered_map<std::string, std::size_t> col;
    auto header = split(line);
    for (std::size_t i = 0; i < header.size(); ++i) col[lower(header[i])] = i;

    auto require = [&](const std::string& name) {
        auto it = col.find(name);
        if (it == col.end())
            throw std::runtime_error("missing column '" + name + "' in " + path);
        return it->second;
    };
    const std::size_t c_date = require("date");
    const std::size_t c_open = require("open");
    const std::size_t c_high = require("high");
    const std::size_t c_low = require("low");
    const std::size_t c_close = require("close");
    const bool has_volume = col.count("volume") > 0;
    const std::size_t c_volume = has_volume ? col["volume"] : 0;

    std::vector<Bar> bars;
    std::size_t skipped = 0;
    std::size_t line_no = 1;
    while (std::getline(in, line)) {
        ++line_no;
        if (trim(line).empty()) continue;
        auto cells = split(line);
        auto get = [&](std::size_t c) -> std::optional<double> {
            return c < cells.size() ? parse_double(cells[c]) : std::nullopt;
        };
        auto o = get(c_open), h = get(c_high), l = get(c_low), c = get(c_close);
        if (c_date >= cells.size() || !o || !h || !l || !c) {
            ++skipped;
            continue;
        }
        Bar bar;
        bar.symbol = symbol;
        bar.date = cells[c_date];
        bar.open = *o;
        bar.high = *h;
        bar.low = *l;
        bar.close = *c;
        bar.volume = has_volume ? get(c_volume).value_or(0.0) : 0.0;
        if (bar.open <= 0 || bar.close <= 0 || bar.high < bar.low) {
            ++skipped;
            continue;
        }
        bars.push_back(std::move(bar));
    }

    std::stable_sort(bars.begin(), bars.end(),
                     [](const Bar& a, const Bar& b) { return a.date < b.date; });
    for (std::size_t i = 1; i < bars.size(); ++i) {
        if (bars[i].date == bars[i - 1].date)
            throw std::runtime_error("duplicate date " + bars[i].date + " in " + path);
    }
    if (skipped > 0)
        std::cerr << "[data] skipped " << skipped << " incomplete or invalid rows in " << path
                  << "\n";
    if (bars.empty()) throw std::runtime_error("no valid bars in " + path);
    return bars;
}

}  // namespace qe
