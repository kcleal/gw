---
title: The interface
layout: default
parent: User guide
nav_order: 1
description: The GW version 2 ImGui interface
---

# The interface
{: .no_toc }

GW v2's overlay UI is built with [Dear ImGui](https://github.com/ocornut/imgui): menus, settings
dialogs, read popups, the command box, label tables and file browsers, all rendered on top of the
plot window.

![GW window with numbered interface regions]({{ site.baseurl }}/assets/images/v2/interface_overview.png)

## The plot window

The plot area responds to the mouse directly:

- **Click** a read to open the read popup; click the coverage or reference for a per-base popup
- **Drag** to zoom; scroll to zoom in/out at the cursor
- **Drag** pane boundaries to resize them (e.g. the track pane)

The view can be split into multiple regions (side-by-side) and multiple BAMs stacked vertically —
see [Commands]({{ site.baseurl }}/guide/commands.html).

## Top menu

The hamburger button (top-left) opens the main menu: open files, settings, save images/sessions,
browse online genomes and files, open the label table. The view dropdown next to it controls
layout options (grid lines, coverage scale, track expansion and more).

![view_menu]({{ site.baseurl }}/assets/images/v2/view_menu.png)

![The main menu]({{ site.baseurl }}/assets/images/v2/top_menu.png)

## Command bar

The command bar sits along the bottom of the window. Press `/` or click it to type; a scrollable
list shows every command (ticks mark switches that are on) and narrows as you type. When idle it
shows the genomic position under the pointer. Hide or show it from the main menu (**Command bar**);
**Output** in the same menu shows command output.

![command_bar]({{ site.baseurl }}/assets/images/v2/command_bar.png)

Type `help` for a summary or `man COMMAND` for details.

- `↑` / `↓` cycles through command history
- Unrecognised text is echoed to the terminal (useful for notes)
- Commands can also be applied at startup with `-c 'sort strand'`

## Read popups

Clicking a read selects it and opens a popup with read details (SAM fields, tags, mate info) and
read-specific actions such as `mate add`, `find`, `sam` and `tags`. Each popup keeps its own read
context, so popups stay valid after you click elsewhere.

![Read popup with the sequence around the clicked base]({{ site.baseurl }}/assets/images/v2/read_popup.png)

## Settings dialogs

The settings editors (General, Genomes, Interaction, Labelling, Navigation, Tracks, Thresholds,
Keymap) are ImGui dialogs with live preview. Changes apply immediately and save to `.gw.ini`. See
[Settings]({{ site.baseurl }}/guide/settings.html).

## Label table and online files

When labelling variants, the label table dialog shows progress through the variant list. The
online files dialog browses remote genomes and data catalogues, searches
[UCSC tables]({{ site.baseurl }}/guide/remote.html#ucsc-tables), and downloads files into a
session. See [Labelling]({{ site.baseurl }}/guide/labelling.html) and
[Remote access]({{ site.baseurl }}/guide/remote.html).

## Startup genome picker

When GW starts without a reference genome, a **Reference genome** chooser opens in the window:
pick a configured or online genome, filter the list, or open a local FASTA. Start with `--wait`
to open an empty window with no chooser (useful when GW is driven by another program).

![genome_chooser]({{ site.baseurl }}/assets/images/v2/genome_chooser.png)
