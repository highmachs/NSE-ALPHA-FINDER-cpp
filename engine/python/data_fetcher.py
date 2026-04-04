"""
NSE Alpha Engine — Data Fetcher

Downloads historical OHLCV data from free public sources for NSE equities:
  1. Yahoo Finance  (ticker: SYMBOL.NS)
  2. Stooq          (no authentication required)

Outputs standardized CSV: timestamp,open,high,low,close,volume
Compatible directly with DataIngestionEngine.load_from_csv()
"""

import urllib.request
import urllib.parse
import urllib.error
import os
import time
import datetime
import csv
import io
from typing import Optional, Literal


# ── Constants ─────────────────────────────────────────────────────────────────

YAHOO_BASE  = "https://query1.finance.yahoo.com/v8/finance/chart"
STOOQ_BASE  = "https://stooq.com/q/d/l"

USER_AGENT  = (
    "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36"
)

# Standard output schema
OUTPUT_COLUMNS = ["timestamp", "open", "high", "low", "close", "volume"]


# ── HTTP Utility ──────────────────────────────────────────────────────────────

def _get(url: str, headers: dict = None, timeout: int = 30) -> str:
    req = urllib.request.Request(url)
    req.add_header("User-Agent", USER_AGENT)
    if headers:
        for k, v in headers.items():
            req.add_header(k, v)
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            return resp.read().decode("utf-8")
    except urllib.error.HTTPError as e:
        raise RuntimeError(f"HTTP {e.code} fetching {url}: {e.reason}")
    except urllib.error.URLError as e:
        raise RuntimeError(f"URL error fetching {url}: {e.reason}")


# ── Timestamp Utilities ───────────────────────────────────────────────────────

def _to_epoch(date_str: str) -> int:
    """Convert YYYY-MM-DD to UTC epoch seconds."""
    dt = datetime.datetime.strptime(date_str, "%Y-%m-%d")
    return int(dt.replace(tzinfo=datetime.timezone.utc).timestamp())


def _epoch_to_date(epoch: int) -> str:
    """Convert epoch seconds to YYYY-MM-DD."""
    return datetime.datetime.utcfromtimestamp(epoch).strftime("%Y-%m-%d")


# ── Yahoo Finance ─────────────────────────────────────────────────────────────

def _fetch_yahoo(symbol_ns: str, start: str, end: str) -> str:
    """
    Fetch OHLCV from Yahoo Finance v8 API.

    Parameters
    ----------
    symbol_ns : str
        Yahoo Finance ticker, e.g. "RELIANCE.NS"
    start, end : str
        Date range in YYYY-MM-DD format.

    Returns
    -------
    str
        Standardized CSV text (timestamp,open,high,low,close,volume).
    """
    t1 = _to_epoch(start)
    t2 = _to_epoch(end) + 86400  # include end date

    params = urllib.parse.urlencode({
        "interval": "1d",
        "period1":  t1,
        "period2":  t2,
        "events":   "history",
    })
    url = f"{YAHOO_BASE}/{urllib.parse.quote(symbol_ns)}?{params}"

    import json
    raw = _get(url, headers={"Accept": "application/json"})
    data = json.loads(raw)

    try:
        result = data["chart"]["result"][0]
        timestamps = result["timestamp"]
        indicators  = result["indicators"]["quote"][0]
        opens   = indicators["open"]
        highs   = indicators["high"]
        lows    = indicators["low"]
        closes  = indicators["close"]
        volumes = indicators["volume"]
    except (KeyError, IndexError, TypeError) as e:
        raise RuntimeError(f"Yahoo Finance API response parse error: {e}")

    rows = [OUTPUT_COLUMNS]
    for i, ts in enumerate(timestamps):
        o = opens[i]
        h = highs[i]
        l = lows[i]
        c = closes[i]
        v = volumes[i]
        if any(x is None for x in [o, h, l, c]):
            continue
        date = _epoch_to_date(ts)
        rows.append([
            date,
            f"{o:.4f}",
            f"{h:.4f}",
            f"{l:.4f}",
            f"{c:.4f}",
            str(v if v is not None else 0),
        ])

    output = io.StringIO()
    writer = csv.writer(output)
    writer.writerows(rows)
    return output.getvalue()


# ── Stooq ─────────────────────────────────────────────────────────────────────

def _fetch_stooq(symbol: str, start: str, end: str) -> str:
    """
    Fetch OHLCV from Stooq (no authentication required).

    Parameters
    ----------
    symbol : str
        Stooq symbol, e.g. "RELIANCE.IN" or "TCS.IN"
    start, end : str
        Date range in YYYY-MM-DD format.

    Returns
    -------
    str
        Standardized CSV text (timestamp,open,high,low,close,volume).
    """
    s_fmt = start.replace("-", "")
    e_fmt = end.replace("-", "")
    params = urllib.parse.urlencode({
        "s": symbol,
        "d1": s_fmt,
        "d2": e_fmt,
        "i": "d",
    })
    url = f"{STOOQ_BASE}/?{params}"
    raw = _get(url)

    if "No data" in raw or len(raw.strip()) < 50:
        raise RuntimeError(f"Stooq returned no data for symbol '{symbol}'")

    # Stooq CSV format: Date,Open,High,Low,Close,Volume
    reader = csv.DictReader(io.StringIO(raw))
    output = io.StringIO()
    writer = csv.writer(output)
    writer.writerow(OUTPUT_COLUMNS)

    for row in reader:
        date  = row.get("Date", "").strip()
        open_ = row.get("Open", "").strip()
        high  = row.get("High", "").strip()
        low   = row.get("Low",  "").strip()
        close = row.get("Close","").strip()
        vol   = row.get("Volume", "0").strip() or "0"
        if not all([date, open_, high, low, close]):
            continue
        writer.writerow([date, open_, high, low, close, vol])

    return output.getvalue()


# ── Public API ────────────────────────────────────────────────────────────────

def fetch(
    symbol: str,
    start: str,
    end: Optional[str] = None,
    source: Literal["yahoo", "stooq", "auto"] = "auto",
    output_dir: Optional[str] = None,
) -> str:
    """
    Download historical OHLCV data for an NSE equity.

    Parameters
    ----------
    symbol : str
        NSE symbol. For Yahoo Finance: "RELIANCE.NS", "TCS.NS".
        For Stooq: "RELIANCE.IN", "TCS.IN".
        For auto mode: pass the base symbol (e.g. "RELIANCE") and the
        correct suffix is appended per source.
    start : str
        Start date in YYYY-MM-DD format.
    end : str, optional
        End date in YYYY-MM-DD format. Defaults to today.
    source : "yahoo" | "stooq" | "auto"
        Data source. "auto" tries Yahoo Finance first, then Stooq.
    output_dir : str, optional
        If provided, saves the CSV to this directory and returns the filepath.
        Otherwise, returns the CSV text.

    Returns
    -------
    str
        CSV text (or file path if output_dir is set).

    Raises
    ------
    RuntimeError
        If data cannot be fetched from any source.
    """
    if end is None:
        end = datetime.datetime.utcnow().strftime("%Y-%m-%d")

    errors = []
    csv_text = None

    if source in ("yahoo", "auto"):
        ticker = symbol if symbol.endswith(".NS") else f"{symbol}.NS"
        try:
            csv_text = _fetch_yahoo(ticker, start, end)
        except Exception as e:
            errors.append(f"Yahoo Finance ({ticker}): {e}")
            csv_text = None

    if csv_text is None and source in ("stooq", "auto"):
        stooq_sym = symbol if symbol.endswith(".IN") else f"{symbol}.IN"
        try:
            csv_text = _fetch_stooq(stooq_sym, start, end)
        except Exception as e:
            errors.append(f"Stooq ({stooq_sym}): {e}")
            csv_text = None

    if csv_text is None:
        raise RuntimeError(
            f"Failed to fetch data for '{symbol}':\n" + "\n".join(errors)
        )

    # Validate row count
    lines = [l for l in csv_text.splitlines() if l.strip()]
    if len(lines) < 2:
        raise RuntimeError(f"Fetched data for '{symbol}' has no rows")

    if output_dir is not None:
        os.makedirs(output_dir, exist_ok=True)
        clean_sym = symbol.replace(".", "_")
        filename  = f"{clean_sym}_{start}_{end}.csv"
        filepath  = os.path.join(output_dir, filename)
        with open(filepath, "w", encoding="utf-8") as f:
            f.write(csv_text)
        print(f"[data_fetcher] Saved {len(lines)-1} rows → {filepath}")
        return filepath

    return csv_text


def fetch_multiple(
    symbols: list,
    start: str,
    end: Optional[str] = None,
    source: Literal["yahoo", "stooq", "auto"] = "auto",
    output_dir: str = "data",
    delay_sec: float = 1.0,
) -> dict:
    """
    Download data for multiple NSE symbols with rate-limiting.

    Parameters
    ----------
    symbols : list of str
        List of NSE symbols.
    start, end : str
        Date range.
    source : "yahoo" | "stooq" | "auto"
    output_dir : str
        Directory to save CSV files.
    delay_sec : float
        Seconds to wait between requests (avoid rate-limiting).

    Returns
    -------
    dict
        { symbol: filepath_or_error_message }
    """
    results = {}
    for i, sym in enumerate(symbols):
        try:
            path = fetch(sym, start, end, source=source, output_dir=output_dir)
            results[sym] = path
        except Exception as e:
            results[sym] = f"ERROR: {e}"
            print(f"[data_fetcher] FAILED {sym}: {e}")
        if i < len(symbols) - 1:
            time.sleep(delay_sec)
    return results


# ── CLI ───────────────────────────────────────────────────────────────────────

if __name__ == "__main__":
    import sys

    if len(sys.argv) < 3:
        print("Usage: python data_fetcher.py <SYMBOL> <START_DATE> [END_DATE] [SOURCE] [OUTPUT_DIR]")
        print("  SYMBOL     : e.g. RELIANCE or RELIANCE.NS")
        print("  START_DATE : YYYY-MM-DD")
        print("  END_DATE   : YYYY-MM-DD (default: today)")
        print("  SOURCE     : yahoo | stooq | auto (default: auto)")
        print("  OUTPUT_DIR : directory to save CSV (default: prints to stdout)")
        sys.exit(1)

    symbol     = sys.argv[1]
    start_date = sys.argv[2]
    end_date   = sys.argv[3] if len(sys.argv) > 3 else None
    src        = sys.argv[4] if len(sys.argv) > 4 else "auto"
    out_dir    = sys.argv[5] if len(sys.argv) > 5 else None

    result = fetch(symbol, start_date, end_date, source=src, output_dir=out_dir)
    if out_dir is None:
        print(result)
