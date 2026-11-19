---
title: Images and tiles
layout: default
parent: User guide
nav_order: 5
description: Static images, variant tiles and screenshots
---

# Images and tiles
{: .no_toc }

## Variant image tiles

Open a variant file (VCF/BCF/BED/GW_LABEL) by drag-and-drop or **Main menu → Open file**, and
choose **Variant file as tiled images**. GW opens the first variant in the interactive alignment
view, with the Variants panel alongside. To see the variants as static image tiles, choose a grid
with the grid button in the Variants panel title bar, or turn on the tile view with the picture
button beside it.

![Variant image tiles with the Variants panel]({{ site.baseurl }}/assets/images/v2/tiles.png)

- **Scroll**, use `←`/`→`, or click the arrows at the edges of the plot to page through the tiles
- The grid button sets how many tiles are shown; a 1 x 1 grid returns to the alignment view
- **Click** a tile to cycle its label
- The triangle on a row of the Variant list opens that variant in the alignment view; click it
  again, or press `Tab`, to return to the tiles
- With several variant files open, switch between them from **Main menu → Variant files**

[Labelling]({{ site.baseurl }}/guide/labelling.html#reviewing-variants) describes reviewing
variants in both views.

## Saving an image

**Main menu → Tools → Save** writes the current view as a PNG, SVG or PDF. Leave the file name
blank for an automatic name.

## From the command line

```shell
gw hg38 -b your.bam -r chr1:1-20000 -n > out.png                 # png to stdout
gw hg38 -b your.bam -r chr1:1-20000 -n --fmt pdf -f out.pdf       # pdf
gw t2t -t 24 -b your.bam -n --outdir chrom_plots                  # every chromosome, parallel
gw hg38.fa -b your.bam -v variants.vcf --outdir all_images --no-show -t 16
gw hg19 -b HG002.bwa.bam -v variants1.vcf -v variants2.vcf        # tiles from two files
bcftools view -r chr1:1-1000000 your.bcf | gw hg38 -b your.bam -v -
```

Output options: `--dims`, `--theme` (`igv`, `dark`, `slate`, `tide`, `frost`), `--font-size`,
`--ylim`, `--cov`, `--ideogram`, `--fmt`, `-f`/`--outdir`.

The `snapshot` command (alias `s`) saves a PNG of the current view.
