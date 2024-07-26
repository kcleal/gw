---
title: Genomes and Ideograms
layout: home
parent: User guide
nav_order: 1
---

# Genomes

GW can load online reference genomes, although it is always best to 
use a local file.

To add or update a reference genome path and tag:

1. Click on the Settings menu (click on cog)
2. Goto Genomes menu
3. Click on the "+" button at the top
4. Type the name of the genome-tag you want e.g. `hg19`. If it exists already it will be updated. Hit ENTER
5. Type the path to the local file. Hit ENTER.
6. Optionally click the save button at the top. This will ensure that the genome-tag is saved to your config file.

Once you have configured the path, the genome-tag will then point to your local file.


# Ideograms


Many of the reference genomes come with a preset ideogram and will be loaded automatically
when a matching genome tag is used e.g. 'hg19' or 't2t' etc.

You can also load ideograms easily by typing the command:

    load ideogram hg19/t2t/hg38  # Choose the appropriate genome-tag

To remove the ideogram use the remove command:

    remove ideogram

To configure GW to use a custom ideogram for a genome-tag add this to the 'tracks'
section of settings (or your .gw.ini file):

    [tracks]
    hg38_ideogram=path_to_ideogram.bed

This will set the ideogram path for the 'hg38' genome-tag. Once this has been saved
in the settings menu, the ideogram will be loaded whenever the hg38 genome-tag is used.
For example, using the load command:

    load genome hg38

Or when using 'hg38' tag from the command line.

The CLI interface also has the `--ideogram` option.

---

Any bed file can be used as an ideogram, and adding more than one ideogram will
stack them on top of another. Idograms can be customised with different colours by
adding in ARGB values into the header of your bed file. For example:

    #gw LTR5 255,255,0,0
    chr1    10000   20000

The `#gw` header line will configure records with LTR5 in the name column to be a
bright red colour on the ideogram.




