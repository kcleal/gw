# GW viewer protocol v1

An opt-in interface for the remote WASM UI, oat++ servers and Python/gwplot
servers. It adds no dependency or ABI change to `Manager::GwPlot`. The ordinary
gwplot API does not change. JSON Schema is the wire contract; `types.hpp` holds
dependency-free C++ value types, `client.mjs` is the browser adapter, and
`server.py` is a transport-neutral Python reference router.

The current remote demo uses this protocol. The oat++ implementation is in
`gw-server/src/protocol.hpp`; the old `/api/*` endpoints remain a separate legacy
view for existing callers. A deployment should choose one protocol for each
viewer, rather than mix their revisions or frame identifiers.

## Messages and transport

Every request carries `protocol: "gw-viewer"`, `version: 1`, `type`, a unique
`request_id`, and `session_id`. Servers provision sessions against trusted
datasets; clients cannot supply filesystem paths. A server may host several
independent sessions. The demo provisions one shared session named `default`.

HTTP binding used by the demo:

| Endpoint | Request | Response |
| --- | --- | --- |
| `POST /viewer/v1/message` | JSON protocol message | JSON `welcome`, `state`, `ack` or `error` |
| `POST /viewer/v1/frame` | `{session_id, client_id, connection_id, frame_id}` | Binary `image/png` |

Endpoints and authentication are supplied to the browser adapter. A WebSocket
adapter can implement the same two functions, associating binary messages with
frame IDs. No URLs, network code, server framework types or browser dimensions
are part of GW's native drawing API. PNG bytes are never base64 encoded in JSON.
Responses and frames must not be cached by proxies or service workers.

The protocol accepts namespaced `extensions` objects. Unknown optional extension
members are ignored. Unknown message types/actions are rejected explicitly;
breaking changes require a new major version. Servers advertise supported action
names and optional codecs in `welcome.capabilities`. Clients must not infer
support from a version number alone. Schema validation is supplemented by the
stateful and numeric rules below.

## Joining and viewports

```json
{
  "protocol": "gw-viewer",
  "version": 1,
  "type": "join",
  "request_id": "unique-join-request",
  "session_id": "default",
  "viewport": {"width": 1000, "height": 650, "pixel_ratio": 2}
}
```

`width` and `height` are **integer logical canvas/container pixels**, not browser
window dimensions. The embedding app observes its container and sends a
`viewport` update when its size or device pixel ratio changes. Physical dimensions
are `floor(logical_size * pixel_ratio + 0.5)` independently on each axis. Return
those exact dimensions, or reject the viewport; do not silently downsample.
Dimensions must be 200..8192 by 100..8192 physical pixels, at most 32 megapixels.
Logical dimensions are 1..8192; pixel ratio is 0.25..8. Hide/suspend zero-size
containers until usable dimensions are available.

A welcome supplies server-generated `client_id`, `connection_id`, `resume_token`,
`last_sequence`, `heartbeat_seconds`, capabilities and a complete `view`. A view
contains shared state, private client state and an immutable frame descriptor:

```text
view.revision                 shared-session revision, decimal string
view.shared                   shared regions/render settings
view.client                   this client's popup/selection/output state
view.frame.frame_id           opaque identity for one image and its layout
view.frame.revision           exactly view.revision
view.frame.viewport           logical dimensions and pixel ratio used
view.frame.width / height     actual PNG dimensions
view.frame.mime_type          image/png
view.frame.layout             geometry and backend UI codec metadata
```

Fetch the matching PNG and display it together with its layout/state. Do not
apply metadata before the image has decoded. A frame belongs to one client and
connection. Bounded frame storage may expire older IDs; request a snapshot when
that happens. A retried response may reference a frame that has since expired.

## Actions, ordering and sharing

After joining, every message includes the three identity fields, `request_id`,
and a monotonically increasing `sequence`. Actions also carry
`expected_revision`, and layout-dependent actions carry `frame_id`:

```json
{
  "protocol": "gw-viewer", "version": 1, "type": "action",
  "request_id": "unique-action-request", "session_id": "default",
  "client_id": "client-from-welcome", "connection_id": "connection-from-welcome",
  "sequence": "1", "expected_revision": "1", "frame_id": "displayed-frame",
  "action": {
    "type": "pointer", "phase": "press", "button": "left",
    "x": 420, "y": 180, "modifiers": []
  }
}
```

All counters are canonical positive **uint64 decimal strings**, avoiding JS's
integer precision limit. `last_sequence` alone may be `"0"`. Bound counters by
18446744073709551615; the schema's string pattern alone cannot enforce that limit.

Actions are `navigate`, `command`, `settings`, `pointer`, `key`, `vscroll` and
`track_resize`; their payloads are defined in `schema.json`. Pointer, key,
vscroll and track resize require a displayed frame ID. Coordinates are logical
pixels relative to the canvas; native GW multiplies by that frame's pixel ratio.
Captured drags may leave the canvas. GW currently clamps to the frame bounds.
Wheel deltas are normalized native **steps**, rather than browser pixel deltas.
Key names use browser `KeyboardEvent.code` names; no GLFW integer constants cross
this interface. Text entry and ImGui-owned interactions stay local. Send command
strings on submission; the native server uses GW's command parser.

Servers serialize shared mutations, reject stale revisions/layouts, and advance
the shared revision only when shared state changes. Viewport changes, popups,
hover, gesture drafts and vertical read offsets are private. Shared edits cancel
other clients' obsolete gestures. Clients serialize actions, coalesce queued
moves and commit local track/scale/scrollbar previews on release as appropriate.
Never silently retry a rejected coordinate action against a different layout.

Server replies to heartbeat with the current revision. If it differs from the
displayed revision, fetch a full `snapshot`. The demo polls this lightweight
message once per second, fetching PNGs only after a change. A transport may also
provide revision notifications; they should trigger snapshot reconciliation.

Valid authenticated sequences are consumed even when their action is rejected.
Retain the exact request after an ambiguous network failure and retry that
envelope (same identity, sequence, request ID and payload). Recent responses are
cached; a duplicate is never applied twice. Evicted sequences return
`duplicate_expired`, which requires a fresh snapshot. Changed payloads with the
same sequence return `sequence_conflict`. A backend failure after mutation must
also cache its error; clients must not create a new action to repeat it blindly.

## Disconnect and reconnect

The application sends heartbeats within the advertised interval and the server
runs cleanup periodically even without incoming traffic. Defaults are 30 seconds
without activity, then a 120-second reconnect grace period. A separate 15-second
gesture lease prevents a lost release remaining active while heartbeats continue.
Transport disconnect notifications may cancel gestures immediately.

Reconnect with `join`, including the previous `client_id` and `resume_token`.
The server cancels gestures, replaces the connection ID, preserves the sequence
high-water mark and returns a complete snapshot. Old connections cannot submit
actions or fetch frames. Expired credentials require a new client. New-client
joins are not automatically retried if their welcome is lost: an orphaned client
is eventually collected. Resume credentials belong to one browser tab; do not
share them across tabs.

`leave` releases one client; it never deletes the shared session. Session closure
is a separate, authorized host operation. The Python router supports explicit
disconnect and session closure; the oat++ demo detects browser loss through its
timer because its transport is stateless HTTP.

## Browser / React integration

```js
import {ViewerClient, httpTransport} from './client.mjs';

const client = new ViewerClient(httpTransport({
  messageUrl: '/viewer/v1/message', frameUrl: '/viewer/v1/frame',
  headers: () => ({Authorization: getAccessToken()})
}), {
  // Resolve only after image and metadata have been committed together.
  onView: async (view, pngBytes) => displayInWasm(view, pngBytes),
  onRevision: () => client.snapshot().catch(reportError)
});
await client.join(sessionId, {width: 1000, height: 650, pixel_ratio: 2});
```

The adapter has no DOM, WASM globals, fixed URLs or timers. The host owns container
sizing, focus, authentication, heartbeat scheduling and error recovery. `retry()`
reuses an uncertain request. `resumeCredentials` is used for reconnect. The demo
injects `Module.viewerTransport` to replace HTTP, and `Module.viewerSession` to
select a session. `Module.viewerProtocolReady` loads this module before connecting.
The demo's existing canvas event queue translates native UI callbacks into these
messages and retains client-side ImGui previews.

## Python / gwplot integration

Import `protocol.viewer.v1.Router` from this checkout (or vendor/install this
directory in the host application). There is no automatic dependency on gwplot:
implement `Backend` using the application's existing GW/gwplot session ownership.
It provides `snapshot`, `apply`, `render`, `cancel` and `release_client`.
`apply` must be atomic on failure and return whether shared state changed.
`render(client_id, viewport)` must return `Render(png, layout, client)` without
changing shared state. Cancel/release must not change the shared view either.
Keep viewport and gesture state private to each renderer; a single mutable
gwplot framebuffer cannot safely represent simultaneous clients.

```python
from protocol.viewer.v1 import Router
from protocol.viewer.v1.wsgi_adapter import application

router = Router()
router.add_session("default", existing_gwplot_backend)
wsgi_app = application(router, authorize_session_access)
# Mount wsgi_app at /viewer/v1, or call router.handle/frame from ASGI routes.
# Schedule router.sweep() periodically; call disconnect() on socket loss.
```

The WSGI binding requires authorization for both messages and PNG reads. Session
IDs and reconnect tokens are not substitutes for authentication. The native demo
binds only localhost and supplies no user accounts; a hosted deployment must add
authentication/authorization and resource limits. Do not log resume credentials.

## Current GW UI payload codec

The core protocol leaves `shared`, `client` and `layout` as objects so renderers
can evolve independently of transport. The existing GW ImGui adapter currently
uses `frame.layout.encoding: "gw-ui-text-1"`:

* `shared`: `regions` (GW region strings), `alignments` (display names), `theme`,
  and `settings` (numeric settings object matching `viewer_settings.h`).
* `client`: `active_region`, `event_id` (private renderer revision), `popups`,
  and `output`. Deduplicate popup/output consumption by `event_id`, not by shared
  revision: a click can open a popup without changing shared state.
* `layout.scrollbars` and `layout.tracks`: existing native GW layout records;
  geometry in these codec-specific strings is **physical pixels**. Popup records
  are likewise the existing quoted GW UI records. `protocol/viewer/native/viewer_interface.cpp`
  defines their generation, and `deps/wasm_remote/main.cpp` their reader.

These legacy UI records are isolated from action/viewport/frame envelopes. Future
structured UI codecs should advertise a different encoding/capability, leaving
the transport and session lifecycle unchanged. A gwplot backend targeting this
specific frontend must provide this codec, rather than only arbitrary state.

## Checks

```sh
python3 test/test_viewer_protocol.py
node test/test_viewer_protocol.mjs
# After rebuilding the WASM demo, also check the generated EM_JS conversion:
node test/test_viewer_protocol_bridge.mjs
# Fixture server: test.fa, test.bam, test.gff3 and server_demo.bed
python3 test/test_viewer_protocol_http.py http://127.0.0.1:8767
node test/test_viewer_protocol_http.mjs http://127.0.0.1:8767
```

The Python suite uses only the standard library plus Node for one cross-language
check. If available, `jsonschema` also validates real request/reply examples.
