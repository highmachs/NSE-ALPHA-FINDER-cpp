/**
 * @file data_ingestion.hpp
 * @brief OHLCV data ingestion engine for NSE equity CSV files.
 *
 * Loads historical Open-High-Low-Close-Volume data from CSV files or raw
 * strings into a cache-friendly struct-of-arrays layout. All parsing is
 * done in a single O(n) pass with zero redundant copies.
 *
 * Supported CSV schemas (column names are case-insensitive):
 *   Required : timestamp | date | datetime | time
 *   Required : open
 *   Required : high
 *   Required : low
 *   Required : close | adj close | adj_close
 *   Optional : volume | vol  (defaults to 0 when absent)
 *
 * Data sources: NSE historical CSV, Yahoo Finance (.NS tickers), Stooq.
 */

#pragma once

#include <vector>
#include <string>
#include <sstream>
#include <cstddef>

// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Policy for handling malformed or incomplete rows during ingestion.
 */
enum class MissingValuePolicy {
    DROP,         ///< Silently discard any row that fails validation.
    FORWARD_FILL  ///< Replace bad row with the previous valid row's values.
};

// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Struct-of-arrays container for OHLCV time-series data.
 *
 * Each column is stored as a contiguous std::vector<double> so that
 * indicator loops can walk a single cache line without skipping fields.
 * All column vectors are guaranteed to have identical length after loading.
 *
 * Memory is reserved in powers of two (initial reservation: 131 072 rows)
 * to avoid repeated reallocation during CSV streaming.
 */
struct OHLCVData {
    std::vector<std::string> timestamp; ///< ISO 8601 date strings as parsed from source.
    std::vector<double>      open;      ///< Opening price per bar.
    std::vector<double>      high;      ///< Intrabar high price.
    std::vector<double>      low;       ///< Intrabar low price.
    std::vector<double>      close;     ///< Closing / adjusted-close price.
    std::vector<double>      volume;    ///< Trade volume (shares or contracts).

    /** @return Number of valid rows stored (same for all columns). */
    std::size_t size() const noexcept { return close.size(); }

    /**
     * @brief Pre-allocate all column vectors to avoid repeated reallocation.
     * @param n Expected row count (estimate; can be exceeded at no extra cost).
     */
    void reserve(std::size_t n) {
        timestamp.reserve(n);
        open.reserve(n);
        high.reserve(n);
        low.reserve(n);
        close.reserve(n);
        volume.reserve(n);
    }
};

// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief High-performance CSV ingestion engine for OHLCV data.
 *
 * All public methods are static; no instance is required. The implementation
 * performs a single-pass O(n) parse and writes directly into the returned
 * OHLCVData struct with no intermediate copies.
 *
 * Thread safety: individual calls are stateless and safe to call concurrently
 * on different inputs.
 */
class DataIngestionEngine {
public:
    /**
     * @brief Load OHLCV data from a CSV file on disk.
     *
     * Opens @p filepath with std::ifstream and delegates to parseStream().
     * Throws on any OS-level open failure.
     *
     * @param filepath  Absolute or relative path to the CSV file.
     * @param policy    How to handle rows that fail schema or value validation.
     * @return          Populated OHLCVData; always contains at least 1 row.
     * @throws std::runtime_error  File not found, unreadable, missing columns,
     *                             or no valid rows after filtering.
     */
    static OHLCVData loadFromCSV(const std::string& filepath,
                                  MissingValuePolicy policy = MissingValuePolicy::DROP);

    /**
     * @brief Load OHLCV data from a raw CSV string in memory.
     *
     * Wraps @p csv_content in std::istringstream and delegates to parseStream().
     * Useful for API endpoints receiving CSV as a request body.
     *
     * @param csv_content  Full CSV text including the header row.
     * @param policy       How to handle rows that fail schema or value validation.
     * @return             Populated OHLCVData; always contains at least 1 row.
     * @throws std::runtime_error  Missing columns, empty input, or no valid rows.
     */
    static OHLCVData loadFromString(const std::string& csv_content,
                                     MissingValuePolicy policy = MissingValuePolicy::DROP);

private:
    /** @brief Core parse loop shared by loadFromCSV and loadFromString. */
    static OHLCVData parseStream(std::istream& stream, MissingValuePolicy policy);

    /**
     * @brief Parse one data row into individual OHLCV fields.
     * @return true if all required fields parsed and are finite; false otherwise.
     */
    static bool parseLine(const std::string& line,
                          int ts_col, int open_col, int high_col,
                          int low_col, int close_col, int vol_col,
                          std::string& ts_out,
                          double& open_out, double& high_out,
                          double& low_out, double& close_out,
                          double& vol_out);

    /**
     * @brief Find the first header column whose lowercase name matches any
     *        entry in @p names. Returns -1 if none found.
     */
    static int findColumn(const std::vector<std::string>& headers,
                           const std::initializer_list<std::string>& names);

    /** @brief Split @p line on @p delim, respecting double-quoted fields. */
    static std::vector<std::string> splitLine(const std::string& line, char delim);
};
