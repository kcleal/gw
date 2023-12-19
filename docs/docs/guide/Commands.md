---
title: Commands
layout: home
parent: User guide
nav_order: 2
---

# Commands
{: .no_toc }

To open the command box press the forward slash or click on the button found on the menu
at the left-hand-side of the screen:

![Alt text](/assets/images/bubble.png "Bubble")

Type `help` to print a quick help summary to the terminal, or for more
information about each command type
`man [COMMAND]` into the box, where COMMAND is the name of the command e.g.
```shell
man ylim
```

If you type some text that is not recognised as a command, then the text will be output in the terminal.
This can be useful for note-taking.

Pressing the UP ARROW key will cycle through your command history.

The list of available commands are given below.

<br>

## Table of contents
{: .no_toc .text-delta }

1. TOC
{:toc}

---


- ## [locus / feature]

Navigate to a genomic locus, or feature of interest.
You can use chromosome names or chromosome coordinates to change the current view point to a new location.

```shell
chr1:1-20000
chr1
chr1:10000
```

Additionally, if you have tracks loaded, such as a bed file of genes, you can type the name of a gene
or feature into the command box to navigate to that feature. Note, this feature only really works with non-indexed
files, because when using indexed files, GW will only load the data that is on-screen, rather than the whole file.

```shell
hTERT
```

- ## add

This will add a new region view to the right-hand-side of your screen.
<br>

```shell
add chr1:1-20000
```


- ## count

Counts the visible reads in each view. 
A summary output will be displayed for each view on the screen.
Optionally a filter expression may be added to the command. See the section for 'filter' for mote details.

```shell
count
...
File	HG002.bwa.bam
Region	chr1:1-20000
Total	4220
Paired	4220
Proper-pair	3791
Read-unmapped	0
Mate-unmapped	6
Read-reverse	2024
Mate-reverse	2107
First-in-pair	2118
Second-in-pair	2102
Not-primary	0
Fails-qc	0
Duplicate	60
Supplementary	76
Deletion-pattern	6
Duplication-pattern	3
Translocation-pattern	356
F-inversion-pattern	26
R-inversion-pattern	40
```

Count after applying a filter expression:

```shell
count flag & 2
count flag & proper-pair
```

- ## cov

Toggles the coverage track, turning it off/on.
<br>

- ## edges

Toggles the edges of tracks, turning them off/on. Edge-highlights are used to indicate split-reads, or 
reads that have an unmapped mate-pair.
<br>

- ## expand-tracks

Toggle expand-tracks. Features in the tracks panel are expanded so overlapping features can be seen.
<br>

- ## filter

Filters visible reads, and removes them from view if a filter is 'failed'.

Reads can be filtered using an expression '{property} {operation} {value}' (the white-spaces are also needed).
For example, here are some useful expressions:

    filter mapq >= 20
    filter flag & 2048
    filter seq contains TTAGGG

Here are a list of '{property}' values you can use:

    maps, flag, ~flag, name, tlen, abs-tlen, rnext, pos, ref-end, pnext, seq, seq-len,
    RG, BC, BX, RX, LB, MD, MI, PU, SA, MC, NM, CM, FI, HO, MQ, SM, TC, UQ, AS

These can be combined with '{operator}' values:

    &, ==, !=, >, <, >=, <=, eq, ne, gt, lt, ge, le, contains, omit

Bitwise flags can also be applied with named values:

    paired, proper-pair, unmapped, munmap, reverse, mreverse, read1, read2, secondary,
    dup, supplementary

Expressions can be chained together providing all expressions are 'AND' or 'OR' blocks:

    filter mapq >= 20 and mapq < 30
    filter mapq >= 20 or flag & supplementary

Finally, you can apply filters to specific panels using array indexing notation:

    filter mapq > 0 [:, 0]   # All rows, column 0 (all bams, first region only)
    filter mapq > 0 [0, :]   # Row 0, all columns (the first bam only, all regions)
    filter mapq > 0 [1, -1]  # Row 1, last column

Here are a list of some example filtering commands:

    filter mapq >= 20             # only reads with mapping quality >= 20 will be shown
    filter flag & 2048            # only supplementary alignments are shown
    filter flag & supplementary   # same as above
    filter ~flag & supplementary  # supplementary reads will be removed
    filter seq contains TTAGGG    # Only reads with TTAGGG kmer will be shown
    filter seq omit AAAAAA        # Reads with this kmer will be removed
    filter mapq > 30 and ~flag & duplicate  #  also removes duplicate reads
    filter mapq > 10 or seq-len > 100; ~flag & duplicate  # multiple commands


- ## find, f

Find a read with target name.

All alignments with the same name will be highlighted with a bright edge border.

    find D00360:18:H8VC6ADXX:1:1107:5538:24033


- ## goto

Navigate to a locus or track feature.

This moves the current region to a new view point, or adds a new view point if one is not already open.
You can also specify a feature name from one of the loaded tracks.
Searching for track features works mainly for non-indexed files that are loaded into memory during startup.

    goto chr1
    goto hTERT


- ## grid

Set the grid size for viewing image tiles.

    grid 8x8   # this will display 64 image tiles


- ## indel-length

Set the minimum indel-length.

Indels (gaps in alignments) will be labelled with text if they have length ≥ 'indel-length

    indel-length 30


- ## insertions, ins

Toggle insertions. Insertions smaller than 'indel-length' are turned on or off.
<br>

- ## line

Toggles a vertical line drawn over cursor position.
<br>

- ## link, l

Link alignments.

This will change how alignments are linked, options are 'none', 'sv', 'all'. Linking by 'sv' will draw
links between alignments that have either a discordant flag, or have a supplementary mapping.
<br>

- ## low-mem

Toggle low-mem mode.

This will discard all base-quality information and sam tags from newly loaded alignments. Use this
option if you wish to display large regions and you have memory concerns. Memory consumption can be reduced
by up to half.
<br>

- ## log2-cov

Toggle log2-coverage. The coverage track will be scaled by log2.
<br>

- ## mate

Goto mate alignment.

Either moves the current view to the mate locus, or adds a new view of the mate locus.

    mate
    mate add


- ## mismatches, mm

Toggle mismatches. Mismatches with the reference genome are turned on or off.
<br>

- ## online

Show links to online browsers for the current region.

This function is mainly implemented for human reference genomes including hg19/GRCh37, hg38/GRCh38 
and hs1/t2t. If you are not using a genome tag, you can add one as an additional argument:

    online
    online GRCh37

For human references, links to databases with the same region as the current view are provided. Supported
databases include:

| UCSC Genome Browser [link](https://genome.ucsc.edu/cgi-bin/hgGateway)
| DECIPHER Genome Browser [link](https://www.deciphergenomics.org/browser)	
| Gnomad Browser [link](https://gnomad.broadinstitute.org)
| Ensemble Genome Browser [link](https://www.ensembl.org/index.html)
| NCBI Genome Data Viewer [link](https://www.ncbi.nlm.nih.gov/genome/gdv/)

If you would like to see more databases added, please get in touch.
<br>

- ## quit, q

Simply quits GW
<br>

- ## refresh, r

Refresh the screen and re-draws everything. All filters will also be removed.
<br>

- ## remove, rm

Remove a region, bam or track by index.

To remove a bam or track add a 'bam' or 'track' prefix.

    rm 0        # this will remove REGION at index 0
    rm bam1     # this will remove BAM FILE at index 1
    rm track2   # removes track at index 2


- ## sam

Print the entire seclected read in sam format to the terminal.

First select a read using the mouse then type `sam`
<br>

- ## settings

Opens the settings panel.
<br>

- ## snapshot, s

Save an image of the screen.

If no name is provided, the image name will be 'chrom_start_end.png' and will be saved
in the current working directory,
or if you are in tiled-mode, the image name will be 'index_start_end.png'

    snapshot
    snapshot my_image.png

If you have a vcf/bcf open in 'single' mode (not 'tiled') it is also possible to parse info
from the vcf record. Use curley braces to select what fields to use in the filename:

    snapshot {pos}_{qual}.png        # parse the position and qual fields
    snapshot {info.SU}.png           # parse SU from info field
    s {format[samp1].SU}.png         # samp1 sample, SU column from format field

Valid fields are chrom, pos, id, ref, alt, qual, filter, info, format. Just to note,
you can press the repeat key (R by default) to repeat the last command, which can save typing this
command over and over.
<br>

- ## soft-clips, sc

Soft-clipped bases or hard-clips are turned on or off.
<br>

- ## tags

This will print all the tags of the selected read (select a read with the mouse first).
<br>

- ## theme

Switch the theme.

Currently, 'igv', 'dark' or 'slate' themes are supported.
    
    theme slate


- ## tlen-y

Toggle `tlen-y` option.

This will scale the read position by template length, so will apply to paired-end reads only. Reads
with a shorter template-length will be drawn towards the top of the screen, and reads with a longer
template-length will be drawn below.

You can set a maximum y-limit on the template-length by adding a number argument:

    tlen-y
    tlen-y 50000


- ## var, v

Print variant information.

This function requires a VCF file to be opened as a variant file, which can be added 
using the `-v` option on command line, or by dragging-and-dropping a VCF into the window.

Using `var` will print the full record of the selected variant (use mouse to select).
If you are viewing a VCF then columns can be parsed e.g:
    
    var pos              # position
    var info.SU          # SU column from info
    v chrom pos info.SU  # list of variables to print
    v format.SU          # SU column from format
    v format[samp1].SU   # using sample name to select SU

Valid fields are chrom, pos, id, ref, alt, qual, filter, info, format. Just to note,
you can press ENTER to repeat the last command, which can save typing this
command over and over.
<br>

- ## ylim

Set the y limit.
The y limit is the maximum height of stacked reads shown on the drawing.
 
```shell
ylim 100
```   

