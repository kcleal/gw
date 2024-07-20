---
title: Genomes and Ideograms
layout: home
parent: User guide
nav_order: 1
---

# Genomes




# Ideograms


Many of the reference genomes come with a preset ideogram and will be loaded automatically
when a matching genome tag is used e.g. 'hg19' or 't2t' etc.

To configure GW to use an ideogram for a genome tag add this to the 'tracks'
section of settings (or your .gw.ini file)::

    [tracks]
    hg38_ideogram=path_to_ideogram.bed

This will set the ideogram path for the 'hg38' genome tag. Once this has been saved
in the settings menu, the ideogram will be loaded whenever the hg38 genome tag is used.
For example, using the load command::

    load genome hg38

Or when using 'hg38' tag from the command line.

The CLI interface also has the `--ideogram` option


