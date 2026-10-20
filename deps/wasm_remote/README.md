# Remote-rendered GW client

This client uses GW's **existing** `GwPlot::overlayImGui` implementation and menu
components. There is no separate remote toolbar or replacement Settings window.
The original full browser renderer remains in `../wasm`.

The demo now uses the versioned [viewer protocol](../../protocol/viewer/v1/README.md).
Browser/server exchanges go through the shared
`client.mjs` adapter and `/viewer/v1/message` plus `/viewer/v1/frame`. Override
`Module.viewerTransport` to use another server or transport. The native API and
gwplot entry points are unchanged. The legacy `/api/*` endpoints remain available
as a separate view for existing callers.

The browser owns a data-free GW UI state mirror. Existing menu commands go through
an optional command callback; supported render-setting edits go to the server.
The alignment/reference rendering happens on the native server. The browser never
loads the server's BAM/FASTA files. This first reuse-oriented implementation links
GW's WASM UI runtime and its libraries, rather than the earlier tiny ImGui-only
prototype. Extracting smaller UI-only libraries can be done later without replacing
the overlay again.

```sh
source /path/to/emsdk/emsdk_env.sh
make -C deps/wasm_remote
make server GW_SERVER_DIR=../gw-server OATPP_ROOT=/path/to/oatpp/prefix
../gw-server/build/gw-server --reference test/test.fa --bam test/test.bam \
  --region chr1:9900-10300 --web-root "$PWD/deps/wasm_remote/out"
```

Open <http://127.0.0.1:8766/>. Use the normal hamburger, view menu, region box,
Tools and full-screen Settings. Open file and Import local files launch the browser BAM/index chooser. Close file
uses the native file list and sends removals to the server. Non-render settings
remain disabled.
Left-clicks on reads, coverage, reference and tracks use the server's original hit-testing and return the original popup models to the client. Popup text includes
selected read/feature metadata (and SAM for the explicit copy action); files remain
on the server. Read action IDs retain the server-side selection for Find/Mate. Add mate remains unavailable until multiple-region UI state is synchronized.
Left dragging pans or uses the original scale-bar/track controls; wheel input uses
GW's normal zoom/pan behavior. Right-click mode switching is not forwarded yet.

Pointer actions use typed JSON `pointer` actions with a phase, button, modifiers
and logical canvas coordinates. The adapter converts GW's existing UI callback
values; no GLFW constants cross the new wire interface. Events target the
displayed frame ID and shared revision, are serialized, and moves are coalesced.
ImGui consumes menu/popup input locally. Releases and focus-loss cancellation finish
active gestures even over a popup. Run `python3 test/test_remote_input.py URL` with
the fixture server to check hit testing, dragging, wheel, cancellation and DPI.

PNG dimensions match the **physical framebuffer**, not CSS dimensions. The resize
request includes `pixel_ratio`, so server fonts, gaps and row layout scale as well.
On a 2x display, a 1000x650 CSS viewport gets a 2000x1300 PNG. Fractional-DPI sizes
are allocated exactly. A 32-megapixel server limit bounds memory; oversized views
are rejected rather than silently returned at a blurry lower resolution.
Server frames use lossless PNG with a fixed Up row filter and zlib level 3,
avoiding the per-row adaptive filter search. Normal GW/gwplot PNG exports keep
their existing encoder settings.

Static assets and API share one origin. PNGs are binary, not base64. The native
demo provides a shared `default` session for up to eight clients, each with its
own viewport, renderer, popup state and gesture state. Shared regions/settings
propagate through lightweight revision polling; unchanged polls do not fetch PNGs.
Reconnects cancel gestures and replace connection IDs. Idle clients expire after
30 seconds plus a 120-second reconnect grace period. Tests use Emscripten 3.1.65/WebGL2.
Build products and isolated objects are ignored under `out/`; existing
`deps/wasm` source files and dependency libraries are preserved.

Tools commands are serialized, including multi-command filter actions. Filter,
Find, position sorting, intron controls and display aliases run through GW's
command parser. Native output is returned in `output` and displayed by GW's
existing Output popup. Snapshot downloads the displayed PNG in the browser; it
does not write a path on the server. Settings commands open the existing local
Settings window. File operations remain unsupported. Region add/close and active-pane commands are synchronized.
Wheel input is consumed before ImGui EndFrame clears its deltas. Nonzero
browser wheel steps are normalized to native zoom/pan steps, avoiding a
second threshold on already-scaled browser events.

The original alignment scrollbars now use server-provided layout/range metadata.
`vscroll INDEX OFFSET` applies the native read-stack offset. Remote scrollbar thumbs update locally while dragging; the server applies the
offset and renders once on release. Native GW retains its existing live-update
behavior for small views.
Popup backgrounds are opaque in the remote viewer, and each frame clears the
whole WebGL framebuffer before drawing the PNG and UI.

### Native command box

Click the canvas to focus it, then press `/` or `:`. The browser draws GW's
original Skia command box and suggestions locally, using GW's existing editor
for caret movement, Backspace/Delete, Tab completion, Up/Down history and Escape.
Enter sends the command through the same serialized server transport as Tools.
Local typing does not make network requests or re-render genomic data. The
transparent command layer is cached until its text, caret, font or dimensions
change.

Every client frame is cleared and composed into a private WebGL framebuffer,
then copied to the browser framebuffer. This avoids relying on the contents of
previous browser swap buffers when rendering the PNG and ImGui dialogs.

Scale-bar press/drag/release uses GW's native selection geometry and shaded box.
The selection preview stays local while dragging; the server receives the press
and release and applies native drag-to-zoom. Escape cancels without navigating.

Command text is relayed to GW's server-side parser, including chromosome-only
shorthand (`chr1`), single positions (`chr1:10,000`), full ranges and feature
names. Unknown commands use GW's normal Output messages. File operations, server
shutdown and unsupported UI lifecycle operations remain excluded.

### Resizing, region panes and hotkeys

The canvas follows viewport dimensions and device pixel ratio, including after
GLFW updates its canvas styles. Resizes cancel active gestures and render the
new physical dimensions on the server.

The existing + duplicates its region and X closes it, including the last pane.
An empty view can be reopened with a region command. State exposes `regions`
and `active_region`; commands can specify `region_index` to target a pane.
Up to 16 panes are supported. Stale read-popup contexts expire when pane topology
changes.

Canvas hotkeys reuse native GW handlers: arrows (pan/zoom), Page Up/Down (read
scrolling), brackets (select pane), L (link mode), F (find selected reads), Ctrl+R
(repeat command), Ctrl+minus/equal (row limit). ImGui text fields/dialogs consume
keyboard input first. Desktop window-management shortcuts are not forwarded.

The `line` command and Line checkbox are client-local. The shared ImGui overlay
draws the guide using the current mouse position and authoritative server layout
bounds. Hovering does not send requests or render new PNGs. Interactive native GW
also uses this overlay; the non-ImGui rendering path retains its Skia fallback.

### Annotation tracks and separators

Start the server with repeatable `--track PATH` arguments, for example
`--track test/test.gff3 --track test/server_demo.bed`. Track files remain on the
server. Existing feature popups show information from clicked annotations.

Hover a track boundary to get the resize cursor, then drag its horizontal preview
line. Releasing sends one `track-resize INDEX Y` command (physical Y coordinate).
The server applies GW's native sizing/minimum-height rules. Boundary 0 resizes the
whole track panel; later boundaries redistribute adjacent track heights. Escape,
focus loss and viewport-size changes cancel the drag. Native desktop dragging
retains its existing behavior.

Track geometry/names are returned in the `tracks` state field; no feature dataset
is mirrored in the client. Tests: `python3 test/test_remote_tracks.py URL`.

## Embed in an application

Open `/embed.html` on gw-server for a full-window embedded GW canvas. Navigation stays inside the native UI. The normal `/` demo remains available. The iframe uses
GW's existing native ImGui UI, not a second toolbar implementation.

Serve `embed.mjs` with your application (or import `/embed.mjs` from
GW's server), then give the container an explicit height:

```js
import {createViewer} from '/embed.mjs';
const viewer = createViewer(document.querySelector('#genome'), {
  url: 'http://127.0.0.1:8766/',
  session: 'default',
  onState: view => console.log(view.shared.regions),
  onError: error => console.error(error)
});
await viewer.ready;
await viewer.navigate('chr1:9900-10300');
await viewer.command('refresh');
await viewer.settings({scale_bar: true}); // use settings supported by your server
// Await completion before submitting another host action.
// await viewer.snapshot();
// await viewer.destroy();
```

`navigate`, `command`, `settings`, and `snapshot` return promises. A busy gesture
or render rejects host actions rather than applying them against a stale frame;
retry after the next state event. `onState` receives the versioned protocol view
once its PNG and UI metadata are installed. `focus()` focuses the viewer frame.
Resizing the container uses the existing framebuffer/DPI resize path. Each mount
has its own iframe/WASM instance, keyboard focus, viewport, and client identity;
instances using the same session share regions/settings according to the protocol.
The native demo currently allows eight clients.

For React, create the viewer in an effect using a container ref, and call
`void viewer.destroy()` in the effect cleanup. No React runtime is required by GW.
The iframe boundary isolates Emscripten's singleton runtime. Messages verify the
exact origin, iframe window and per-instance channel. The host must permit framing
and send a Referer containing its origin (the usual browser default); a
`no-referrer` policy prevents the handshake. Cross-origin deployments must also
configure their frame/CSP policies. Authentication and authorization remain the
server's responsibility. Existing deployments can supply a custom transport in
an alternative shell using `Module.viewerTransport`.

This is the **remote-rendered** component. Dropped BAM files are uploaded to the
server; they are not read directly by WASM. Touch support is not yet complete. The full
local-data WASM demo remains in `deps/wasm`.


### BAM drag and drop (local demo)

Drop one coordinate-sorted BAM onto `/embed.html` with its matching `.bai` or
`.csi`, or choose them through Open file. Selecting a BAM alone retains it locally
and displays “Waiting for index”; select/drop the matching index later to proceed.
No data is uploaded before the pair is complete, and the server never builds an
index. The browser uploads 4 MiB chunks. A separate protocol host opens the upload at its first
mapped read, using the reference configured on gw-server. Chromosome naming must
match that reference. FASTA upload and reference switching are not implemented.
An incompatible BAM returns an error and leaves the previous view available.

The optional `uploadDataset(files, {url, onProgress})` export implements this demo
extension and returns `{transportBase, dispose}`. Pass `transportBase` into
`createViewer`, and call `dispose()` after destroying the viewer when finished.
`onFiles(files)` receives files dropped inside the embedded frame. A host may
replace this extension with its own storage/database workflow; it is not part of
the versioned viewer wire contract.

The demo accepts four upload sessions, at most 8 GiB per file. Temporary files
live in owner-only directories, are removed after 30 minutes without activity,
and are removed on normal server shutdown. Replacing an uploaded view deletes
its old session. Uploads are transferred to the server, so this is intended for
the localhost demo; hosted deployments need their own authorization and quotas.

`createFileLoader({onReady, onStatus})` provides the same pairing state machine to
embedding hosts. Its `accept(files)` handles joint or sequential selections;
`waiting` indicates an outstanding index and `cancel()` clears an idle selection.
Open file also works in the standalone WASM shell. The current browser picker
supports BAM plus BAI/CSI; reference switching and annotation/VCF uploads need
separate dataset-loading adapters. Opening a new BAM creates an isolated view;
closing a BAM or track is shared with the other clients in that view. A view with
no BAMs remains usable and can be populated again through Open file.
