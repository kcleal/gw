---
title: The interface
layout: default
parent: User guide
nav_order: 1
description: A tour of the GW version 2 window
---

# The interface
{: .no_toc }

Everything in GW can be done with the mouse: menus, panels and popups sit on top of the plot.
The command bar and the command line are optional shortcuts to the same actions.

![GW window with numbered interface regions]({{ site.baseurl }}/assets/images/v2/interface_overview.png)

| | Part | What it does |
|---|------|--------------|
| **1** | [Main menu](#main-menu) | Open and close files, the Tracks and Tools panels, Settings and Help |
| **2** | [Quick display options](#quick-display-options) | Switch plot elements on or off; sort, hide and link reads |
| **3** | Region box | Shows the locus in view. Click it, type a locus or feature name and press `Enter` to go there |
| **4** | Region buttons | `+` adds a copy of the region as a new column; `x` closes the region |
| **5** | Scale bar and reference | Drag along the scale bar to zoom to a selection. Bases are drawn when zoomed in |
| **6** | Coverage | Read depth; the number on the left is the maximum. Click for per-base counts |
| **7** | Alignments | One row of reads per level. Click a read for its details |
| **8** | Scrollbar | Scroll through the read rows when they do not all fit |
| **9** | Tracks | Annotation tracks. Click a feature for its details; drag the line above to resize |
| **10** | Ideogram | Position on the chromosome. Click the chromosome name to pick another |
| **11** | [Command bar](#command-bar) | Shows the position under the pointer; press `/` to type a command |

File-name labels (for example `demo1.bam`) can be clicked to rename them.

## Moving around

- **Drag** left or right to pan; **scroll** to zoom at the pointer
- **Drag on the scale bar** (5) to zoom to the selected interval
- Type a locus (`chr1:1-20000`) or a feature name in the region box (3)
- Arrow keys pan and zoom — see [Hotkeys]({{ site.baseurl }}/guide/hotkeys.html)

Add regions with the `+` button (4) to view several loci side-by-side. Several alignment files
stack vertically.

![Two regions side by side]({{ site.baseurl }}/assets/images/v2/regions.png)

## Main menu

The button at the top-left (1).

![The main menu]({{ site.baseurl }}/assets/images/v2/top_menu.png){: width="288" }

| Item | Purpose |
|------|---------|
| **Open file** | **Local file** opens a file browser; **Online file** browses online genomes, data catalogues and UCSC tables |
| **Close file** | Choose loaded files to close |
| **Tracks** | The [track manager](#tracks-panel): reorder, resize and close alignments and tracks |
| **Variants** | The Variants panel — shown once a variant file is open as tiles |
| **Tools** | The [Tools panel](#tools-panel): count, filter, find, markers, save and sort |
| **Command bar** | Show or hide the command bar |
| **Output** | Text output from tools and commands |
| **Settings** | The [settings dialog]({{ site.baseurl }}/guide/settings.html) |
| **Help** | Help and the hotkey list |

Files can also be dropped onto the window.

## Quick display options

The second button (2) holds switches for what is drawn.

![The display options menu]({{ site.baseurl }}/assets/images/v2/view_menu.png){: width="289" }

- **Tick boxes** — alignments, coverage, insertions, mismatches, edges, soft-clips, base
  modifications, the cursor line, template-length y-axis, expanded tracks, log2 coverage and
  amino-acid translation
- **Sort** — by strand or haplotype
- **Hide** — duplicates, secondary or supplementary reads, MAPQ 0 reads; **Clear filters** removes
  them
- **Link** — draw lines between reads of a structural variant, or all read pairs
- **Refresh** — reset the view, including filters

## Tools panel

**Main menu → Tools**. Each section opens to a small form. The `D` button docks the panel to the
side of the window.

![The Tools panel]({{ site.baseurl }}/assets/images/v2/tools.png){: width="272" }

| Section | Purpose |
|---------|---------|
| **Count** | Count reads in view, optionally matching an expression; copy the table as TSV |
| **Filter** | Build and apply read filters |
| **Find read** | Highlight reads by name |
| **Introns** | Toggle the splice-junction track |
| **Markers & ROI** | Add, visit and clear markers and regions of interest |
| **Save** | Save a PNG, SVG or PDF image, the visible alignments (BAM/SAM), or the session |
| **Sort** | Sort reads by strand, haplotype or the base at a position |

## Tracks panel

**Main menu → Tracks** lists every loaded file. Move files up or down, set panel heights, show a
BAM header, switch on the intron track, or close a file.

![The track manager]({{ site.baseurl }}/assets/images/v2/track_manager.png)

## Popups

Click a read to open its popup: SAM fields, tags, and the sequence around the clicked base. The
buttons in the title bar go to the mate, add the mate as a new region, find the read's other
alignments, or copy the record as SAM. Popups can be moved and stay open while you browse.

![Read popup with the sequence around the clicked base]({{ site.baseurl }}/assets/images/v2/read_popup.png)

Click the coverage for per-base counts, or a track feature for its fields.

![Coverage popup]({{ site.baseurl }}/assets/images/v2/coverage_popup.png){: width="489" }

## Command bar

The command bar (11) runs along the bottom of the window. Press `/` or click it to type; a list of
every command appears and narrows as you type (ticks mark switches that are on). Hide the bar from
**Main menu → Command bar**; output appears in **Main menu → Output**.

![The command bar with its command list]({{ site.baseurl }}/assets/images/v2/command_bar.png){: width="560" }

`↑` / `↓` step through command history. See
[Commands]({{ site.baseurl }}/guide/commands.html) for the full list.

## Choosing a reference genome

When GW starts without a reference, a **Reference genome** chooser opens: pick a configured or
online genome, filter the list, or open a local FASTA.

![The reference genome chooser]({{ site.baseurl }}/assets/images/v2/genome_chooser.png){: width="540" }

Starting with `gw --wait` opens an empty window with no chooser, for when GW is driven by another
program.
