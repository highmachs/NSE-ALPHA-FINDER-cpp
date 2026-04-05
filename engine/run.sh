#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────────────────
# NSE Alpha Engine — Unified Run Script
# Pure C++ + Python backend. No Node.js.
# ─────────────────────────────────────────────────────────────────────────────
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_OUT="$SCRIPT_DIR/build_output"
PYTHON="$SCRIPT_DIR/python"

# ── Colours ───────────────────────────────────────────────────────────────────
if [ -t 1 ]; then
    R="\033[0m"   BOLD="\033[1m"  DIM="\033[2m"
    GRN="\033[32m" YLW="\033[33m" CYN="\033[36m"
    BGRN="\033[1;32m" BRED="\033[1;31m" BCYN="\033[1;36m" BYLW="\033[1;33m"
else
    R="" BOLD="" DIM="" GRN="" YLW="" CYN="" BGRN="" BRED="" BCYN="" BYLW=""
fi

ok()   { echo -e "${BGRN}[OK]${R}  $*"; }
info() { echo -e "${CYN}[..] ${DIM}$*${R}"; }
fail() { echo -e "${BRED}[!!]${R}  $*"; }
sep()  { echo -e "${DIM}────────────────────────────────────────────────────${R}"; }

# ─────────────────────────────────────────────────────────────────────────────
usage() {
    echo -e "${BCYN}"
    cat << 'EOF'
 ███╗   ██╗███████╗███████╗     █████╗ ██╗      ██████╗ ██╗  ██╗ █████╗
 ████╗  ██║██╔════╝██╔════╝    ██╔══██╗██║     ██╔══██╗██║  ██║██╔══██╗
 ██╔██╗ ██║███████╗█████╗      ███████║██║     ██████╔╝███████║███████║
 ██║╚██╗██║╚════██║██╔══╝      ██╔══██║██║     ██╔═══╝ ██╔══██║██╔══██║
 ██║ ╚████║███████║███████╗    ██║  ██║███████╗██║     ██║  ██║██║  ██║
 ╚═╝  ╚═══╝╚══════╝╚══════╝    ╚═╝  ╚═╝╚══════╝╚═╝     ╚═╝  ╚═╝╚═╝  ╚═╝
EOF
    echo -e "${R}"
    echo -e "${BOLD}Commands:${R}"
    echo -e "  ${CYN}build${R}                   Rebuild all C++ targets (module · CLI · tests)"
    echo -e "  ${CYN}test${R}                    Run 647 C++ unit tests"
    echo -e "  ${CYN}serve${R} [PORT]            Start FastAPI server (default: 8080)"
    echo -e "  ${CYN}cli${R} <csv> [options]     Run C++ standalone CLI on a CSV file"
    echo -e "  ${CYN}benchmark${R} [ROWS]        C++ benchmark (default: 1 000 000 rows)"
    echo -e "  ${CYN}bench-table${R} [MAX_ROWS]  Full benchmark table across all row counts"
    echo -e "  ${CYN}compare${R} [ROWS]          C++ vs pure-Python speed comparison"
    echo -e "  ${CYN}all-strategies${R} <csv>    Run all 3 strategies + comparison table"
    echo -e "  ${CYN}validate${R} <csv>          Validate OHLCV data (OHLC + price checks)"
    echo -e "  ${CYN}fetch${R} <SYM> <START> [END] [SRC] [DIR]  Download NSE OHLCV data"
    echo -e "  ${CYN}help${R}                    Show this message"
    echo ""
    echo -e "${BOLD}CLI options (--strategy all | sma_crossover | rsi | macd):${R}"
    echo -e "  --strategy <name>  --sma-short N  --sma-long N"
    echo -e "  --rsi-window N     --rsi-oversold F  --rsi-overbought F"
    echo -e "  --macd-fast N      --macd-slow N  --macd-signal N"
    echo -e "  --bb-window N      --bb-k F       --benchmark  --rows N"
    echo -e "  --export file.csv  --no-color     --verbose"
    echo ""
    echo -e "${BOLD}Examples:${R}"
    echo -e "  bash run.sh build"
    echo -e "  bash run.sh test"
    echo -e "  bash run.sh serve 8000"
    echo -e "  bash run.sh cli data/sample_RELIANCE.csv --strategy all"
    echo -e "  bash run.sh cli data/sample_RELIANCE.csv --strategy rsi --rsi-window 9 --benchmark"
    echo -e "  bash run.sh cli data/sample_RELIANCE.csv --export trades.csv"
    echo -e "  bash run.sh benchmark 2000000"
    echo -e "  bash run.sh bench-table 1000000"
    echo -e "  bash run.sh compare 100000"
    echo -e "  bash run.sh all-strategies data/sample_RELIANCE.csv"
    echo -e "  bash run.sh validate data/sample_RELIANCE.csv"
    echo -e "  bash run.sh fetch RELIANCE 2020-01-01 2024-12-31 auto data/"
}

# ─────────────────────────────────────────────────────────────────────────────
build_if_needed() {
    if [[ ! -x "$BUILD_OUT/nse_engine" ]]; then
        info "Binary not found — building first..."
        bash "$0" build
    fi
}

# ─────────────────────────────────────────────────────────────────────────────
cmd="${1:-help}"

case "$cmd" in

    build)
        sep
        info "Configuring CMake (Release, -O3)..."
        cmake -B "$SCRIPT_DIR/build" -S "$SCRIPT_DIR" \
              -DCMAKE_BUILD_TYPE=Release -Wno-dev 2>&1 | grep -v "^--" || true
        info "Compiling 3 targets in parallel ($(nproc) cores)..."
        cmake --build "$SCRIPT_DIR/build" --parallel "$(nproc)"
        sep
        ok "Build complete. Artifacts:"
        ls -lh "$BUILD_OUT"
        ;;

    test)
        build_if_needed
        sep
        info "Running C++ unit tests..."
        "$BUILD_OUT/nse_tests"
        ;;

    serve)
        PORT="${2:-8080}"
        sep
        info "Starting FastAPI server on port $PORT..."
        info "Docs → http://localhost:$PORT/docs"
        sep
        PORT="$PORT" python3 "$PYTHON/server.py"
        ;;

    cli)
        build_if_needed
        if [[ $# -lt 2 ]]; then
            echo "Usage: bash run.sh cli <csv_file> [options]"
            echo "Run:   $BUILD_OUT/nse_engine --help  for all options"
            exit 1
        fi
        shift
        "$BUILD_OUT/nse_engine" "$@"
        ;;

    benchmark)
        build_if_needed
        ROWS="${2:-1000000}"
        sep
        info "C++ standalone benchmark — $ROWS rows..."
        "$BUILD_OUT/nse_engine" --benchmark --rows "$ROWS"
        ;;

    bench-table)
        MAX="${2:-1000000}"
        sep
        info "Full benchmark table (max $MAX rows)..."
        python3 "$PYTHON/benchmark_table.py" --max-rows "$MAX"
        ;;

    compare)
        ROWS="${2:-100000}"
        sep
        info "C++ vs pure-Python comparison — $ROWS rows..."
        python3 "$PYTHON/compare.py" "$ROWS"
        ;;

    all-strategies)
        build_if_needed
        if [[ $# -lt 2 ]]; then
            echo "Usage: bash run.sh all-strategies <csv_file>"
            exit 1
        fi
        sep
        info "Running all 3 strategies on: $2"
        "$BUILD_OUT/nse_engine" "$2" --strategy all --benchmark
        ;;

    validate)
        if [[ $# -lt 2 ]]; then
            echo "Usage: bash run.sh validate <csv_file>"
            exit 1
        fi
        sep
        info "Validating OHLCV data: $2"
        python3 - << PYEOF
import sys
sys.path.insert(0, "$BUILD_OUT")
import nse_engine_cpp as e

data = e.DataIngestionEngine.load_from_csv("$2", e.MissingValuePolicy.DROP)
e.DataUtils.normalise_timestamps(data)
errs = e.DataUtils.validate(data)

print(f"  Rows loaded : {data.size()}")
print(f"  Date range  : {data.timestamp[0]}  →  {data.timestamp[-1]}")
print(f"  Price range : {min(data.close):.2f}  –  {max(data.close):.2f}")

if not errs:
    print("  Validation  : \033[1;32mPASS (0 errors)\033[0m")
else:
    print(f"  Validation  : \033[1;33m{len(errs)} warning(s)\033[0m")
    for err in errs[:10]:
        print(f"    row {err.row}: [{err.field}] {err.reason}")
    if len(errs) > 10:
        print(f"    ... and {len(errs)-10} more")
PYEOF
        ;;

    fetch)
        if [[ $# -lt 3 ]]; then
            echo "Usage: bash run.sh fetch <SYMBOL> <START_DATE> [END_DATE] [SOURCE] [OUTPUT_DIR]"
            echo "Sources: yahoo | stooq | alpha | auto"
            exit 1
        fi
        shift
        python3 "$PYTHON/data_fetcher.py" "$@"
        ;;

    help|--help|-h|"")
        usage
        ;;

    *)
        fail "Unknown command: $cmd"
        usage
        exit 1
        ;;
esac
