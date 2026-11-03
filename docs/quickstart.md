---
title: Quickstart
layout: default
nav_order: 2
description: Get up and running with GW version 2
---

# Quickstart
{: .no_toc }

<details open markdown="block">
  <summary>Table of contents</summary>
  {: .text-delta }
1. TOC
{:toc}
</details>

## 1. Build GW

GW v2 is in beta and must be built from source — see
[Installation]({{ site.baseurl }}/install.html) for platform details:

```shell
conda create -y -n gw_env -c conda-forge glfw htslib ncurses
conda activate gw_env
git clone https://github.com/kcleal/gw.git
cd gw && make prep
CONDA_PREFIX=$(conda info --base) LDLIBS+="-lcrypto -lssl" make -j4
```

## 2. Launch GW and pick a genome

```shell
gw
```

A terminal picker lists the online reference genomes — type a number or a tag such as `hg38`.
To skip the picker, pass the tag on the command line (`gw hg38`). Genomes can also be added later
in Settings → Genomes — local FASTA files are much faster than online genomes.

## 3. Open data

Drag-and-drop a BAM/CRAM into the window. GW opens it and navigates to the first chromosome.
Add more files the same way: further BAMs stack vertically, tracks (BED/VCF/GFF/GTF/bigBed/bigWig)
appear below the alignments.

To browse and download remote data without leaving GW, use the **Online files** dialog from the
top menu — it includes online genomes, data catalogues and a
[UCSC table search]({{ site.baseurl }}/guide/remote.html#ucsc-tables).

## 4. The interface

- **Plot window** — click a read to select it, right-click for its popup, drag to zoom, scroll to
  zoom at the cursor
- **Top menu** — hamburger button (top-left): open files, settings, save images/sessions, online
  files. The view dropdown controls layout options
- **Command box** — press `:` or `/`, then type a command (below)

<div style="border: 2px dashed #5c5c5c; border-radius: 8px; padding: 3rem 1rem; text-align: center; color: #8a8a8a; margin: 1.5rem 0;">
  <strong>Screenshot placeholder</strong> — annotated GW v2 window<br>
  <code>docs/assets/images/v2/quickstart_annotated.png</code>
</div>

See [The interface]({{ site.baseurl }}/guide/interface.html) for a full tour.

## 5. Useful commands

```
help              # help menu
chr1:1-20000      # navigate to region
add chr2:1-50000  # append new region
rm 1              # remove region at column index 1
mate              # move view to mate of read
find QNAME        # highlight all reads with qname == QNAME
filter mapq >= 10 # filter reads
count             # read counts for each view
snapshot          # save screenshot
translate         # translate reads
marker chr1:15000 # add a marker
man COMMAND       # manual for COMMAND
```

See [Commands]({{ site.baseurl }}/guide/commands.html) for the full list.

## Command line (power users)

Everything above can be scripted. Open files directly, split regions, generate images without a
window:

```shell
gw hg38 -b your.bam -r chr1:1-20000
gw hg38 -b your.bam -r chr1:1-20000 -r chr2:50000-60000   # two regions side-by-side
gw hg38 -b b1.bam -b b2.bam -r chr1                       # stacked bams
gw hg38 -b your.bam -v var.vcf                            # variant tiles
gw hg38 -b your.bam -r chr1 -c 'sort strand'              # apply a command at launch
gw hg38 -b your.bam -r chr1:1-20000 -n > out.png          # png to stdout
gw hg38 -b your.bam -r chr1:1-20000 -n --fmt pdf -f out.pdf
gw t2t -t 24 -b your.bam -n --outdir chrom_plots          # every chromosome, in parallel
bcftools view -r chr1:1-1000000 your.bcf | gw hg38 -b your.bam -v -
```

All command-line options: `gw --help`.
