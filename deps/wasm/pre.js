// deps/wasm/pre.js — Injected before the Emscripten runtime starts.
//
// A page can set window.GW_ARGS before gw.js loads to choose the startup
// arguments. Without it GW starts with nothing loaded. The published site:
//   index.html → window.GW_ARGS = [] (nothing loaded)
//   demo.html  → window.GW_ARGS = ['/test.fa', '-b', ...] (bundled demo BAM)
// The bundled demo files are preloaded via --preload-file in the Makefile.

//
// With nothing loaded GW shows its reference genome chooser. Add ?blank=1 to the page
// URL (or put '--wait' in GW_ARGS) for an empty viewer with no chooser or hints, which
// waits for the host page to drive it.

Module['arguments'] = (typeof window !== 'undefined' && Array.isArray(window.GW_ARGS))
    ? window.GW_ARGS.slice()
    : [];
if (typeof location !== 'undefined') {
    var gwBlank = new URLSearchParams(location.search).get('blank');
    if (gwBlank !== null && gwBlank !== '0' && Module['arguments'].indexOf('--wait') < 0) Module['arguments'].push('--wait');
}
