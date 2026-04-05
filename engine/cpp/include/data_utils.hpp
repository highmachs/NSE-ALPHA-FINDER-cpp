/**
 * @file data_utils.hpp
 * @brief Data standardisation and validation utilities for OHLCV series.
 *
 * PRD §6.2 requirements:
 *   - Timezone normalisation: strip tz suffix, convert to "YYYY-MM-DD" or
 *     "YYYY-MM-DD HH:MM:SS" (UTC assumed unless offset present).
 *   - OHLC consistency check: high ≥ max(open, close) ≥ min(open, close) ≥ low.
 *   - Price sanity check: all prices > 0, volume ≥ 0.
 *   - Unified timestamp format suitable for downstream indicators and API.
 *
 * These utilities operate on OHLCVData structs returned by DataIngestionEngine
 * and are called automatically by the Python-layer nse_engine wrapper before
 * exposing data to REST consumers.
 */

#pragma once

#include "data_ingestion.hpp"
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Per-row validation result.
 *
 * Produced by DataUtils::validate(). Callers iterate the list to report
 * or filter bad rows before further processing.
 */
struct ValidationError {
    std::size_t row;    ///< Zero-based row index in the OHLCVData struct.
    std::string field;  ///< Name of the offending column ("high", "low", etc.).
    std::string reason; ///< Human-readable description of the violation.
};

// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Static utility class for standardising and validating OHLCV data.
 *
 * All methods operate in O(n) time and do not modify the caller's data
 * unless specifically documented as in-place.
 */
class DataUtils {
public:
    /**
     * @brief Normalise all timestamps in @p data to "YYYY-MM-DD" format.
     *
     * Handles the following input formats produced by Yahoo Finance and Stooq:
     *   "2023-01-05"                  → "2023-01-05"   (no change)
     *   "2023-01-05 00:00:00"         → "2023-01-05"   (time stripped)
     *   "2023-01-05T00:00:00+05:30"   → "2023-01-05"   (ISO 8601 with TZ)
     *   "01/05/2023"                  → "2023-01-05"   (US locale)
     *   "05-Jan-2023"                 → "2023-01-05"   (Bloomberg style)
     *
     * Malformed timestamps that cannot be parsed are left unchanged.
     * Modification is done in-place on @p data.timestamp.
     *
     * @param data  OHLCVData to normalise; modified in place.
     */
    static void normaliseTimestamps(OHLCVData& data);

    /**
     * @brief Validate OHLCV data for internal consistency and price sanity.
     *
     * Checks per row:
     *   - open, high, low, close > 0.
     *   - volume ≥ 0.
     *   - high ≥ open  (bar high cannot be below open).
     *   - high ≥ close (bar high cannot be below close).
     *   - low  ≤ open  (bar low cannot be above open).
     *   - low  ≤ close (bar low cannot be above close).
     *   - high ≥ low   (trivial but checked explicitly).
     *
     * @param data  Read-only reference to the data to validate.
     * @return      List of ValidationError; empty iff all rows pass.
     */
    static std::vector<ValidationError> validate(const OHLCVData& data);

    /**
     * @brief Return a copy of @p data with all invalid rows removed.
     *
     * A row is invalid if it appears in the output of validate().
     * This is equivalent to re-ingesting with MissingValuePolicy::DROP but
     * applies to already-loaded data without re-parsing the CSV.
     *
     * @param data  Source OHLCVData (not modified).
     * @return      New OHLCVData containing only valid rows.
     */
    static OHLCVData dropInvalidRows(const OHLCVData& data);

    /**
     * @brief Parse a timestamp string and return its "YYYY-MM-DD" component.
     *
     * Handles ISO 8601 with or without time and timezone components, slash-
     * delimited US format, and Bloomberg month-name format.
     *
     * @param ts  Raw timestamp string from CSV.
     * @return    Normalised "YYYY-MM-DD" string, or @p ts unchanged if parsing fails.
     */
    static std::string normaliseOneTimestamp(const std::string& ts);

private:
    /** @brief Convert a 3-letter English month abbreviation to "01"–"12". */
    static std::string monthAbbrevToNum(const std::string& mon);
};
