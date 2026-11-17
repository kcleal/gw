---
title: C++ API (libgw)
layout: default
parent: API and integrations
nav_order: 1
description: Embedding GW through the libgw C++ API
---

# C++ API (libgw)

GW's renderer is available as a library, `libgw`, centred on the `Manager::GwPlot` class. The v2
API around `GwPlot` is more stable than v1. The public class is declared in
`include/libgw/plot_manager.h`; `include/libgw/libgwplot.hpp` aggregates the public headers.

Build the shared library with:

```shell
make shared    # produces libgw.so / libgw.dylib
```

## Core concepts

- `Manager::GwPlot` owns window state, loaded files (BAM/CRAM, tracks, variants), active regions,
  read collections, popup state and command execution context
- `selectedAlign` is the current SAM record used by commands such as `mate add`, `sam`, `tags`
  and `find`
- Input events are delivered through `keyPress`, `mouseButton`, `mousePos` and `scrollGesture`,
  mirroring the GLFW callbacks

## Common operations

```cpp
Manager::GwPlot plot;
plot.init(width, height);                 // create framebuffers
plot.loadGenome("hg38", std::cerr);       // genome tag or fasta path
plot.addBam(bam_path);
plot.addTrack(track_path, true, false, false);
plot.fetchRefSeqs();
plot.processBam();
plot.setScaling();
plot.drawScreen(canvas, sContext, sSurface);  // render a frame to a Skia surface
plot.rasterToPng("out.png");                  // save the current raster
```

Command-box commands can also be run programmatically through the command parser; sessions can
be saved/restored with `loadSession`.

## Remote-viewer session adapter

The optional `deps/protocol/viewer/native` layer (`GwViewer::Session`, built via `make server-core`)
wraps `GwPlot` for the [remote viewer protocol]({{ site.baseurl }}/api/protocol.html):
navigation, command execution, input handling, layout/popup state, frame caching and PNG
encoding. It exposes standard C++ types only — no Skia, GLFW or server-framework types cross the
interface.

The desktop and `libgw` targets do not compile this adapter; it is an explicit integration
choice, not a dependency.

For language bindings:
- Python (gwplot): https://github.com/kcleal/gwplot
- JavaScript (gwplot-js): https://github.com/APridgeon/gwplot-js
