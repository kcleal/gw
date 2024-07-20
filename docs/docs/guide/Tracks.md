---
title: Tracks
layout: home
parent: User guide
nav_order: 7
---

# Tracks

One or more data tracks can be loaded into GW by dragging-and-dropping files into the main window,
or by using the `--track` command-line option, or using the `load` GW command.

Data tracks appear below alignments towards the bottom of the screen.

The following file formats are supported:

| Data option | File formats supported
|---	|---
| `--track` | VCF, BCF, BED, GFF3, GTF, BIGBED, BIGWIG, GW_TRACK


By default, dragging a VCF/BCF file will load the file in the same way as using the `--var` option,
resulting in images being drawn as tiles on a grid. To change this behaviour, goto
Settings->Interaction and change the `vcf_as_tracks` option to true.


## Loading track data

```shell
gw hg19 -r chr1 -b HG002.bwa.bam --track test.gff3.gz
```

![Alt text](/assets/images/track1.png "GW")

Note the track pane can be dynamically resized using the mouse by clicking on the boundary line and
dragging up or down.

Additionally selecting `expand-tracks` from the menu will display all overlapping features in a separate
row.