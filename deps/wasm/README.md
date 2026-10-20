# GW browser demo

The browser build uses Emscripten, GLFW's browser port and Skia Ganesh/WebGL2.
The page fills the viewport with GW, including its ImGui menus and dialogs;
stdout/stderr go to the browser developer console. No terminal widget is needed.

## Run the existing demo

From the GW repository root:

```sh
make -C deps/wasm serve
```

Open <http://127.0.0.1:8765/gw.html>. Serve over HTTP rather than opening the HTML
as a local file. Stop the server with Ctrl-C. The checked-in demo includes the
small test reference, BAM and indexes and opens `chr1:9900-10300`.

## Rebuild GW using existing WASM dependencies

Use **Emscripten 3.1.65** to match the cached libraries:

```sh
source "$HOME/Documents/tools/emsdk/emsdk_env.sh"
# If another SDK version is active, first run:
# "$EMSDK/emsdk" install 3.1.65
# "$EMSDK/emsdk" activate 3.1.65
# then source emsdk_env.sh again.
make -C deps/wasm JOBS=2
make -C deps/wasm serve
```

Use your own emsdk installation path if different. The wrapper removes inherited
native compiler/linker flags (for example conda's macOS linker flags). Browser
objects live in `deps/wasm/out/gw_objects`, separate from native GW objects.
The WASM build uses headers from `deps/wasm/skia`, never native `lib/skia`.

Output is `deps/wasm/gw.html`, `gw.js`, `gw.wasm` and `gw.data`. Keep these four
files together when hosting. Reload the browser after rebuilding.
`make -C deps/wasm clean` removes only the demo outputs and WASM GW objects;
it retains dependency libraries and native GW objects.

## Build dependencies from scratch

Install emsdk, Python 3, git, curl, ninja, autoconf, automake and make, then:

```sh
source /path/to/emsdk/emsdk_env.sh
JOBS=2 bash deps/wasm/build.sh
make -C deps/wasm serve
```

The script installs/activates Emscripten 3.1.65 and builds Skia m133, XZ 5.2.5
and HTSlib 1.21 before building the demo. It intentionally remains on m133:
the native m143 migration does not change the browser library or its ABI.
A fresh dependency build downloads substantial source trees and takes time.
The compiler version must match across Skia, XZ, HTSlib and GW.

## Local files and limitations

Use **GW menu → Open file**, or drop files onto the canvas. Select a BAM and
its `.bai`/`.csi`, or select the BAM first and its index later. The waiting state
retains the selected BAM without reading it. References require an uncompressed
FASTA and `.fai`; load your reference before alignments against it. Selected files
are mounted lazily under `/local`, then opened automatically through GW's normal
commands. Closing and selecting a file again uses a fresh mount, without a page
reload. Compressed reference/track and CRAM loading are not wired into this new
picker yet.

This is an experimental single-threaded browser build. Local imports use
Asyncify-backed range reads instead of copying entire BAMs into WASM memory;
large-file behavior still needs broader testing. General remote BAM/track loading
is not equivalent to desktop GW: HTSlib's curl/S3/GCS backends are disabled.
There is a separate experimental HTTP-range bridge for reference genomes, which
requires a server that supports ranges and permits the browser's CORS requests.

Settings use IndexedDB when available; private browsing or blocked storage may
fall back to temporary settings. WebGL2 is required. The old `gw-sw.js` service
worker is not registered by this page and is not required for the demo.


## Client-only embedded viewer

Open `/embed.html` on the static demo host. It embeds `gw.html`, the full local
WASM renderer; there is no gw-server connection, PNG stream or file upload.
The separate `deps/wasm_remote` demo still uses server rendering intentionally.
Keep `embed.html`, `embed.mjs` and `local-files.mjs` alongside the built GW files.
The host component exports `createViewer(container, {url, onState, onError})`,
with `ready`, `navigate`, `command`, `snapshot`, `focus`, and `destroy` methods.
Local state events report the current theme, regions and alignment paths; they
are not remote frame descriptors. It needs only ordinary static HTTP hosting.

`test/test_wasm_local_files.mjs` checks bounded reads on a simulated 10 GB File,
index pairing, alternate index names and close/reload mounts, and forbids calling
the File's whole-buffer method. No networking API exists in the local loader.

Local asynchronous I/O is implemented at the **WASI syscall boundary** by
`local-file-library.js`. Each `fd_read`/`fd_pread` completes all requested iovecs
inside one Asyncify suspension. The JS-library `__async` metadata instruments
those imports. Suspending each stream buffer separately is unsafe for libc's
vectored reads and can trap during rewind.

`test/test_wasm_local_io.c` and `.mjs` exercise real compiled libc reads; compile
with `--js-library deps/wasm/local-file-library.js`, `-sASYNCIFY=1`, exports
`_range_stdio,_range_pread`, and runtime methods `ccall,Asyncify,FS`. Use
`-sMODULARIZE=1 -sEXPORT_ES6=1 -sENVIRONMENT=node`, then pass the output `.mjs`
path to `node test/test_wasm_local_io.mjs`. Defining `GW_TEST_HTSLIB` and linking
the browser HTSlib/Zlib/Bzip2/LZMA libraries with `_range_bam` also exported
checks an indexed BAM query against an in-memory fixture baseline.

Browser key, character, mouse and wheel callbacks queue GW input for the frame
loop. A pending asynchronous File read must finish before the next input starts
another HTSlib operation; adjacent cursor moves are coalesced. Native desktop
input remains synchronous. When the reference is shorter than an alignment's
chromosome, navigation uses the BAM header length and reference-dependent drawing
is restricted to fetched bases.

## Online files

The shared Open → Online file dialog supports references, alignments, tracks and
variants through lazy browser HTTP range reads. See
[the catalogue and transport documentation](../../resources/online/README.md).
