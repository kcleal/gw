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
as a local file. Stop the server with Ctrl-C. GW starts with nothing loaded; to open the
bundled test reference and BAM at `chr1:9900-10300`, set
`window.GW_ARGS=['/test.fa','-b','/test.bam','-r','chr1:9900-10300']` before `gw.js` loads
(the published `demo.html` does this).

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
objects live in `deps/wasm/out/gw_objects_m143`, separate from native GW objects.
The WASM build uses headers from `deps/wasm/skia`, never native `lib/skia`.

Output is `deps/wasm/gw.html`, `gw.js`, `gw.wasm` and `gw.data`. Keep these four
files together when hosting. Reload the browser after rebuilding.
`make -C deps/wasm clean` removes only the demo outputs and WASM GW objects;
it retains dependency libraries and native GW objects.

## Use the published Skia m143 package

The wrapper automatically selects matching headers and libraries from
`deps/wasm/out/skia_m143` when the published package is unpacked there:

```sh
mkdir -p deps/wasm/out/skia_m143
curl -fL https://github.com/kcleal/skia_build_arm64/releases/download/m143/skia-m143-wasm.tar.gz \
  -o /tmp/skia-m143-wasm.tar.gz
tar -xzf /tmp/skia-m143-wasm.tar.gz -C deps/wasm/out/skia_m143
make -C deps/wasm
```

This package uses Emscripten 3.1.65, matching the existing HTSlib/XZ dependencies.
`WASM_SKIA_SRC` and `WASM_SKIA_PATH` can override the header and library paths.
Objects are isolated from the older m133 cache.

## Build dependencies from scratch

Install emsdk, Python 3, git, curl, ninja, autoconf, automake and make, then:

```sh
source /path/to/emsdk/emsdk_env.sh
JOBS=2 bash deps/wasm/build.sh
make -C deps/wasm serve
```

The script installs/activates Emscripten 3.1.65 and builds Skia m143, XZ 5.2.5
and HTSlib 1.21 before building the demo. Both native and browser builds now target m143. Rebuild browser dependencies
with `bash deps/wasm/build.sh` if the local cache still contains m133.
A fresh dependency build downloads substantial source trees and takes time.
The compiler version must match across Skia, XZ, HTSlib and GW.

## Build size

Debug info is stripped by default: the shared native flags carry `-g`, which
would add ~43 MB of DWARF sections to `gw.wasm` (13 MB of code becomes a 57 MB
binary). `deps/wasm/flags.mk` filters `-g` out and links with
`-Wl,--strip-debug`, producing a ~13 MB wasm. Build with `WASM_DEBUG=1` to keep
debug info:

```sh
make -C deps/wasm            # stripped (default)
make -C deps/wasm WASM_DEBUG=1
```

Note the stripped build has no wasm name section (Emscripten only emits one
with `-g`), so browser stack traces show addresses only — use `WASM_DEBUG=1`
when debugging crashes.

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
The host component exports `createViewer(container, {url, params, focus, onState, onError})`,
with `ready`, `navigate`, `command`, `load`, `loadTrack`, `snapshot`, `focus`, and
`destroy` methods. Local state events report the current theme, regions, alignment
paths and names (`alignment_names`), track names (`tracks`), tiled files (`tiled`)
and the view (`mode`: `single` or `tiled`); they are not remote
frame descriptors. It needs only ordinary static HTTP hosting. The Pages build
publishes this module as `app/embed.js`.

```js
import {createViewer} from 'https://kcleal.github.io/gw/app/embed.js';
const viewer = createViewer(element, {params: {genome: 'hg38', theme: 'igv'}});
await viewer.ready;
await viewer.load('https://…/NA12878.final.cram', {name: 'Daughter'});   // resolves when open
await viewer.loadTrack('https://hgdownload.soe.ucsc.edu/gbdb/hg38/mane/mane.bb', {name: 'MANE'});
await viewer.navigate('chr11:5226990-5227035');
```

Commands reject with GW's error text (for example a URL that cannot be opened).
Embedded, GW does not take the keyboard until it is clicked, so a host such as a
slide deck keeps its keys; pass `focus: true` (or `?focus=1`) to focus on start.

### Startup from the URL

`gw.html` (or the published `index.html`) reads a view from its URL, so a plain link
or `<iframe>` can open prepared data without page code:

```
index.html?genome=hg38&theme=igv&bam=URL1&bam=URL2&name=Daughter&name=Father
          &track=https://…/mane.bb&region=chr11:5226990-5227035&font-size=18
```

`genome` (a FASTA URL, or `hg38`, `hg19`, `hs1` for the catalogue's CORS-enabled
copies), `theme`, `region` and `font-size` take one value; `bam`, `name` (applied
to the opened BAMs in order), `track`, `tiled` and `cmd` (any GW command) repeat. They
run in that order after GW starts, and failures are shown as a toast.

Annotation files open as tracks by default. `tiled=URL` (or `viewer.loadTiled(url, {grid: '4x3'})`,
or the command `load tiled URL`) opens a VCF/BED as a grid of image tiles, one per
variant, instead (`grid=4x3` sets the layout; GW's default is 1x1); `tiles off`/`tiles on` (or `tiles` to toggle) switches between the
grid and the normal view, and navigating to a region also leaves the grid. Remote
`.vcf.gz`/`.bed.gz` need their `.tbi`/`.csi`; plain `.vcf`/`.bed` up to 16 MiB are
read whole.

Remote http(s) annotation tracks (bigBed, bigWig, and indexed BGZF BED/GFF/VCF)
stream over the same range bridge as remote BAM/CRAM files. Two commands help
teaching views: `name bam1 Father` / `name track0 Genes` sets a data label (names
belong to the file, so they survive added panes), and `font-size 20` enlarges labels
for a projector.

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
[the catalogue and transport documentation](../../include/resources/online/README.md).
