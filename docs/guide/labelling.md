---
title: Labelling
layout: default
parent: User guide
nav_order: 6
description: Labelling variants and images in GW
---

# Labelling
{: .no_toc }

GW makes manually labelling hundreds–thousands of variants as pain-free as possible. Labels save
to a tab-separated file and can be re-opened later, supporting labelling over multiple sessions.
GW can also write a modified VCF with labels in the filter column.

![The Variants panel]({{ site.baseurl }}/assets/images/v2/label_table.png){: width="260" }

## Basic workflow

1. Open a VCF as [tiled images]({{ site.baseurl }}/guide/images.html) — drag it onto the window
   and choose **Variant file as tiled images**
2. Page through the tiles with `←`/`→`; set the number of tiles with the grid button in the
   Variants panel title bar
3. **Click** a tile, or its label in the Variant list, to cycle through the labels
4. [Save the labels](#saving-and-loading-labels) with the file icon in the **Variant List** header

Selecting a row in the Variant list shows the record under **Variant Details**: its fields, INFO
values and per-sample FORMAT values, with buttons to jump to its position.

Ensure all variant IDs in the input VCF/BCF are unique.

To set label files at launch instead:

```shell
gw hg38 -b your.bam -v variants.vcf --in-labels labels.tsv --out-labels labels.tsv
```

## Saving and loading labels

The file icon at the right of the **Variant List** header saves labels to a file and loads them
back, so labelling can be spread over several sessions.

![The label file menu in the Variant List header]({{ site.baseurl }}/assets/images/v2/label_file_menu.png){: width="260" }

| Menu item | What it does |
|-----------|--------------|
| **Save labels...** | Opens a file browser to choose where to save. The name defaults to `labels.tsv` |
| **Save to *file*** | Saves straight to the current label file. Shown once a label file is set |
| **Save labels as...** | Saves to a different file, which becomes the current label file |
| **Load labels...** | Opens a file browser to pick a label file, and applies its labels to the open variant files |

A short message beside the icon confirms the result — **Labels saved** or **Labels loaded** — or
reports a problem, such as a file that is not a label file.

A label file is set by saving once from this menu, or at launch with `--out-labels`. Once set, GW
also saves to it automatically every minute. Labels are matched to variants by variant file name and ID, so load a
label file alongside the variant file it was made from.

The same actions are available from the command bar: `save labels.tsv` and
`load labels labels.tsv`.

## Labels and comments

By default GW parses the VCF `filter` column as the label. Parse another field with
`--parse-label`:

```shell
gw hg38 -b your.bam -v variants.vcf --parse-label info.SU
```

Custom label list:

```shell
gw hg38 -b your.bam -v variants.vcf --labels Yes,No,Maybe
```

To add a comment, move the mouse over the tile, open the command box and type `"` or `'`
followed by the comment. The comment attaches to the label (orange dot) and is saved in the
output.

## Label file format

Tab-separated columns:

| chrom | pos | variant_ID | label | var_type | labelled_date | variant_filename | comment |
|-------|-----|-----------|-------|----------|---------------|------------------|---------|
| chr1 | 200000 | 27390 | PASS | DEL | | test.vcf | |
| chr1 | 250000 | 2720 | FAIL | SNP | 14-10-2022 16-05-46 | test.vcf | |

`labelled_date` is only filled when a tile was manually clicked — blank means the label came from
`--parse-label`. `--out-labels` auto-saves every minute.

## Writing labels to a VCF

Writes the whole VCF to `--out-vcf`, with the final label in the `filter` column. Date and
previous filter are kept under `GW_DATE` and `GW_PREV`:

```shell
gw -v input_variants.vcf --in-labels labels.tsv --out-vcf output_variants.vcf
```
