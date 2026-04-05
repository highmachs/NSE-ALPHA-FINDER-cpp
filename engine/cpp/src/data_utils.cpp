/**
 * @file data_utils.cpp
 * @brief Implementation of OHLCV data standardisation and validation utilities.
 *
 * See data_utils.hpp for full API documentation.
 */

#include "data_utils.hpp"
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <regex>
#include <sstream>
#include <stdexcept>

// ─────────────────────────────────────────────────────────────────────────────
// Internal helpers
// ─────────────────────────────────────────────────────────────────────────────

static std::string trimStr(const std::string& s) {
    auto start = std::find_if_not(s.begin(), s.end(),
                                  [](unsigned char c){ return std::isspace(c); });
    auto end   = std::find_if_not(s.rbegin(), s.rend(),
                                  [](unsigned char c){ return std::isspace(c); }).base();
    return (start < end) ? std::string(start, end) : "";
}

static std::string pad2(int n) {
    return (n < 10) ? ("0" + std::to_string(n)) : std::to_string(n);
}

// ─────────────────────────────────────────────────────────────────────────────
// DataUtils::monthAbbrevToNum
// ─────────────────────────────────────────────────────────────────────────────

std::string DataUtils::monthAbbrevToNum(const std::string& mon) {
    static const std::pair<const char*, const char*> table[] = {
        {"jan","01"}, {"feb","02"}, {"mar","03"}, {"apr","04"},
        {"may","05"}, {"jun","06"}, {"jul","07"}, {"aug","08"},
        {"sep","09"}, {"oct","10"}, {"nov","11"}, {"dec","12"}
    };
    std::string lower = mon;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c){ return std::tolower(c); });
    for (auto& [abbrev, num] : table) {
        if (lower.substr(0, 3) == abbrev) return num;
    }
    return "";
}

// ─────────────────────────────────────────────────────────────────────────────
// DataUtils::normaliseOneTimestamp
// ─────────────────────────────────────────────────────────────────────────────

std::string DataUtils::normaliseOneTimestamp(const std::string& ts) {
    std::string s = trimStr(ts);
    if (s.empty()) return ts;

    // Strategy 1: already YYYY-MM-DD[...] — extract date part
    // Covers: "2023-01-05", "2023-01-05 00:00:00", "2023-01-05T15:30:00+05:30"
    if (s.size() >= 10 && s[4] == '-' && s[7] == '-') {
        return s.substr(0, 10);
    }

    // Strategy 2: MM/DD/YYYY (US locale) e.g. "01/05/2023"
    {
        static const std::regex us_re(R"((\d{1,2})/(\d{1,2})/(\d{4}))");
        std::smatch m;
        if (std::regex_search(s, m, us_re)) {
            return m[3].str() + "-" + pad2(std::stoi(m[1])) + "-" + pad2(std::stoi(m[2]));
        }
    }

    // Strategy 3: DD-Mon-YYYY (Bloomberg) e.g. "05-Jan-2023"
    {
        static const std::regex bl_re(R"((\d{1,2})-([A-Za-z]{3})-(\d{4}))");
        std::smatch m;
        if (std::regex_search(s, m, bl_re)) {
            std::string mon = monthAbbrevToNum(m[2].str());
            if (!mon.empty()) {
                return m[3].str() + "-" + mon + "-" + pad2(std::stoi(m[1]));
            }
        }
    }

    // Strategy 4: DD/MM/YYYY (European) e.g. "05/01/2023"
    // Ambiguous with US format; we distinguish by checking if field[0] > 12.
    {
        static const std::regex eu_re(R"((\d{1,2})/(\d{1,2})/(\d{4}))");
        std::smatch m;
        if (std::regex_search(s, m, eu_re) && std::stoi(m[1]) > 12) {
            return m[3].str() + "-" + pad2(std::stoi(m[2])) + "-" + pad2(std::stoi(m[1]));
        }
    }

    return ts; // Unrecognised — return unchanged
}

// ─────────────────────────────────────────────────────────────────────────────
// DataUtils::normaliseTimestamps
// ─────────────────────────────────────────────────────────────────────────────

void DataUtils::normaliseTimestamps(OHLCVData& data) {
    for (auto& ts : data.timestamp) {
        ts = normaliseOneTimestamp(ts);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// DataUtils::validate
// ─────────────────────────────────────────────────────────────────────────────

std::vector<ValidationError> DataUtils::validate(const OHLCVData& data) {
    std::vector<ValidationError> errors;
    const std::size_t n = data.size();

    for (std::size_t i = 0; i < n; ++i) {
        double o = data.open[i], h = data.high[i];
        double l = data.low[i],  c = data.close[i];
        double v = data.volume[i];

        // Price positivity
        if (o <= 0.0) errors.push_back({i, "open",   "open must be > 0"});
        if (h <= 0.0) errors.push_back({i, "high",   "high must be > 0"});
        if (l <= 0.0) errors.push_back({i, "low",    "low must be > 0"});
        if (c <= 0.0) errors.push_back({i, "close",  "close must be > 0"});
        if (v <  0.0) errors.push_back({i, "volume", "volume cannot be negative"});

        // OHLC internal consistency
        if (h < o) errors.push_back({i, "high", "high < open"});
        if (h < c) errors.push_back({i, "high", "high < close"});
        if (l > o) errors.push_back({i, "low",  "low > open"});
        if (l > c) errors.push_back({i, "low",  "low > close"});
        if (h < l) errors.push_back({i, "high", "high < low"});
    }
    return errors;
}

// ─────────────────────────────────────────────────────────────────────────────
// DataUtils::dropInvalidRows
// ─────────────────────────────────────────────────────────────────────────────

OHLCVData DataUtils::dropInvalidRows(const OHLCVData& data) {
    auto errors = validate(data);
    // Build a set of bad row indices
    std::vector<bool> bad(data.size(), false);
    for (auto& e : errors) bad[e.row] = true;

    OHLCVData out;
    out.reserve(data.size());
    for (std::size_t i = 0; i < data.size(); ++i) {
        if (!bad[i]) {
            out.timestamp.push_back(data.timestamp[i]);
            out.open.push_back(data.open[i]);
            out.high.push_back(data.high[i]);
            out.low.push_back(data.low[i]);
            out.close.push_back(data.close[i]);
            out.volume.push_back(data.volume[i]);
        }
    }
    return out;
}
