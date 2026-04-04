#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_OUT="$SCRIPT_DIR/build_output"

usage() {
    cat <<EOF
NSE Alpha Engine — Run Script

Commands:
  build              Rebuild all C++ targets (module, CLI, tests)
  test               Run C++ unit tests
  serve [PORT]       Start Python FastAPI server (default port: 8000)
  cli <csv> [strat]  Run standalone C++ CLI on a CSV file
                     Strategies: sma_crossover | rsi | macd
  benchmark [ROWS]   Run performance benchmark (default: 1M rows)
  fetch <SYM> <START> [END] [SRC] [OUTDIR]
                     Download NSE data (src: yahoo|stooq|auto)
  help               Show this message

Examples:
  bash run.sh build
  bash run.sh test
  bash run.sh serve 8000
  bash run.sh cli data/RELIANCE.NS.csv sma_crossover --benchmark
  bash run.sh benchmark 1000000
  bash run.sh fetch RELIANCE 2020-01-01 2024-12-31 auto data/
EOF
}

cmd="${1:-help}"

case "$cmd" in
    build)
        echo "[run] Configuring cmake..."
        cmake -B "$SCRIPT_DIR/build" -S "$SCRIPT_DIR" -DCMAKE_BUILD_TYPE=Release
        echo "[run] Compiling..."
        cmake --build "$SCRIPT_DIR/build" --parallel "$(nproc)"
        echo "[run] Build complete. Artifacts:"
        ls "$BUILD_OUT"
        ;;

    test)
        if [[ ! -x "$BUILD_OUT/nse_tests" ]]; then
            echo "[run] Test binary not found. Building first..."
            bash "$0" build
        fi
        echo "[run] Running C++ unit tests..."
        "$BUILD_OUT/nse_tests"
        ;;

    serve)
        PORT="${2:-8000}"
        echo "[run] Starting FastAPI server on port $PORT..."
        PORT="$PORT" python3 "$SCRIPT_DIR/python/server.py"
        ;;

    cli)
        if [[ $# -lt 2 ]]; then
            echo "Usage: $0 cli <csv_file> [strategy] [--benchmark]"
            exit 1
        fi
        if [[ ! -x "$BUILD_OUT/nse_engine" ]]; then
            echo "[run] CLI binary not found. Building first..."
            bash "$0" build
        fi
        shift
        "$BUILD_OUT/nse_engine" "$@"
        ;;

    benchmark)
        ROWS="${2:-1000000}"
        echo "[run] Benchmark with $ROWS rows via Python wrapper..."
        python3 - <<PYEOF
import sys
sys.path.insert(0, "$SCRIPT_DIR/python")
import nse_engine as e
results = e.benchmark($ROWS)
print(f"\n{'Name':<30} {'ms':>10} {'pts/sec':>15}")
print("-" * 60)
for r in results:
    print(f"{r['name']:<30} {r['elapsed_ms']:>10.3f} {r['throughput_per_sec']:>15,}")
PYEOF
        ;;

    fetch)
        if [[ $# -lt 3 ]]; then
            echo "Usage: $0 fetch <SYMBOL> <START_DATE> [END_DATE] [SOURCE] [OUTPUT_DIR]"
            exit 1
        fi
        shift
        python3 "$SCRIPT_DIR/python/data_fetcher.py" "$@"
        ;;

    help|--help|-h)
        usage
        ;;

    *)
        echo "Unknown command: $cmd"
        usage
        exit 1
        ;;
esac
