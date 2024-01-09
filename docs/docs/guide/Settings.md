---
title: Settings
layout: home
parent: User guide
nav_order: 7
---

# Settings and config

The settings menu can be accessed by clicking the cog button on the left-hand side of the screen, or
by hitting the `ESC` key. 

Settings can be changed for the current session by navigating the menu and altering the desired options.
If you want to save the changes permanently, click the save button located in the menu at the top 
left-hand corer.

![Alt text](/assets/images/settings1.png "GW")

The config file for GW is called .gw.ini and is usually saved in your home directory, or under `~/.config`
folder. Feel free to edit this. If you mess things up, just remove the ini file and GW will make a new
one in the home directory when you restart.

## Reference genomes
Reference genomes are defined by a unique genome-tag (e.g. `hg19` or `t2t`) and a path to a local or online genome in fasta
format. For best performance always use a local file if possible. Add a new reference genome by either editing the .gw.ini
file, or click the `+` button at the top of the menu in the `Genomes` section.

## Default tracks
Tracks can be loaded when a specific genome-tag is selected during start up. The list of tracks to load for a genome-tag
is defined as a comma-separated-list of paths. For example:

```shell
hg19=a.bed,b.gff3
```

This line will tell GW to load a bed file and a gff3 track file when the `hg19` genome-tag is used.