---
title: Alignment data
layout: home
parent: User guide
nav_order: 1
---

# Alignment data

The easiest way to load an alignment file (bam or cram format) is to start
GW and drag-and-drop a file into the window. This will open the file and navigate
to the first chromosome in the bam file header, showing the first 20 kb.

Using the CLI the same can be achieved using:

```shell
gw hg38 -b your.bam -r chr1:1-20000
```

![Alt text](/assets/images/ui2.png "GW")

The hg38 argument is a genome-tag listed in the config file and will load a remote reference genome.

For much faster performance, either replace the argument with the path to a local file,
or change the config file to point to a local file rather than a remote reference genome.
This command will open a GW window that can be used interactively with the mouse and keyboard.
Note multiple -b and -r options can be used.

## Genome tags
If you have a genome-tag configured in your config file it is possible to provide the 
alignment file as a single argument:

```shell
gw your.bam
```

GW will read the bam file header and infer the correct reference genome to load by 
comparing with the references listed in your config file. If you are using the desktop
application this feature allows you to use the `Open with` function (found by right-clicking on a
bam file). 