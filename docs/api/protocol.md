---
title: Remote viewer protocol
layout: default
parent: API and integrations
nav_order: 2
description: The GW viewer protocol — embed GW in web applications
---

# Remote viewer protocol
{: .no_toc }

GW v2 adds a versioned remote protocol that drives a GW renderer over the network and embeds it
in web applications — including a WASM build of GW running the same ImGui overlay in the browser.

The protocol is defined in `deps/protocol/viewer/v1/`; the authoritative spec is
`deps/protocol/viewer/v1/README.md`, with the JSON Schema in `deps/protocol/viewer/v1/schema.json`.

<details open markdown="block">
  <summary>Table of contents</summary>
  {: .text-delta }
1. TOC
{:toc}
</details>

## At a glance

- Every message carries `protocol: "gw-viewer"` and `version: 1`, plus `request_id` and
  `session_id`. Breaking changes require a new major version.
- Two transport functions: a JSON **message** endpoint and a binary **frame** (PNG) endpoint.
  Reference HTTP binding:
  - `POST /viewer/v1/message` — JSON protocol message → `welcome`, `state`, `ack` or `error`
  - `POST /viewer/v1/frame` — `{session_id, client_id, connection_id, frame_id}` → `image/png`
- A WebSocket adapter can implement the same two functions.
- Servers provision sessions against trusted datasets; clients cannot supply filesystem paths.

## Sessions, views and frames

A client **joins** a session with its viewport (logical pixels and pixel ratio). The server
replies with a `welcome` containing `client_id`, `resume_token`, capabilities and a complete
`view`:

- `view.shared` — session state: regions, alignment display names, theme, settings
- `view.client` — per-client state: active region, popups, command output
- `view.frame` — an immutable frame descriptor (`frame_id`, dimensions, layout); the matching PNG
  is fetched separately from the frame endpoint

## Actions

Clients send ordered **actions**: `navigate`, `command`, `settings`, `pointer`, `key`,
`vscroll` and `track_resize`. Command strings are interpreted by GW's own command parser, so the
full [command set]({{ site.baseurl }}/guide/commands.html) works remotely. Coordinates are
logical pixels relative to the canvas; key names use browser `KeyboardEvent.code` names.

Actions carry a monotonically increasing `sequence` and an `expected_revision`. The server
rejects stale revisions and advances the shared revision only when shared state changes — so
multiple clients can share a session while popups, hover and scroll offsets stay private.

## Reconnect and heartbeats

Clients send heartbeats within the advertised interval; the server replies with the current
revision so clients fetch a snapshot after changes. Reconnect by re-joining with the previous
`client_id` and `resume_token` — the server returns a complete snapshot. `leave` releases a
client without deleting the session.

## Integrating

**Browser / React** — use the bundled adapter:

```js
import {ViewerClient, httpTransport} from './client.mjs';

const client = new ViewerClient(httpTransport({
  messageUrl: '/viewer/v1/message',
  frameUrl: '/viewer/v1/frame',
  headers: () => ({Authorization: getAccessToken()})
}), {
  onView: async (view, pngBytes) => displayInWasm(view, pngBytes),
  onRevision: () => client.snapshot().catch(reportError)
});
await client.join('default', {width: 1000, height: 650, pixel_ratio: 2});
```

The adapter has no DOM, WASM globals, fixed URLs or timers — the host owns sizing, focus,
authentication and error recovery.

**Python** — `protocol.viewer.v1.Router` is a transport-neutral reference router. Implement its
`Backend` over your existing GW/gwplot sessions, then mount the included WSGI adapter (or call
`router.handle` / `router.frame` from ASGI routes). Authorization is required for both messages
and PNG reads; the demo binds only localhost.

**Native C++ server** — `make server` builds `gw-server`, which translates protocol messages into
`GwViewer::Session` actions from the [native adapter]({{ site.baseurl }}/api/libgw.html).

## Security

- Session IDs and resume tokens are not a substitute for authentication
- Do not log resume credentials
- A hosted deployment must add authentication/authorization and resource limits
