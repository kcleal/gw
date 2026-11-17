---
title: API and integrations
layout: default
nav_order: 5
has_children: true
has_toc: false
description: The GW v2 API — deployment models, libgw, remote viewer protocol and Python bindings
---

# API and integrations

GW v2 offers a more stable programmatic interface than v1: embed the renderer over the C++ API,
drive it over a versioned remote protocol, or script it from Python. Use the menu on the left to
pick a page.

## Ways to run GW

GW can run in several configurations. Two choices decide the behaviour: **where the instance
runs** (your machine or a server), and **how it is compiled** (native or WASM).

| | **Native** | **WASM** |
| --- | --- | --- |
| **Local** | The `gw` desktop app and `libgw`. Renders on your machine with Skia/OpenGL. | GW compiled to WASM runs in the browser, rendering locally with WebGL2. Nothing to install. |
| **Server** | `gw-server` runs GW headless on a server. Renders there, streams PNG frames to browser clients over the viewer protocol. Data never leaves the server. | A WASM client in the browser connects to a remote server. The server renders; the browser draws the frames and runs the ImGui UI. |

### Where rendering happens

Rendering always happens in one place per configuration, and knowing where explains the tradeoffs:

- **Native local** — rendered on your machine. Fastest option; large BAMs and deep coverage are
  no problem. Requires a build (see [Install]({{ site.baseurl }}/install.html)).
- **WASM local** — rendered in your browser. Nothing to install and runs anywhere, but the data
  must be uploaded to the browser, and performance is bounded by the browser's memory and WebGL.
  Best for small-to-medium datasets and for sharing views with people who cannot install GW.
- **Server, native or WASM client** — rendered on the server. Clients are thin: they display
  server-drawn frames and send input back. The BAM/FASTA files stay on the server, which suits
  large or sensitive datasets and many simultaneous users. Cost: server CPU/GPU per client, and
  frame delivery over the network.

### WASM modes

A WASM build can run in two modes:

- **Headless** — rendering only, no GW user interface. The host application owns all UI. Useful
  for embedding a genome view inside a larger custom interface.
- **With ImGui** — the full GW overlay (menus, settings, popups, command box) runs in the
  browser, the same interface as the desktop app. The remote WASM client uses this mode against a
  rendering server.

## Which interface should I use?

**Python bindings (gwplot)** — for scripting and local analysis: batch image generation,
pipelines, iterating over regions in code. Runs a GW renderer in-process. Start here if you are
generating figures or working in a Python pipeline.

**JavaScript bindings (`client.mjs` / `embed.mjs`)** — for embedding a GW view in a web
application: a host page creates a viewer, navigates it and receives state. Start here if you
have a web app and want a genome view inside it.

**Viewer protocol** — for serving GW from your own backend: multi-client shared sessions,
custom authentication, custom transports (HTTP or WebSocket), or a non-Python/non-JS server. The
protocol is the common contract underneath the JavaScript adapter; reach for it when the bindings
are not enough.

The ordinary gwplot API is unchanged in v2; existing integrations keep working.
