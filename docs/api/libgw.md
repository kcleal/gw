---
title: C++ API (libgw)
layout: default
parent: API and integrations
nav_order: 1
description: Running GW headless from C++ with libgw
---

# C++ API (libgw)
{: .no_toc }

`libgw` is GW built as a library. A program creates a `Manager::GwPlot`, loads data into it, runs
the same commands as the command bar, and draws to an off-screen canvas — no window or GPU needed.
This is how the [Python]({{ site.baseurl }}/api/python.html) and JavaScript bindings work.

1. TOC
{:toc}

## Building

```shell
make clean
make shared    # produces libgw/libgw.so (or libgw.dylib) and headers in libgw/GW
```

{: .warning }
Run `make clean` first. Symbols are only exported when the objects are compiled by the `shared`
target; a library linked from objects left over from a plain `make` has no public symbols and
your program will fail to link.

Compile against the bundled headers and link `libgw` and htslib:

```shell
c++ -std=c++17 example.cpp -I libgw/GW -L libgw -lgw -lhts -Wl,-rpath,$PWD/libgw -o example
```

`libgwplot.hpp` includes everything needed.

## The main stages

A headless program always follows the same five stages:

| Stage | What happens |
|-------|--------------|
| **1. Options** | Fill a `Themes::IniOptions`: theme, image size, thresholds. `readIni()` loads the user's `.gw.ini` |
| **2. Construct** | `GwPlot` opens the reference and any BAMs, regions and tracks given to the constructor |
| **3. Canvas** | `setImageSize()` then `makeRasterSurface()` allocate the off-screen image |
| **4. Commands** | Set `inputText` and call `commandProcessed()` for each command |
| **5. Draw and save** | `drawScreen()` loads the data in view and renders it; `rasterToPng()` or `encodeToPng()` returns the image |

Stages 4 and 5 repeat for as long as you like: navigate, draw, save, navigate again.

## Example

Draw a region with a filter and a sort applied, print read counts, then move and draw again:

```cpp
#include <iostream>
#include <string>
#include <vector>
#include "libgwplot.hpp"

int main() {
    // 1. Options
    Themes::IniOptions opts;
    if (opts.readIni()) {              // use the user's .gw.ini if there is one
        opts.getOptionsFromIni();
    }
    opts.theme_str = "slate";          // igv, dark, slate, tide or frost
    opts.setTheme(opts.theme_str);
    opts.dimensions = {1200, 600};     // image size in pixels
    opts.no_show = true;

    // 2. Construct
    std::vector<std::string> bams = {"sample.bam"};
    std::vector<std::string> tracks;
    std::vector<Utils::Region> regions;
    std::string locus = "chr8:37047270-37055161";
    regions.push_back(Utils::parseRegion(locus));

    Manager::GwPlot plot("ref.fa", bams, opts, regions, tracks);
    plot.opts.theme.setAlphas();
    plot.drawToBackWindow = true;      // off-screen drawing
    plot.terminalOutput = false;       // keep command output in the log, not stdout

    std::string bed = "genes.bed";
    plot.addTrack(bed, false, false, true);

    // 3. Canvas
    plot.setImageSize(opts.dimensions.x, opts.dimensions.y);
    plot.makeRasterSurface();

    // 4. Commands
    auto run = [&](const std::string &command) {
        plot.inputText = command;
        plot.commandProcessed();
    };
    run("filter mapq >= 20");
    run("sort strand");

    // 5. Draw and save
    plot.drawScreen();
    plot.rasterToPng("region.png");

    // Commands that report on reads need the reads loaded, so run them after a draw
    run("count");
    std::cout << plot.flushLog() << std::endl;

    // Navigate and draw again
    run("chr8:37050000-37052000");
    plot.drawScreen();
    auto png = plot.encodeToPng();     // {pointer, size} of an in-memory PNG
    std::cout << png.second << " bytes\n";
    return 0;
}
```

`drawScreen()` does the whole job of a frame: it fetches the reference sequence, reads the
alignments for the regions in view, lays everything out and renders. There is no need to call
`fetchRefSeqs()`, `processBam()` or `setScaling()` yourself.

## Running commands

Any [command]({{ site.baseurl }}/guide/commands.html) that works in the command bar works here:
a locus or feature name to navigate, `add`/`rm` for regions, `filter`, `sort`, `link`, `theme`,
`ylim`, `load`, `expand-tracks` and so on. Commands change state only; nothing is drawn until the
next `drawScreen()`.

With `terminalOutput = false`, text output collects in the plot's log. `flushLog()` returns it
and empties the log.

## Loading data later

Data does not have to be passed to the constructor:

```cpp
std::string bam = "other.bam";
plot.addBam(bam);

std::string vcf = "calls.vcf.gz";
plot.addTrack(vcf, false, true, true);   // path, print message, VCF as track, BED as track

plot.addIdeogram("cytoBand.bed");
plot.removeBam(0);
plot.removeTrack(0);
```

The path arguments are non-const references, so pass a named `std::string`, not a literal.

## Output

| Function | Result |
|----------|--------|
| `rasterToPng(path)` | Write the last drawn frame to a PNG file |
| `encodeToPng(level)` | PNG bytes in memory, as `{pointer, size}`. The buffer is owned by the plot and replaced by the next call |
| `encodeToJpeg(quality)` | JPEG bytes in memory (not available on Windows) |
| `saveToPdf(path)` / `saveToSvg(path)` | Redraw the view as a vector image |
| `resizeCanvas(width, height)` | Change the image size, keeping filters and regions |

## Gotchas

- **Draw before asking about reads.** Reads are loaded by `drawScreen()`, not by the constructor
  or by navigating. `count` and similar commands run before the first draw report nothing — and
  `count` then marks the view as up to date, so the next draw comes out without reads.
- **Make the canvas before drawing.** `drawScreen()` and the output functions need
  `makeRasterSurface()` to have been called. Call `resizeCanvas()` to change size afterwards.
- **A missing reference or BAM ends the program.** The constructor calls `std::exit` if the
  reference cannot be opened or a BAM in the constructor list fails to load. Check paths first, or
  add BAMs with `addBam()`, which returns `false` instead.
- **Call `setAlphas()`** on `plot.opts.theme` after construction, as GW itself does, so the
  theme's transparency settings are applied.
- **Set `terminalOutput = false`** unless you want command output on stdout — important if your
  program writes an image to stdout.
- **One plot per thread.** A `GwPlot` is not thread-safe. To draw in parallel, give each thread
  its own instance.
- **Region coordinates** passed to `Utils::parseRegion` are written as in the command bar
  (`chr1:1-20000`).
- **The `.gw.ini` is the user's.** `readIni()` picks up their themes, thresholds and genome tags,
  so output can differ between machines. Skip it for reproducible images and set the options you
  need.
- **Dialog commands.** gwplot creates a Dear ImGui context (`ImGui::CreateContext()`) before
  running commands. The commands in the example run without one; do the same as gwplot if you send
  commands that open GW's own dialogs or drive input events.

## Input events

For an interactive embedding, forward input with `mouseButton`, `mousePos`, `scrollGesture` and
`keyPress`, which mirror the GLFW callbacks, then draw.

## Remote-viewer session adapter

The optional `deps/protocol/viewer/native` layer (`GwViewer::Session`, built via `make server-core`)
wraps `GwPlot` for the [remote viewer protocol]({{ site.baseurl }}/api/protocol.html):
navigation, command execution, input handling, layout/popup state, frame caching and PNG
encoding. It exposes standard C++ types only — no Skia, GLFW or server-framework types cross the
interface. The desktop and `libgw` targets do not compile this adapter.

## Language bindings

- Python — [gwplot](https://github.com/kcleal/gwplot)
- JavaScript — [gwplot-js](https://github.com/APridgeon/gwplot-js)
