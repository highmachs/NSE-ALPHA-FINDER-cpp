"""
NSE Alpha Engine — Python package root.

This package exposes the high-level Python API wrapping the C++ engine:

    from engine.python import nse_engine

Or import the submodules directly:

    from engine.python.nse_engine    import all_indicators, backtest, benchmark
    from engine.python.data_fetcher  import fetch, fetch_multiple

The low-level pybind11 C++ module (``nse_engine_cpp``) lives at::

    engine/build_output/nse_engine_cpp.cpython-312-x86_64-linux-gnu.so

It is imported automatically by ``nse_engine.py`` and should not need to
be imported directly by application code.

Build the C++ module before using this package::

    bash engine/run.sh build
"""
