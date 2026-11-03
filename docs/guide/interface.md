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

<details open markdown="block">
  <summary>Table of contents</summary>
  {: .text-delta }
1. TOC
{:toc}
</details>

<div style="border: 2px dashed #5c5c5c; border-radius: 8px; padding: 3rem 1rem; text-align: center; color: #8a8a8a; margin: 1.5rem 0;">
  <strong>Screenshot placeholder</strong> — GW v2 window with labelled UI regions<br>
  <code>docs/assets/images/v2/interface_overview.png</code>
</div>

## The plot window

The plot area responds to the mouse directly:

- **Left-click** a read to select it; **right-click** for the read popup
- **Drag** to zoom; scroll to zoom in/out at the cursor
- **Drag** pane boundaries to resize them (e.g. the track pane)

The view can be split into multiple regions (side-by-side) and multiple BAMs stacked vertically —
see [Commands]({{ site.baseurl }}/guide/commands.html).

## Top menu

The hamburger button (top-left) opens the main menu: open files, settings, save images/sessions,
browse online genomes and files, open the label table. The view dropdown next to it controls
layout options (grid lines, coverage scale, track expansion and more).

<div style="border: 2px dashed #5c5c5c; border-radius: 8px; padding: 2rem 1rem; text-align: center; color: #8a8a8a; margin: 1rem 0;">
  <strong>Screenshot placeholder</strong> — the top menu, opened<br>
  <code>docs/assets/images/v2/top_menu.png</code>
</div>

## Command box

Press `:` or `/` to open the command box. Type `help` for a summary or `man COMMAND` for details.

- `↑` / `↓` cycles through command history
- Unrecognised text is echoed to the terminal (useful for notes)
- Commands can also be applied at startup with `-c 'sort strand'`

## Read popups

Clicking a read selects it and opens a popup with read details (SAM fields, tags, mate info) and
read-specific actions such as `mate add`, `find`, `sam` and `tags`. Each popup keeps its own read
context, so popups stay valid after you click elsewhere.

<div style="border: 2px dashed #5c5c5c; border-radius: 8px; padding: 2rem 1rem; text-align: center; color: #8a8a8a; margin: 1rem 0;">
  <strong>Screenshot placeholder</strong> — a read popup<br>
  <code>docs/assets/images/v2/read_popup.png</code>
</div>

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

When GW starts without a reference genome, a terminal (ncurses) picker lists the available online
genomes — type a number or a genome tag (e.g. `hg38`). The same genomes can be selected from the
ImGui menus (Settings → Genomes, or the online files dialog).
