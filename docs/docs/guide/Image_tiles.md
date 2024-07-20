---
title: Image tiles
layout: home
parent: User guide
nav_order: 5
---

# Variant data and image tiles

One or more variant data files can be loaded into GW which will result in variants being draw as image tiles -
one static image will be drawn for each variant in the file. Images can be scrolled using mouse or arrow
keys.

The following file formats are supported for image-tiling:

| Data option | File formats supported
|---	|---
| `--var`   | VCF, BCF, BED, GW_LABEL

Load data can be achieved by either dragging-and-dropping files into the main window, using the load
command or by using the `--var/-v` command-line option. Dragging and dropping vcf/bcf files can result
in files being added as image tiles or a track, this option is controlled by the `vcf_as_tracks` option
in the Settings->Interaction menu.



![Alt text](/assets/images/tiles1.png "GW")

Image tiles can be selected by right-clicking, which will switch to the interactive alignment-view mode. To get back
to the tiled image panel just right click again.

To change the label, and cycle through your label list, left-click an image tile.

Also it is possible to load multiple variant data files at the same time. The blue rectangles at the top-left
of the screen indicates the current variant data file being displayed. Click on a different rectangle to
switch to another variant data file, or use the keyboard short-cut by pressing COMMAND+RIGHT-ARROW.

```shell
gw hg19 -b HG002.bwa.bam -v variants1.vcf -v variants2.vcf 
```
![Alt text](/assets/images/tiles3.png "GW")

VCF files can also be open in a stream e.g. using bcftools to select and filter variant can be convenient:

```shell
bcftools view -r chr1:1-1000000 your.bcf | gw hg38 -b your.bam -v -
```


## Saving images of variant data

You can also generate an image of every variant in your VCF file, using the `--outdir` and `--no-show` options.
Also, you might want to increase the number of threads `-t` to speed things up a bit.
Be warned this will probably generate a huge number of files:

```shell
gw hg38.fa -b your.bam -v variants.vcf --outdir all_images --no-show -t 16
```
