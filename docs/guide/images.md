---
title: Images and tiles
layout: default
parent: User guide
nav_order: 5
description: Static images, variant tiles and screenshots
---

# Images and tiles
{: .no_toc }

<details open markdown="block">
  <summary>Table of contents</summary>
  {: .text-delta }
1. TOC
{:toc}
</details>

## Variant image tiles

Load a variant file (VCF/BCF/BED/GW_LABEL) and each variant is drawn as a static image tile.
Scroll with the mouse or arrow keys.

Load by drag-and-drop, the `load` command, or `--var/-v`. Dragged VCF/BCF files become tiles or a
track depending on `vcf_as_tracks` (Settings → Interaction).

<div style="border: 2px dashed #5c5c5c; border-radius: 8px; padding: 3rem 1rem; text-align: center; color: #8a8a8a; margin: 1rem 0;">
  <strong>Screenshot placeholder</strong> — tiled variant images<br>
  <code>docs/assets/images/v2/tiles.png</code>
</div>

- **Right-click** a tile to switch to the interactive alignment view; right-click again to return
- **Left-click** a tile to cycle its label
- Multiple variant files: the rectangles at top-left show the current file — click another to
  switch (or `COMMAND`/`CTRL` + `→`)

```shell
gw hg19 -b HG002.bwa.bam -v variants1.vcf -v variants2.vcf
```

VCF files can be streamed through bcftools:

```shell
bcftools view -r chr1:1-1000000 your.bcf | gw hg38 -b your.bam -v -
```

## Static images from the command line

```shell
gw hg38 -b your.bam -r chr1:1-20000 -n > out.png                 # png to stdout
gw hg38 -b your.bam -r chr1:1-20000 -n --fmt pdf -f out.pdf       # pdf
gw t2t -t 24 -b your.bam -n --outdir chrom_plots                  # every chromosome, parallel
gw hg38.fa -b your.bam -v variants.vcf --outdir all_images --no-show -t 16
```

Output options: `--dims`, `--theme` (`igv`, `dark`, `slate`), `--font-size`, `--ylim`, `--cov`,
`--ideogram`, `--fmt`, `-f`/`--outdir`.

## Screenshots

`snapshot` (alias `s`) saves a PNG of the current view.
