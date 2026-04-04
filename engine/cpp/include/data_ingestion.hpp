#pragma once

#include <vector>
#include <string>
#include <sstream>
#include <cstdint>

struct OHLCVData {
    std::vector<std::string> timestamp;
    std::vector<double> open;
    std::vector<double> high;
    std::vector<double> low;
    std::vector<double> close;
    std::vector<double> volume;

    std::size_t size() const noexcept { return close.size(); }

    void reserve(std::size_t n) {
        timestamp.reserve(n);
        open.reserve(n);
        high.reserve(n);
        low.reserve(n);
        close.reserve(n);
        volume.reserve(n);
    }
};

enum class MissingValuePolicy {
    DROP,
    FORWARD_FILL
};

class DataIngestionEngine {
public:
    static OHLCVData loadFromCSV(const std::string& filepath,
                                  MissingValuePolicy policy = MissingValuePolicy::DROP);

    static OHLCVData loadFromString(const std::string& csv_content,
                                     MissingValuePolicy policy = MissingValuePolicy::DROP);

private:
    static OHLCVData parseStream(std::istream& stream,
                                  MissingValuePolicy policy);

    static bool parseLine(const std::string& line,
                          int ts_col, int open_col, int high_col,
                          int low_col, int close_col, int vol_col,
                          std::string& ts_out,
                          double& open_out, double& high_out,
                          double& low_out, double& close_out,
                          double& vol_out);

    static int findColumn(const std::vector<std::string>& headers,
                           const std::initializer_list<std::string>& names);

    static std::vector<std::string> splitLine(const std::string& line, char delim);
};
