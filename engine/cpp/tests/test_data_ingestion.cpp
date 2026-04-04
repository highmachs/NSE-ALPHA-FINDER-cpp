#include "test_runner.hpp"
#include "data_ingestion.hpp"

#include <stdexcept>

static const char* VALID_CSV =
    "timestamp,open,high,low,close,volume\n"
    "2023-01-01,100.0,105.0,99.0,103.0,10000\n"
    "2023-01-02,103.0,108.0,102.0,107.0,12000\n"
    "2023-01-03,107.0,110.0,106.0,109.0,11000\n"
    "2023-01-04,109.0,112.0,108.0,111.0,9500\n"
    "2023-01-05,111.0,115.0,110.0,114.0,13000\n";

static const char* MALFORMED_ROW_CSV =
    "timestamp,open,high,low,close,volume\n"
    "2023-01-01,100.0,105.0,99.0,103.0,10000\n"
    "2023-01-02,BAD,108.0,102.0,107.0,12000\n"
    "2023-01-03,107.0,110.0,106.0,109.0,11000\n";

static const char* MISSING_COL_CSV =
    "timestamp,open,high,low,volume\n"
    "2023-01-01,100.0,105.0,99.0,10000\n";

static const char* ALT_HEADER_CSV =
    "date,open,high,low,adj close,vol\n"
    "2023-01-01,100.0,105.0,99.0,103.0,10000\n"
    "2023-01-02,103.0,108.0,102.0,107.0,12000\n";

static const char* FORWARD_FILL_CSV =
    "timestamp,open,high,low,close,volume\n"
    "2023-01-01,100.0,105.0,99.0,103.0,10000\n"
    "2023-01-02,BAD,108.0,102.0,107.0,12000\n"
    "2023-01-03,107.0,110.0,106.0,109.0,11000\n";

void runDataIngestionTests() {
    test::suite("DataIngestionEngine");

    {
        auto data = DataIngestionEngine::loadFromString(VALID_CSV);
        test::check(data.size() == 5, "valid CSV: row count == 5");
        test::near(data.close[0], 103.0, 1e-9, "valid CSV: close[0] == 103.0");
        test::near(data.close[4], 114.0, 1e-9, "valid CSV: close[4] == 114.0");
        test::near(data.open[0],  100.0, 1e-9, "valid CSV: open[0] == 100.0");
        test::near(data.high[2],  110.0, 1e-9, "valid CSV: high[2] == 110.0");
        test::near(data.low[1],   102.0, 1e-9, "valid CSV: low[1] == 102.0");
        test::near(data.volume[3], 9500.0, 1e-9, "valid CSV: volume[3] == 9500");
        test::check(data.timestamp[0] == "2023-01-01", "valid CSV: timestamp[0]");
    }

    {
        auto data = DataIngestionEngine::loadFromString(MALFORMED_ROW_CSV,
                                                         MissingValuePolicy::DROP);
        test::check(data.size() == 2, "DROP policy: malformed row dropped → 2 rows");
    }

    {
        auto data = DataIngestionEngine::loadFromString(FORWARD_FILL_CSV,
                                                         MissingValuePolicy::FORWARD_FILL);
        test::check(data.size() == 3, "FORWARD_FILL: all rows kept");
        test::near(data.open[1], data.open[0], 1e-9, "FORWARD_FILL: open[1] == open[0]");
    }

    {
        bool threw = false;
        try {
            DataIngestionEngine::loadFromString(MISSING_COL_CSV);
        } catch (const std::runtime_error&) {
            threw = true;
        }
        test::check(threw, "missing 'close' column throws");
    }

    {
        auto data = DataIngestionEngine::loadFromString(ALT_HEADER_CSV);
        test::check(data.size() == 2, "alt headers (date/adj close/vol): 2 rows");
        test::near(data.close[0], 103.0, 1e-9, "alt headers: close[0]");
    }

    {
        bool threw = false;
        try {
            DataIngestionEngine::loadFromString("");
        } catch (...) {
            threw = true;
        }
        test::check(threw, "empty string throws");
    }
}
