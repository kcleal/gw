---
title: Alignments
layout: default
parent: User guide
nav_order: 3
description: Viewing alignment and variant data in GW
---

# Alignments and variants
{: .no_toc }

## Loading alignment data

Drag-and-drop a BAM/CRAM into the window, use the `load` command (tab-completion works), or the
`--bam/-b` option:

```shell
gw hg38 -b your.bam -r chr1:1-20000
```

The `hg38` argument is a genome-tag from the config file. For much faster performance, point the
tag at a local FASTA — see [Settings]({{ site.baseurl }}/guide/settings.html).

Multiple `-b` options stack BAMs vertically; multiple `-r` options split the window into regions.

If a genome-tag matches the BAM header SQ lines, the file can be opened directly:

```shell
gw your.bam
```

GW reads the BAM header and infers the reference genome. This also makes the macOS/Linux
`Open with` association work.

<div style="border: 2px dashed #5c5c5c; border-radius: 8px; padding: 3rem 1rem; text-align: center; color: #8a8a8a; margin: 1rem 0;">
  <strong>Screenshot placeholder</strong> — alignment view with coverage, mismatches, indels<br>
  <code>docs/assets/images/v2/alignments.png</code>
</div>

## Working with reads

- **Click** a read to select it and open the read popup (SAM fields, tags, `mate add`, `find`)
- Toggle display elements with the view dropdown or
  [toggle commands]({{ site.baseurl }}/guide/commands.html): insertions, mismatches, base
  modifications, edges, soft-clips
- `sort` reads by strand, haplotype or base at a position
- `filter` reads (e.g. `filter mapq >= 10`)

## Base modifications

Display base modifications (MM/ML tags) with the `--mods` option or the `mods` command.

## Splice junctions

`--introns` or the `introns` command draws a splice-junction track per BAM. Junctions are
clustered (`--splice-cluster-eps`) and filtered by supporting reads (`--min-junction-reads`).

## Structural variants

`link sv` draws linking lines between the split alignments of a structural variant; `link none`
clears them. `v` / `var` shows the variant under the cursor.

## Variant files

A variant file (VCF/BCF/BED) can be opened by drag-and-drop or with `-v`. Depending on
`vcf_as_tracks`, variants are drawn as a track or as
[image tiles]({{ site.baseurl }}/guide/images.html).
