---
title: Images
layout: home
parent: User guide
nav_order: 8
---

# Images

GW can generate static images in .png/.pdf format of target genomic regions. These can be saved to
a file but only .png images can be re-opened for exploring at a later date. Images can be generated
using the `snapshot` command, or using options on the command line.

The command line options are detailed below. 


## Saving images
Images are directed to stdout, to a file using the `-f` option, or to a folder using 
`--outdir`. The `--no-show`/`-n` option is also needed to stop GW displaying a window:

```shell
gw hg38 -b your.bam -r chr1:1-20000 -n > out.png             # png to stdout
gw hg38 -b your.bam -r chr1:1-20000 -n --fmt pdf -f out.pdf  # pdf saved to file
```

All variants in a VCF file can also be generated:

```shell
gw hg38.fa -b your.bam -v variants.vcf --outdir all_images -n -t 16
```

Or, all chromosome in a BAM file can be drawn:
```shell
gw t2t -t 12 -b your.bam -n --outdir chrom_plots
```

GW can also encode the genomic location of a region in the file name, 
allowing you to open static images alongside bam files at a later date.
To use this function, let GW name the png file for you by supplying only the output directory with `--outdir`:

```shell
gw hg38.fa -b your.bam -r chr1:1-20000 --outdir . -n
```

Here an image file will be saved in the current directory with the filename `GW~chr1~1~20000~.png`.

## Opening images
The image file mentioned above can be opened alongside a BAM file, allowing you to explore the image dynamically:

```shell
gw hg19 -i GW~chr1~1~20000~.png -b your.bam
```


Images saved in .png format can be opened in a similar way to variant data, using the -i or --images option.
Files are input using a glob pattern. 
For example all .png images in a folder called 'images' can be opened with:

```shell
gw -i "images/*.png"
```

If you previously used GW to generate images from a vcf file (see example in Variant data section),
any parsed-labels will be saved in the --outdir directory. For example if --outdir images was used when generating images, you can now view these images and labels using:

```shell
gw -i "images/*.png" --in-labels images/gw.parsed_labels.tsv
```

To open one or more bam files alongside your images you will need to supply a reference genome. Right-clicking using the mouse will then switch between images and bam files:

```shell
gw hg38.fa -b your.bam -i "images/*.png"
```
