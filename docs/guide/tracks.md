---
title: Tracks
layout: default
parent: User guide
nav_order: 4
description: Feature tracks in GW
---

# Tracks
{: .no_toc }

Load tracks by drag-and-drop, the `--track` option, or the `load track` command. Tracks appear
below the alignments.

| Data option | File formats supported |
|-------------|------------------------|
| `--track` | VCF, BCF, BED, GFF3, GTF, BIGBED, BIGWIG, GW_TRACK |

```shell
gw hg19 -r chr1 -b HG002.bwa.bam --track test.gff3.gz
```

Resize the track pane by dragging its boundary, with `track-height` / `tab-track-height`, or via
Settings → Tracks.

By default, dragging a VCF/BCF file loads it as
[image tiles]({{ site.baseurl }}/guide/images.html) (like `--var`). To add variants as a track
instead, set `vcf_as_tracks` in Settings → Interaction.

`expand-tracks` (view menu or command) displays overlapping features in separate rows.

<div style="border: 2px dashed #5c5c5c; border-radius: 8px; padding: 3rem 1rem; text-align: center; color: #8a8a8a; margin: 1rem 0;">
  <strong>Screenshot placeholder</strong> — alignment view with feature tracks<br>
  <code>docs/assets/images/v2/tracks.png</code>
</div>

## Ideograms

Many reference genomes come with a preset ideogram for matching genome-tags (e.g. `hg19`, `hg38`,
`t2t`). Manage them manually:

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
