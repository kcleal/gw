---
title: Alignments
layout: default
parent: User guide
nav_order: 3
description: Viewing alignment and variant data in GW
---

# Alignments and variants
{: .no_toc }

## Opening alignments

Drag-and-drop a BAM/CRAM onto the window, or use **Main menu → Open file → Local file**. Each
file gets its own panel, stacked vertically. Reorder, resize or close panels in
**Main menu → Tracks**.

![Alignment view of a deletion with linked reads]({{ site.baseurl }}/assets/images/v2/alignments.png)

GW needs a reference genome first — choose one at startup or under **Settings → Genomes**. A local
FASTA is much faster than an online genome. If a configured genome matches the BAM header, a BAM
can be opened on its own and GW picks the reference.

## Working with reads

- **Click** a read for its popup: SAM fields, tags and sequence. The title-bar buttons go to the
  mate, add the mate as a new region, find the read's other alignments, or copy it as SAM
- **Click** the coverage for per-base counts
- **Scroll** through deep pileups with the scrollbar at the right of the panel

## Changing what is drawn

The display options button (second from the top-left) switches plot elements on and off:
insertions, mismatches, edges, soft-clips, coverage, log2 coverage and more. From the same menu:

- **Sort** — by strand or haplotype. To sort by the base at a position, use **Tools → Sort**, or
  press `S` with the pointer over the position
- **Hide** — duplicates, secondary, supplementary or MAPQ 0 reads
- **Link** — join the reads of a structural variant, or all pairs

For other filters open **Main menu → Tools → Filter** and enter an expression such as
`mapq >= 10` — see the [filter reference]({{ site.baseurl }}/guide/commands.html#filter).
**Tools → Count** reports read counts for the view.

## Base modifications

Tick **Mods** in the display options to draw base modifications (MM/ML tags).

## Splice junctions

**Tools → Introns**, or the **Introns** box beside a file in the Tracks panel, draws a
splice-junction track for that BAM.

## Structural variants

**Link → Structural variants** draws lines between the split alignments of a structural variant.
Reads are coloured by pattern: deletion, duplication, inversion or translocation.

## Variant files

Open a VCF/BCF the same way as any other file. GW asks whether to load it as an annotation
[track]({{ site.baseurl }}/guide/tracks.html), as
[tiled images]({{ site.baseurl }}/guide/images.html) for reviewing variants, or both.

## From the command line

```shell
gw hg38 -b your.bam -r chr1:1-20000     # genome tag, bam, region
gw hg38 -b a.bam -b b.bam -r chr1       # stacked bams
gw your.bam                             # reference inferred from the header
gw hg38 -b your.bam --mods --introns
```

The equivalent commands are `load`, `sort`, `filter`, `link`, `mods` and `introns` — see
[Commands]({{ site.baseurl }}/guide/commands.html).
