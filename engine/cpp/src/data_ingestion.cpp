#include "data_ingestion.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <algorithm>
#include <cctype>
#include <cmath>

static std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c){ return std::tolower(c); });
    return s;
}

static std::string trim(const std::string& s) {
    std::size_t start = s.find_first_not_of(" \t\r\n\"");
    if (start == std::string::npos) return "";
    std::size_t end = s.find_last_not_of(" \t\r\n\"");
    return s.substr(start, end - start + 1);
}

std::vector<std::string> DataIngestionEngine::splitLine(const std::string& line, char delim) {
    std::vector<std::string> tokens;
    std::string token;
    bool in_quotes = false;
    for (char c : line) {
        if (c == '"') {
            in_quotes = !in_quotes;
        } else if (c == delim && !in_quotes) {
            tokens.push_back(trim(token));
            token.clear();
        } else {
            token += c;
        }
    }
    tokens.push_back(trim(token));
    return tokens;
}

int DataIngestionEngine::findColumn(const std::vector<std::string>& headers,
                                     const std::initializer_list<std::string>& names) {
    for (int i = 0; i < static_cast<int>(headers.size()); ++i) {
        std::string h = toLower(trim(headers[i]));
        for (const auto& n : names) {
            if (h == n) return i;
        }
    }
    return -1;
}

bool DataIngestionEngine::parseLine(const std::string& line,
                                     int ts_col, int open_col, int high_col,
                                     int low_col, int close_col, int vol_col,
                                     std::string& ts_out,
                                     double& open_out, double& high_out,
                                     double& low_out, double& close_out,
                                     double& vol_out) {
    auto tokens = splitLine(line, ',');
    int max_col = std::max({ts_col, open_col, high_col, low_col, close_col, vol_col});
    if (max_col < 0 || static_cast<int>(tokens.size()) <= max_col) return false;

    auto parseDouble = [&](int col, double& out) -> bool {
        if (col < 0) { out = 0.0; return true; }
        const std::string& s = tokens[static_cast<std::size_t>(col)];
        if (s.empty() || s == "-" || s == "null" || s == "nan" || s == "N/A") return false;
        try {
            std::size_t pos;
            out = std::stod(s, &pos);
            if (pos != s.size()) return false;
            if (!std::isfinite(out)) return false;
        } catch (...) { return false; }
        return true;
    };

    if (ts_col < 0 || static_cast<int>(tokens.size()) <= ts_col) return false;
    ts_out = tokens[static_cast<std::size_t>(ts_col)];
    if (ts_out.empty()) return false;

    return parseDouble(open_col, open_out) &&
           parseDouble(high_col, high_out) &&
           parseDouble(low_col, low_out) &&
           parseDouble(close_col, close_out) &&
           parseDouble(vol_col, vol_out);
}

OHLCVData DataIngestionEngine::parseStream(std::istream& stream,
                                            MissingValuePolicy policy) {
    OHLCVData data;
    std::string line;

    if (!std::getline(stream, line)) {
        throw std::runtime_error("CSV is empty: no header row found");
    }

    auto headers = splitLine(line, ',');
    if (headers.empty()) {
        throw std::runtime_error("CSV header is empty");
    }

    int ts_col    = findColumn(headers, {"timestamp", "date", "datetime", "time"});
    int open_col  = findColumn(headers, {"open"});
    int high_col  = findColumn(headers, {"high"});
    int low_col   = findColumn(headers, {"low"});
    int close_col = findColumn(headers, {"close", "adj close", "adj_close"});
    int vol_col   = findColumn(headers, {"volume", "vol"});

    if (ts_col < 0)    throw std::runtime_error("CSV missing 'timestamp'/'date' column");
    if (open_col < 0)  throw std::runtime_error("CSV missing 'open' column");
    if (high_col < 0)  throw std::runtime_error("CSV missing 'high' column");
    if (low_col < 0)   throw std::runtime_error("CSV missing 'low' column");
    if (close_col < 0) throw std::runtime_error("CSV missing 'close' column");

    data.reserve(131072);

    std::string last_ts;
    double last_open = 0.0, last_high = 0.0, last_low = 0.0, last_close = 0.0, last_vol = 0.0;
    bool has_prev = false;

    std::size_t row = 1;
    while (std::getline(stream, line)) {
        ++row;
        if (line.empty() || line[0] == '#') continue;

        std::string ts;
        double o, h, l, c, v = 0.0;

        bool ok = parseLine(line, ts_col, open_col, high_col, low_col, close_col, vol_col,
                            ts, o, h, l, c, v);

        if (!ok) {
            if (policy == MissingValuePolicy::DROP) {
                continue;
            } else {
                if (!has_prev) continue;
                if (ts.empty()) ts = last_ts;
                o = last_open; h = last_high; l = last_low; c = last_close; v = last_vol;
            }
        }

        data.timestamp.push_back(ts);
        data.open.push_back(o);
        data.high.push_back(h);
        data.low.push_back(l);
        data.close.push_back(c);
        data.volume.push_back(v);

        last_ts = ts; last_open = o; last_high = h; last_low = l; last_close = c; last_vol = v;
        has_prev = true;
    }

    if (data.size() == 0) {
        throw std::runtime_error("CSV contained no valid data rows after parsing");
    }

    return data;
}

OHLCVData DataIngestionEngine::loadFromCSV(const std::string& filepath,
                                             MissingValuePolicy policy) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open file: " + filepath);
    }
    return parseStream(file, policy);
}

OHLCVData DataIngestionEngine::loadFromString(const std::string& csv_content,
                                               MissingValuePolicy policy) {
    std::istringstream stream(csv_content);
    return parseStream(stream, policy);
}
