---
title: Tracks
layout: default
parent: User guide
nav_order: 4
description: Feature tracks in GW
---

# Tracks
{: .no_toc }

Tracks show annotations below the alignments: VCF, BCF, BED, GFF3, GTF, bigBed, bigWig and
GW_TRACK files.

![Alignments with a feature track and ideogram]({{ site.baseurl }}/assets/images/v2/tracks.png)

## Opening tracks

Drag-and-drop a file onto the window, or use **Main menu → Open file**. **Online file** can also
fetch annotation tracks from [UCSC]({{ site.baseurl }}/guide/remote.html#ucsc-tables).

For a VCF/BCF, GW asks whether to load it as an annotation track, as
[tiled images]({{ site.baseurl }}/guide/images.html), or both.

## Working with tracks

- **Click** a feature for a popup with its fields. VCF/BCF, GFF/GTF and bigBed records are shown
  as a table; a VCF record lists INFO fields and per-sample FORMAT values
- **Drag** the line above the tracks to resize the track area
- **Click** a track's name label to rename it
- Tick **Expand tracks** in the display options to draw overlapping features on separate rows

**Main menu → Tracks** lists every track: set its height, move it up or down, or close it.

![The track manager]({{ site.baseurl }}/assets/images/v2/track_manager.png)

Tracks to load at every startup are set in **Settings → Tracks**.

## Genotypes

A VCF/BCF track with samples draws a genotype row per sample beneath its features. Each bar is one
haplotype: a heterozygous variant has one bar, a homozygous variant a second bar stacked on top.
Click a bar to highlight its variant.

## From the command line

```shell
gw hg19 -r chr1 -b HG002.bwa.bam --track test.gff3.gz
```

The `load track`, `track-height` and `expand-tracks` commands do the same from the command bar.

## Ideograms

Many reference genomes come with a preset ideogram for matching genome-tags (e.g. `hg19`, `hg38`,
`t2t`). Click the chromosome name beside the ideogram to jump to another chromosome. To manage
ideograms manually, use the command bar:

```shell
load ideogram hg19
remove ideogram
```

Set a custom ideogram for a genome-tag in the `[tracks]` section of `.gw.ini`:

```ini
[tracks]
hg38_ideogram=path_to_ideogram.bed
```

Any BED file works as an ideogram; multiple ideograms stack. Colour records with `#gw` header
lines (ARGB):

```
#gw LTR5 255,255,0,0
chr1    10000   20000
```
