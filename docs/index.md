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

[Get started]({{ site.baseurl }}/quickstart.html){: .btn .btn-primary .fs-5 .mb-4 .mb-md-0 .mr-2 }
[Install from source]({{ site.baseurl }}/install.html){: .btn .fs-5 .mb-4 .mb-md-0 .mr-2 }
[Version 1 docs]({{ site.baseurl }}/v1/){: .btn .fs-5 .mb-4 .mb-md-0 }

<div style="border: 2px dashed #5c5c5c; border-radius: 8px; padding: 3rem 1rem; text-align: center; color: #8a8a8a; margin: 1.5rem 0;">
  <strong>Screenshot placeholder</strong> — GW v2 main window<br>
  <code>docs/assets/images/v2/main_window.png</code>
</div>

---

## Run GW in your browser

No install needed — GW v2 can run as WebAssembly. Your data stays in the browser.

[Open GW]({{ site.baseurl }}/app/){: .btn .btn-primary .fs-5 .mb-4 .mb-md-0 .mr-2 }
[Open GW with demo data]({{ site.baseurl }}/app/demo.html){: .btn .fs-5 .mb-4 .mb-md-0 .mr-2 }
[Open GW with HG008-T (GIAB)]({{ site.baseurl }}/app/hg008.html){: .btn .fs-5 .mb-4 .mb-md-0 }

{: .note }
Requires a WebGL2-capable browser. All entries stream the human GRCh38
reference (from IGV/AWS) at startup — no reference download needed. The demo
entry loads a small bundled example BAM. The HG008-T entry streams the GIAB
HG008-T tumor BAM (PacBio HiFi) over HTTP from NCBI — the first load fetches
its ~22 MB index, so it is slower to open.

## What is GW?

GW displays BAM/CRAM alignments, VCF/BCF variants and feature tracks (BED/GFF/GTF/bigBed/bigWig).
It generates static images and labels variants.

## What's new in version 2

- **ImGui interface** — all overlay UI (menus, settings, popups, command box) is now built with
  Dear ImGui. See [The interface]({{ site.baseurl }}/guide/interface.html).
- **More commands** — `translate`, `marker`, `link`, `cov`, `theme`, `grid`, `online`, `load`,
  `goto` and more. See [Commands]({{ site.baseurl }}/guide/commands.html).
- **Stable API and protocol** — embed GW via the `libgw` C++ API, or drive it remotely over a
  versioned JSON protocol. See [API & integrations]({{ site.baseurl }}/api/).

{: .note }
**GW v2 is in beta.** It must be [built from source]({{ site.baseurl }}/install.html) — it is not
yet on conda or Homebrew. GW v1 packages remain available from the
[v1 install docs]({{ site.baseurl }}/v1/install/Install.html).
