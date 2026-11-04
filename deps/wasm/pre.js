// deps/wasm/pre.js — Injected before the Emscripten runtime starts.
//
// A page can set window.GW_ARGS before gw.js loads to choose the startup
// arguments. Without it GW starts with nothing loaded. The published site:
//   index.html → window.GW_ARGS = [] (nothing loaded)
//   demo.html  → window.GW_ARGS = ['/test.fa', '-b', ...] (bundled demo BAM)
// The bundled demo files are preloaded via --preload-file in the Makefile.

Module['arguments'] = (typeof window !== 'undefined' && Array.isArray(window.GW_ARGS))
    ? window.GW_ARGS
    : [];
