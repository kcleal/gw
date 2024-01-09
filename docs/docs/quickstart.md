---
title: Quickstart
layout: default
nav_order: 3
---

# Quick Start

## Desktop Application:
When you start GW for the first time you will see a prompt with a list of online reference genomes - select an
appropriate genome tag from the list by typing in a number. You can update this list from the main menu 
when GW has loaded.

Once a reference genome has been loaded, drag-and-drop data files such as bam or bed's into the graphical window. The command box 
can be accessed by pressing the forward-slash key `/`, or by clicking on the bubble towards the left-hand-side of the
screen. Navigate to a region by typing, for example `/chr2`.

For further help on command usage, see the help menu by typing `/help`, or for detailed manuals type `/man COMMAND`, where
COMMAND is the name of the command in question. More information is available in the [Commands section
of the User Guide](https://kcleal.github.io/gw/docs/guide/Commands.html).

GW also supports opening a bam file by double-clicking. In order for this to work, a matching reference genome is
needed in the GW configuration file, see the [Settings section](https://kcleal.github.io/gw/docs/guide/Settings.html) for more details.


## Command line:

Start gw using the hg38 genome tag, next, drag and drop bams into the window:
```shell
gw hg38
```

Open a target bam file (a matching reference genome will be needed in the config file):
```shell
gw a.bam
````

View start of chr1:
```shell
gw hg38 -b your.bam -r chr1:
```

Two regions, side-by-side:
```shell
gw hg38 -b your.bam -r chr1:1-20000 -r chr2:50000-60000
```

Multiple bams:
```shell
gw hg38 -b '*.bam' -r chr1
gw hg38 -b b1.bam -b b2.bam -r chr1
```

Add a track BED/VCF/BCF/GFF3/GTF/GW_LABEL/BIGBED/BIGWIG:
```shell
gw hg38 -b your.bam -r chr1 --track a.bed
```

png image to stdout:
```shell
gw hg38 -b your.bam -r chr1:1-20000 -n > out.png
```

Save pdf:
```shell
gw hg38 -b your.bam -r chr1:1-20000 -n --fmt pdf -f out.pdf
```

Plot every chromosome in parallel:
```shell
gw t2t -t 24 -b your.bam -n --outdir chrom_plots
```

View VCF/BCF/BED as tiled images:
```shell
gw hg38 -b your.bam -v var.vcf
```

View VCF/BCF from stdin:
```shell
gw hg38 -b your.bam -v -
```

View some png images:
```shell
gw -i "images/*.png"
```

Save some annotations:
```shell
gw hg38 -b your.bam -v var.vcf --labels Yes,No --out-labels labels.tsv
```

## Useful commands
GW commands - access command box with : or /. Example commands:

```
help              # help menu
config            # open config file for editing
chr1:1-20000      # Navigate to region
add chr2:1-50000  # Append new region
rm 1              # Region at column index 1 removed
rm bam1           # Bam file at row index 1 removed
mate              # Move view to mate of read
mate add          # mate added in new view
line              # Toggle vertical line
ylim 100          # View depth increased to 100
find QNAME        # Highlight all reads with qname==QNAME
filter mapq >= 10 # Filer reads for mapq >= 10
count             # Counts of all reads for each view point
snapshot          # Save screenshot to .png
man COMMAND       # manual for command
```

