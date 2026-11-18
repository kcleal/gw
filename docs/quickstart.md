---
title: Quickstart
layout: default
nav_order: 2
description: Get up and running with GW version 2
---

# Quickstart
{: .no_toc }

## 1. Start GW

The quickest way is in your browser — nothing to install:

[Open GW]({{ site.baseurl }}/app/){: .btn .btn-primary .mr-2 }
[GW demo]({{ site.baseurl }}/app/demo.html){: .btn }

For large files, [install GW locally]({{ site.baseurl }}/install.html) and start it with `gw`.

## 2. Pick a reference genome

A **Reference genome** chooser opens in the window. Pick a genome from the list, filter by name, or
open a local FASTA. Local FASTA files are much faster than online genomes; add your own under
**Settings → Genomes**.

![The reference genome chooser]({{ site.baseurl }}/assets/images/v2/genome_chooser.png){: width="540" }

## 3. Open data

Drag-and-drop files onto the window, or use **Main menu → Open file**:

- **Local file** — a file browser for BAM/CRAM alignments, VCF/BCF variants and tracks
  (BED/GFF/GTF/bigBed/bigWig)
- **Online file** — online genomes, data catalogues and a
  [UCSC table search]({{ site.baseurl }}/guide/remote.html#ucsc-tables)

Alignment files stack vertically; tracks appear below them.

## 4. Look around

![GW window with numbered interface regions]({{ site.baseurl }}/assets/images/v2/interface_overview.png)

- **Move** — drag to pan, scroll to zoom, or type a locus in the region box at the top (3)
- **Inspect** — click a read, the coverage or a track feature for a popup with its details
- **Change the display** — the second button at the top-left (2) switches coverage, mismatches,
  soft-clips and more, and sorts, hides or links reads
- **Compare loci** — the `+` button (4) adds another region alongside
- **Tools** — **Main menu → Tools** counts and filters reads, adds markers, and saves images
- **Settings** — **Main menu → Settings** for themes, genomes, thresholds and key bindings

The numbers are explained in [The interface]({{ site.baseurl }}/guide/interface.html).

## 5. Commands (optional)

Every action also has a command. Press `/` to open the command bar and type; the list narrows as
you type.

```
chr1:1-20000      # go to a region
add chr2:1-50000  # add a region alongside
filter mapq >= 10 # filter reads
sort strand       # sort reads
snapshot          # save an image
man COMMAND       # manual for COMMAND
```

See [Commands]({{ site.baseurl }}/guide/commands.html) for the full list.

## 6. Command line (optional)

A local install can open files at launch, or draw images without a window:

```shell
gw hg38 -b your.bam -r chr1:1-20000
gw hg38 -b your.bam -r chr1:1-20000 -r chr2:50000-60000   # two regions side-by-side
gw hg38 -b b1.bam -b b2.bam -r chr1                       # stacked bams
gw hg38 -b your.bam -v var.vcf                            # variant tiles
gw hg38 -b your.bam -r chr1:1-20000 -n > out.png          # png to stdout
gw hg38 -b your.bam -r chr1:1-20000 -n --fmt pdf -f out.pdf
```

All options: `gw --help`.
