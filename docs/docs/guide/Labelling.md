---
title: Labelling
layout: home
parent: User guide
nav_order: 4
---

# Labelling

GW is designed to make manually labelling 100s - 1000s of variants as pain free as possible. 
Using the command-line interface, labels can be saved to a tab-separated file and opened 
at a later date to support labelling over multiple sessions.
GW can also write a modified VCF, updating the filter column with curated labels.

![Alt text](/assets/images/tiles1.png "GW")

Use the UP and DOWN arrows to increase or decrease the number of image tiles, or LEFT and RIGHT
to navigate through images. Below is a view of 196 variant images.

![Alt text](/assets/images/tiles2.png "GW")

To use labelling correctly in GW, ensure all variant IDs in your input VCF/BCF are unique - this should 
normally be the case anyway.

When you open a VCF file, GW will parse the 'filter' column and display this as a label
in the bottom left-hand corner of image tiles.
Other labels can be parsed from the VCF using the --parse-label option.
For example, the `SU` tag can be parsed from the info column using:

```shell
gw hg38.fa -b your.bam -v variants.vcf --parse-label info.SU
```
    

Image tiles can then be clicked-on using the mouse to modify the label,
choosing between PASS/FAIL by default. To provide a list of alternate labels, use the `--labels` option:

```shell
gw hg38.fa -b your.bam -v variants.vcf --labels Yes,No,Maybe
```
    

Now, when you left-click on a tiled image, you can cycle through this list.

To save or open a list of annotations, use the `--in-labels` and `--out-labels` options.
This makes it straightforward to keep track of labelling progress between sessions.
Only variants that have been displayed to screen will be appended to the results in `--out-labels` file.
The same file can be used for in and out labels, if desired:

```shell
gw hg38 -b your.bam -v variants.vcf --in-labels labels.tsv --out-labels labels.tsv
```
    

Labels are output as a tab-separated file with the following format:


|chrom|pos|variant_ID|label|var_type|labelled_date|variant_filename
|--|--|---|---|---|---|---
|chr1|200000|27390|PASS|DEL||test.vcf
|chr1|250000|2720|FAIL|SNP|14-10-2022 16-05-46|test.vcf

The labelled_date column is only filled out if one of the tiled images was manually clicked - 
if this field is blank then the `--parsed-label` was used.
This feature allows you to keep track of which variants were user-labelled over multiple sessions.
Additionally, the `--out-labels` file is auto-saved every minute for safe keeping.

GW can also write labels to a VCF file. We recommend using this feature to finalise your annotation - 
the whole VCF file will be written to `--out-vcf`. The final label will appear in the 'filter' column in the vcf.
Additionally, the date and previous filter label are kept in the info column under `GW_DATE`, `GW_PREV`:

```shell
gw hg38.fa -v input_variants.vcf --in-labels labels.tsv \
  --out-vcf output_variants.vcf
```
    

Note, the `--in-labels` option is not required here, but could be used if labelling over multiple sessions, for example.
Also, a GW window will still pop-up here, but this could be supressed using the `--no-show` option.
