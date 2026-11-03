// deps/wasm/pre.js — Injected before the Emscripten runtime starts.
//
// A page can set window.GW_ARGS before gw.js loads to choose the startup
// arguments. The published demo uses this to offer two entries:
//   index.html          → window.GW_ARGS = []                          (no files)
//   demo.html           → window.GW_ARGS = ['/test.fa', '-b', ...]     (bundled demo)
// The bundled demo files are preloaded via --preload-file in the Makefile.

Module['arguments'] = (typeof window !== 'undefined' && Array.isArray(window.GW_ARGS))
    ? window.GW_ARGS
    : ['/test.fa', '-b', '/test.bam', '-r', 'chr1:9900-10300'];
