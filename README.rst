GW
==

.. image:: include/banner.png
    :align: center


GW is a fast browser for genomic sequencing data (.bam/.cram format), used directly from the terminal. GW also
allows you to view and annotate variants from vcf/bcf files. Check out the examples below!


Installing GW
--------------
GW is built using clang++/g++ and make and requires glfw3 and htslib to be available. To build from source use::

    git clone https://github.com/kcleal/gw.git && cd gw
    make prep && make

A conda package will be made available shortly.

User Guide
==========

Sequencing data
--------------------
To view a genomic region e.g. chr1:1-20000, supply an indexed reference genome and an alignment file (using -b option)::

    gw hg38.fa -b your.bam -r chr1:1-20000

.. image:: include/igv.png
    :align: center

This will open a GW window that can be used interactively with the mouse and keyboard. Note multiple -b and -r options can be used.

Various commands are also available via the GW window. Simply click on the GW window and type ":help" which will display a list of commands in your terminal.

.. image:: include/help.png
    :align: center
    :scale: 50%

A GW window can also be started with only the reference genome as a positional argument::

    gw hg38.fa

You can then drag-and-drop alignment files and vcf files into the window, and use commands to navigate to regions etc.

GW can also be used to generate images in .png format of target genomic regions.
To use this function apply the --no-show option along with an output folder --outdir::

    gw hg38.fa -b your.bam -r chr1:1-20000 --outdir . --no-show

Variant data
-----------------
A variant file in .vcf/.bcf format can be opened in a GW window by either dragging-and-dropping or via the -v option::

    gw hg38.fa -b your.bam -v variants.vcf

.. image:: include/tiles.png
    :align: center

This will open a window in tiled mode. To change the number of tiles use the up/down arrow keys to change interactively or use the -n option to control the dimensions::

    gw hg38.fa -n 8x8 -b your.bam -v variants.vcf

If you right-click on one of the tiles then the region will be opened for browsing. To get back to the tiled-image view,
just right-click again.

You can also generate an image of every variant in your vcf file - as before use the --outdir and --no-show options. Also,
you might want to increase the number of threads used here to speed things up a bit. Be warned this will probably generate a huge number of files::

    gw hg38.fa -b your.bam -v variants.vcf --outdir all_images --no-show -t 16

The time taken here depends a great deal on the speed of your hard drive and depth of coverage, but using a fast
NVMe SSD for example, you can expect a throughput around 30-80 images per second.

Labelling variant data
----------------------
GW is designed to make manually labelling 100s - 1000s of variants as pain free as possible. Labels can be saved to
a tab-separated file, and opened at a later date to support labelling over multiple sessions.
GW can also write a modified vcf, updating the vcf filter column with the curated labels.

To use labelling in GW, first ensure all variant IDs in your input vcf are unique.

When you open a vcf file, GW will parse the 'filter' column and display this as a label in the bottom
left-hand corner of image tiles. Other labels can be parsed from the vcf using the --parse-label option.
For example, the "SU" tag can be parsed from the info column using::

    gw hg38.fa -b your.bam -v variants.vcf --parse-label info.SU

Image tiles can then be click-on to modify the label, choosing between PASS/FAIL by default.
To provide a list of alternate labels, use the --labels option::

    gw hg38.fa -b your.bam -v variants.vcf --labels Yes,No,Maybe

Now when you left-click on a tiled image, you can cycle through this list.

To save or open a list of annotations, we recommend using the --in-labels and --out-labels options. This makes it
straightforward to keep track of labelling progress between sessions. Only variants that have been displayed to screen will be appended to
the results in --out-labels::

    gw hg38 -b your.bam -v variants.vcf --in-labels labels.tsv --out-labels labels.tsv

Labels are output as a tab-separated file, for example:

.. list-table::
   :widths: 25 25 25 25 25 25
   :header-rows: 1

   * - #chrom
     - pos
     - variant_ID
     - label
     - var_type
     - labelled_date
   * - chr1
     - 200000
     - 27390
     - PASS
     - DEL
     -
   * - chr1
     - 250000
     - 2720
     - FAIL
     - SNP
     - 14-10-2022 16-05-46

The labelled_date column is only filled out if one of the tiled images was manually clicked - if this field is blank then
the --parsed-label was used. This feature allows you to keep track of which variants were user-labelled over multiple sessions.
Additionally, the --out-labels file is auto-saved every minute for safe keeping.

GW can also write labels to a vcf file. We recommend using this feature to finalise your annotation - the whole vcf file
will be written to --out-vcf. The final label will appear in the 'filter' column in the vcf. Additionally, the date and previous filter label
are kept in the info column under GW_DATE, GW_PREV::

    gw hg38.fa -b your.bam -v variants.vcf --in-labels labels.tsv --out-vcf final_annotations.vcf

Note, the --in-labels option is not required here, but could be used if labelling over multiple sessions, for example. Also,
a GW window will still pop-up here, but this could be supressed using the --no-show option.

Remote
------

GW can be used on remote servers. Simply use `ssh -X remote` when logging on to the server.
When GW is run, the window will show up on your local screen.

Config file
-----------

GW ships with a .gw.ini config file. You can manually set various options within the file so you dont have to keep
typing them in every time.

Some useful options to set in your .gw.ini file are a list of reference genomes so these can be selected without using a full path.
Also things like the theme, image dimensions and hot-keys can be set.

The .gw.ini file can be copied to your home directory or .config directory for safe-keeping - gw will look in these locations before checking the
local install directory.


Issues and contributing
-----------------------
If you find bugs, or have feature requests please open an issue, or drop me an email clealk@cardiff.ac.uk.
GW is under active development, and we would welcome any contributions!
