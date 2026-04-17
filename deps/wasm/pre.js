// deps/wasm/pre.js — Injected before the Emscripten runtime starts.
//
// Sets default CLI arguments so GW receives a genome and BAM path when
// launched from the browser (equivalent to: gw /test.fa -b /test.bam).
// These files must be preloaded via --preload-file in the Makefile.

Module['arguments'] = ['/test.fa', '-b', '/test.bam'];
