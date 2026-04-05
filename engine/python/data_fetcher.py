from __future__ import annotations

import csv
import io
import os
import sys
import time
from datetime import datetime
from typing import List, Optional

try:
    import yfinance as yf
    _HAS_YFINANCE = True
except ImportError:
    _HAS_YFINANCE = False

try:
    import pandas as pd
    _HAS_PANDAS = True
except ImportError:
    _HAS_PANDAS = False

try:
    import requests as _requests
    _HAS_REQUESTS = True
except ImportError:
    _HAS_REQUESTS = False

_STOOQ_BASE = "https://stooq.com/q/d/l/"
_AV_BASE    = "https://www.alphavantage.co/query"
_STANDARD_HEADER = ["timestamp", "open", "high", "low", "close", "volume"]

# Public API

def fetch(
    symbol:     str,
    start:      str,
    end:        str,
    source:     str = "auto",
    output_dir: Optional[str] = None,
) -> str:
    """
    Download historical OHLCV data for a single NSE symbol.

    The output is a CSV string with the standardised header::

        timestamp,open,high,low,close,volume

    and is optionally written to ``{output_dir}/{symbol}.csv``.

    Parameters
    ----------
    symbol:
        NSE ticker without exchange suffix (e.g. ``"RELIANCE"``).
        The appropriate suffix is appended automatically per source.
    start:
        Start date in ``"YYYY-MM-DD"`` format (inclusive).
    end:
        End date in ``"YYYY-MM-DD"`` format (inclusive).
    source:
        One of ``"yahoo"``, ``"stooq"``, ``"alpha"``, or ``"auto"``.
        ``"auto"`` tries Yahoo Finance first; falls back to Stooq.
    output_dir:
        If provided, write the result CSV to ``{output_dir}/{symbol}.csv``.

    Returns
    -------
    str
        Standardised CSV text (including header row).

    Raises
    ------
    ValueError
        Unknown source, invalid date format, or symbol not found.
    RuntimeError
        All attempted sources failed.
    """
    _validate_dates(start, end)

    csv_text: Optional[str] = None

    if source == "yahoo":
        csv_text = _fetch_yahoo(symbol, start, end)
    elif source == "stooq":
        csv_text = _fetch_stooq(symbol, start, end)
    elif source == "alpha":
        csv_text = _fetch_alpha_vantage(symbol, start, end)
    elif source == "auto":
        csv_text = _fetch_auto(symbol, start, end)
    else:
        raise ValueError(
            f"Unknown source '{source}'. Choose from: yahoo, stooq, alpha, auto."
        )

    if not csv_text:
        raise RuntimeError(
            f"All sources failed to return data for '{symbol}' "
            f"between {start} and {end}."
        )

    csv_text = _standardise_csv(csv_text)

    if output_dir:
        os.makedirs(output_dir, exist_ok=True)
        path = os.path.join(output_dir, f"{symbol}.csv")
        with open(path, "w", newline="") as fh:
            fh.write(csv_text)
        print(f"[data_fetcher] Saved {path}")

    return csv_text

def fetch_multiple(
    symbols:    List[str],
    start:      str,
    end:        str,
    source:     str = "auto",
    output_dir: Optional[str] = None,
    delay_sec:  float = 0.5,
) -> dict:
    """
    Download OHLCV data for multiple NSE symbols with rate-limiting.

    Parameters
    ----------
    symbols:
        List of NSE tickers (e.g. ``["RELIANCE", "TCS", "INFY"]``).
    start, end:
        Date range in ``"YYYY-MM-DD"`` format.
    source:
        Data source (see :func:`fetch`).
    output_dir:
        If provided, write individual CSVs for each symbol.
    delay_sec:
        Seconds to sleep between requests (default 0.5) to avoid rate limits.

    Returns
    -------
    dict
        ``{symbol: csv_text}`` for successful downloads;
        ``{symbol: None}`` for failures.
    """
    results = {}
    for i, sym in enumerate(symbols):
        try:
            print(f"[data_fetcher] Fetching {sym} ({i+1}/{len(symbols)}) ...")
            results[sym] = fetch(sym, start, end, source, output_dir)
        except Exception as exc:
            print(f"[data_fetcher] WARN: {sym} failed — {exc}", file=sys.stderr)
            results[sym] = None
        if i < len(symbols) - 1:
            time.sleep(delay_sec)
    return results

# Source-specific fetchers (internal)

def _fetch_yahoo(symbol: str, start: str, end: str) -> Optional[str]:
    """
    Download via yfinance library.

    Appends the ``.NS`` suffix for NSE tickers automatically.
    Returns standardised CSV text or None on failure.
    """
    if not _HAS_YFINANCE:
        print("[data_fetcher] yfinance not installed; skipping Yahoo.", file=sys.stderr)
        return None
    if not _HAS_PANDAS:
        print("[data_fetcher] pandas not installed; skipping Yahoo.", file=sys.stderr)
        return None

    ticker_sym = symbol if symbol.endswith(".NS") else f"{symbol}.NS"
    try:
        df = yf.download(ticker_sym, start=start, end=end, progress=False, auto_adjust=True)
        if df.empty:
            return None

        # Flatten MultiIndex columns if present (yfinance >= 0.2.38)
        if isinstance(df.columns, pd.MultiIndex):
            df.columns = [col[0].lower() for col in df.columns]
        else:
            df.columns = [c.lower().replace(" ", "_") for c in df.columns]

        df = df.reset_index()
        # The index is usually 'Date' or 'Datetime', case-sensitive
        df.rename(columns={"date": "timestamp", "Date": "timestamp", "Datetime": "timestamp", "adj_close": "close"}, inplace=True)

        if "close" not in df.columns and "adj_close" in df.columns:
            df["close"] = df["adj_close"]

        df["timestamp"] = pd.to_datetime(df["timestamp"]).dt.strftime("%Y-%m-%d")
        out = df[["timestamp", "open", "high", "low", "close", "volume"]]
        print(f"[data_fetcher] Yahoo: {len(out)} rows for {symbol}")
        return out.to_csv(index=False)

    except Exception as exc:
        print(f"[data_fetcher] Yahoo failed for {symbol}: {exc}", file=sys.stderr)
        return None

def _fetch_stooq(symbol: str, start: str, end: str) -> Optional[str]:
    """
    Download via Stooq plain-text CSV feed.

    Appends the ``.IN`` suffix for Indian equities and filters the date
    range client-side since Stooq ignores query params for some tickers.
    Returns standardised CSV text or None on failure.
    """
    if not _HAS_REQUESTS:
        print("[data_fetcher] requests not installed; skipping Stooq.", file=sys.stderr)
        return None

    ticker_sym = symbol if symbol.endswith(".IN") else f"{symbol}.IN"
    url = (
        f"{_STOOQ_BASE}?s={ticker_sym.lower()}"
        f"&d1={start.replace('-','')}&d2={end.replace('-','')}&i=d"
    )
    try:
        resp = _requests.get(url, timeout=15)
        resp.raise_for_status()
        text = resp.text.strip()
        if not text or "No data" in text or len(text.splitlines()) < 2:
            return None

        out_lines = ["timestamp,open,high,low,close,volume"]
        reader    = csv.DictReader(io.StringIO(text))
        count     = 0
        for row in reader:
            ts = row.get("Date", row.get("date", ""))
            if not _in_range(ts, start, end):
                continue
            out_lines.append(
                f"{ts},"
                f"{row.get('Open', row.get('open', ''))},"
                f"{row.get('High', row.get('high', ''))},"
                f"{row.get('Low',  row.get('low',  ''))},"
                f"{row.get('Close',row.get('close',''))},"
                f"{row.get('Volume', row.get('volume', '0'))}"
            )
            count += 1
        if count == 0:
            return None
        print(f"[data_fetcher] Stooq: {count} rows for {symbol}")
        return "\n".join(out_lines)

    except Exception as exc:
        print(f"[data_fetcher] Stooq failed for {symbol}: {exc}", file=sys.stderr)
        return None

def _fetch_alpha_vantage(symbol: str, start: str, end: str) -> Optional[str]:
    """
    Download via Alpha Vantage TIME_SERIES_DAILY_ADJUSTED endpoint.

    Requires the ``ALPHA_VANTAGE_KEY`` environment variable.
    Returns standardised CSV text or None on failure.

    Notes
    -----
    Alpha Vantage's free tier allows 25 requests/day.
    The ``outputsize=full`` parameter fetches up to 20 years of history.
    Adjusted close prices are used (column "5. adjusted close").
    """
    if not _HAS_REQUESTS:
        print("[data_fetcher] requests not installed; skipping Alpha Vantage.",
              file=sys.stderr)
        return None

    api_key = os.environ.get("ALPHA_VANTAGE_KEY", "")
    if not api_key:
        print(
            "[data_fetcher] ALPHA_VANTAGE_KEY not set; skipping Alpha Vantage.",
            file=sys.stderr,
        )
        return None

    try:
        params = {
            "function":   "TIME_SERIES_DAILY_ADJUSTED",
            "symbol":     symbol,
            "outputsize": "full",
            "datatype":   "json",
            "apikey":     api_key,
        }
        resp = _requests.get(_AV_BASE, params=params, timeout=20)
        resp.raise_for_status()
        data = resp.json()

        if "Error Message" in data:
            print(f"[data_fetcher] Alpha Vantage error: {data['Error Message']}",
                  file=sys.stderr)
            return None
        if "Note" in data:
            print(f"[data_fetcher] Alpha Vantage rate limit: {data['Note']}",
                  file=sys.stderr)
            return None

        ts_key = "Time Series (Daily)"
        if ts_key not in data:
            return None

        out_lines = ["timestamp,open,high,low,close,volume"]
        count     = 0
        for date_str, vals in sorted(data[ts_key].items()):
            if not _in_range(date_str, start, end):
                continue
            adj_close = vals.get("5. adjusted close", vals.get("4. close", ""))
            out_lines.append(
                f"{date_str},"
                f"{vals.get('1. open',   '')},"
                f"{vals.get('2. high',   '')},"
                f"{vals.get('3. low',    '')},"
                f"{adj_close},"
                f"{vals.get('6. volume', '0')}"
            )
            count += 1

        if count == 0:
            return None
        print(f"[data_fetcher] Alpha Vantage: {count} rows for {symbol}")
        return "\n".join(out_lines)

    except Exception as exc:
        print(f"[data_fetcher] Alpha Vantage failed for {symbol}: {exc}", file=sys.stderr)
        return None

def _fetch_auto(symbol: str, start: str, end: str) -> Optional[str]:
    """
    Try Yahoo Finance first; fall back to Stooq on any failure.

    Alpha Vantage is not tried automatically because it has a strict
    daily request quota.  Use ``source="alpha"`` explicitly when needed.
    """
    result = _fetch_yahoo(symbol, start, end)
    if result:
        return result
    print(f"[data_fetcher] Yahoo failed for {symbol}; trying Stooq ...", file=sys.stderr)
    return _fetch_stooq(symbol, start, end)

# Data standardisation (internal)

def _standardise_csv(csv_text: str) -> str:
    """
    Ensure the CSV uses the canonical header and sorts rows ascending by date.

    Handles minor schema variations (different column capitalisations, extra
    columns) by projecting onto the six standard columns.

    Parameters
    ----------
    csv_text:
        Raw CSV text from any supported source.

    Returns
    -------
    str
        Standardised CSV with header: timestamp,open,high,low,close,volume
    """
    reader = csv.DictReader(io.StringIO(csv_text.strip()))
    if reader.fieldnames is None:
        return csv_text

    col_map = {f.lower().replace(" ", "_"): f for f in reader.fieldnames}

    def _find(names):
        for n in names:
            if n in col_map:
                return col_map[n]
        return None

    ts_col = _find(["timestamp", "date", "datetime", "time"])
    o_col  = _find(["open"])
    h_col  = _find(["high"])
    l_col  = _find(["low"])
    c_col  = _find(["close", "adj_close", "adj close"])
    v_col  = _find(["volume", "vol"])

    if not all([ts_col, o_col, h_col, l_col, c_col]):
        return csv_text  # Cannot standardise — return as-is

    rows = []
    for row in reader:
        rows.append([
            row.get(ts_col, ""),
            row.get(o_col,  ""),
            row.get(h_col,  ""),
            row.get(l_col,  ""),
            row.get(c_col,  ""),
            row.get(v_col,  "0") if v_col else "0",
        ])

    rows.sort(key=lambda r: r[0])  # ISO 8601 sorts lexicographically

    out = io.StringIO()
    writer = csv.writer(out)
    writer.writerow(_STANDARD_HEADER)
    writer.writerows(rows)
    return out.getvalue()

def _validate_dates(start: str, end: str) -> None:
    """
    Raise ValueError if start or end are not valid YYYY-MM-DD strings,
    or if start > end.
    """
    fmt = "%Y-%m-%d"
    try:
        s = datetime.strptime(start, fmt)
        e = datetime.strptime(end, fmt)
    except ValueError as exc:
        raise ValueError(f"Invalid date format: {exc}") from exc
    if s > e:
        raise ValueError(f"start ({start}) must be <= end ({end})")

def _in_range(date_str: str, start: str, end: str) -> bool:
    """Return True iff date_str falls within [start, end] (string comparison)."""
    return start <= date_str[:10] <= end

# CLI entry point

if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(
        description="NSE Alpha Engine data fetcher — download OHLCV data.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=(
            "Examples:\n"
            "  python3 data_fetcher.py RELIANCE 2020-01-01 2024-12-31\n"
            "  python3 data_fetcher.py RELIANCE,TCS 2020-01-01 2024-12-31 auto data/\n"
            "  python3 data_fetcher.py RELIANCE 2020-01-01 2024-12-31 alpha data/\n"
        ),
    )
    parser.add_argument("symbols",
                        help="Symbol or comma-separated list (e.g. RELIANCE or RELIANCE,TCS)")
    parser.add_argument("start", help="Start date YYYY-MM-DD")
    parser.add_argument("end",   help="End date YYYY-MM-DD",
                        nargs="?", default=datetime.today().strftime("%Y-%m-%d"))
    parser.add_argument("source", help="Data source: yahoo|stooq|alpha|auto",
                        nargs="?", default="auto")
    parser.add_argument("output_dir", help="Directory to save CSV files",
                        nargs="?", default=None)
    args = parser.parse_args()

    sym_list = [s.strip() for s in args.symbols.split(",") if s.strip()]
    if len(sym_list) == 1:
        try:
            text = fetch(sym_list[0], args.start, args.end, args.source, args.output_dir)
            if not args.output_dir:
                print(text[:2000])
        except Exception as e:
            print(f"Error: {e}", file=sys.stderr)
            sys.exit(1)
    else:
        results = fetch_multiple(sym_list, args.start, args.end, args.source, args.output_dir)
        ok  = sum(1 for v in results.values() if v is not None)
        err = len(results) - ok
        print(f"\nCompleted: {ok} OK, {err} failed.")
