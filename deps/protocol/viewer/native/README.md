# Native GW viewer adapter

This optional C++ layer connects the remote viewer protocol to GW's existing
renderer and interaction code. It lets a server keep persistent GW renderers,
apply viewer actions, return PNG frames and expose the state needed by the
browser's native ImGui overlay. It is not an HTTP server or a JSON parser.

## Files

* `viewer_interface.h` declares `GwViewer::Session`, its configuration, actions
  and state. The header exposes standard C++ types without Skia, GLFW, oat++ or
  Python types.
* `viewer_interface.cpp` implements the session using `Manager::GwPlot`: dataset
  validation, navigation, command execution, input handling, layout/popup state,
  frame caching and lossless PNG encoding. It also supports copying shared view
  state between client renderers while keeping their viewports, file handles,
  selections and gestures separate.
* `viewer_settings.h` maps the supported numeric display settings to and from
  `Themes::IniOptions`. Both the native server and WASM UI use it to synchronize
  rendering controls.

## Relationship to the protocol

The [versioned protocol](../v1/README.md) defines JSON messages, client/session
identities, revisions, frame IDs and connection lifecycle rules. This directory
implements the GW-specific rendering operations beneath that contract. Keeping
the adapter outside `v1` allows its implementation to change without requiring a
new wire-protocol version.

gw-server translates JSON requests into `GwViewer::Action` values and translates
session state into protocol responses. Its host manages client ownership,
serialization, shared revisions, reconnects, timeouts and frame delivery. It must
serialize access to each `Session`, including frame reads. Frame bytes remain
owned by the session and are valid until its next render.

The WASM frontend includes the interface's value types and settings helpers to
maintain its local UI mirror. It does not compile the native session
implementation or load the server's BAM/FASTA files.

## Build and API scope

`make server-core` explicitly compiles `viewer_interface.cpp` into
`libgw_server_core.a`; `make server` links that archive into gw-server. The normal
desktop/shared-library targets do not compile this adapter. Includes from other
directories use `deps/protocol/viewer/native/viewer_interface.h` or
`deps/protocol/viewer/native/viewer_settings.h` relative to the GW repository root
(or `protocol/viewer/native/...` with `deps/` on the include path).

This layer does not replace or change the existing `Manager::GwPlot` API used by
gwplot. Existing gwplot integrations can continue using that API. Adopting the
remote protocol is an explicit integration choice, not an automatic dependency.
