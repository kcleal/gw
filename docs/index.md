---
layout: default
title: Home
nav_order: 1
description: "GW: A fast genome browser for genomic sequencing data — version 2"
---

# GW version 2
{: .fs-9 }

A fast browser for genomic sequencing data — now with an ImGui interface, more commands, and a
stable API.
{: .fs-6 .fw-300 }

[Open GW]({{ site.baseurl }}/app/){: .btn .btn-primary .fs-5 .mb-4 .mb-md-0 .mr-2 }
[GW demo]({{ site.baseurl }}/app/demo.html){: .btn .fs-5 .mb-4 .mb-md-0 .mr-2 }
[Install local GW]({{ site.baseurl }}/install.html){: .btn .fs-5 .mb-4 .mb-md-0 }

![GW main window with the Variants panel]({{ site.baseurl }}/assets/images/v2/main_window.png)

---

## Run GW in your browser

No install needed — GW v2 can run as WebAssembly, and your data stays in the browser.
**Open GW** starts an empty browser; **GW demo** loads a small bundled example BAM. Open your own
data from the main menu or by drag-and-drop — remote references stream over HTTP range reads.
A WebGL2-capable browser is required.

For large files, [install GW locally]({{ site.baseurl }}/install.html). New to GW? Start with the
[Quickstart]({{ site.baseurl }}/quickstart.html).

## What is GW?

GW displays BAM/CRAM alignments, VCF/BCF variants and feature tracks (BED/GFF/GTF/bigBed/bigWig).
It generates static images and labels variants.

## What's new in version 2

- **Point-and-click interface** — menus, a track manager, tools panel, popups and settings
  dialogs cover everyday use; no commands needed. See
  [The interface]({{ site.baseurl }}/guide/interface.html).
- **More commands** — `translate`, `marker`, `link`, `cov`, `theme`, `grid`, `online`, `load`,
  `goto` and more. See [Commands]({{ site.baseurl }}/guide/commands.html).
- **Stable API and protocol** — embed GW via the `libgw` C++ API, or drive it remotely over a
  versioned JSON protocol. See [API & integrations]({{ site.baseurl }}/api/).

{: .note }
**GW v2 is in beta.** It must be [built from source]({{ site.baseurl }}/install.html) — it is not
yet on conda or Homebrew. GW v1 packages remain available from the
[v1 install docs]({{ site.baseurl }}/v1/install/Install.html).
