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

1. Open a variant file (VCF, BCF, BED or a GW label file) — drag it onto the window and choose
   **Variant file as tiled images**, or start GW with `-v variants.vcf`
2. GW opens the first variant in the alignment view, with the Variants panel alongside
3. Step through the variants and label each one — see [Reviewing variants](#reviewing-variants)
4. [Save the labels](#saving-and-loading-labels) with the file icon in the **Variant List** header

Ensure all variant IDs in the input VCF/BCF are unique.

To set label files at launch:

```shell
gw hg38 -b your.bam -v variants.vcf --in-labels labels.tsv --out-labels labels.tsv
```

## Reviewing variants

Variants can be reviewed one at a time in the live alignment view, or many at a time as static
image tiles. Both views share the Variants panel, and labels set in one show in the other.

### One at a time: the alignment view

![A variant open in the alignment view, with the label chip and step arrows]({{ site.baseurl }}/assets/images/v2/variant_review.png)

This is the view a variant file opens in. The plot is fully interactive — pan, zoom, click reads —
and three controls tie it to the variant list:

| Control | What it does |
|---------|--------------|
| **Step arrows** | The triangles at the left and right edges of the plot go to the previous and next variant. So does `Cmd`/`Ctrl` + `←`/`→` |
| **Label chip** | The pill at the bottom of the plot shows the variant's label, type, genotype and position. Click it to cycle the label |
| **Number keys** | `1` sets the first label, `2` the second, and so on |

The arrows are faint until the pointer is over them, and fade out after five seconds without
pointer movement, so they stay out of screenshots. Move the pointer to bring them back.

Once you pan or zoom away from the variant, the chip shrinks to the label alone. Clicking it then
returns to the variant instead of changing the label. The number keys always label the variant
you stepped to, wherever the view has moved.

The Variant list follows along: the row of the variant in view is selected, and **Variant
Details** shows its fields, INFO values and per-sample FORMAT values, with buttons to jump to its
position. The filled triangle beside a position marks the variant in the alignment view; click
the triangle on any other row to open that variant.

`PASS` is drawn as the chip that stands out — bright on dark themes, dark on light themes — and
every other label is subdued.

### Many at a time: the tile view

![Variant image tiles with the Variants panel]({{ site.baseurl }}/assets/images/v2/tiles.png)

Each variant is drawn as a static image tile. Two buttons in the Variants panel title bar control
it:

| Button | What it does |
|--------|--------------|
| **Grid** (four squares) | Sets how many tiles are shown, and switches to the tile view. Choosing 1 x 1 returns to the alignment view |
| **Tile view** (picture) | Turns the tile view on or off at any grid size, including 1 x 1. Highlighted while on |

In the tile view:

- **Scroll** the mouse wheel, use `←`/`→`, or click the step arrows to page through the tiles
- **Click** a tile, or its label in the Variant list, to cycle through the labels. Number keys
  label the tile under the pointer
- Click a position in the Variant list to go to the page holding that variant
- The triangle beside a position opens that variant in the alignment view; click it again to
  return to the tiles
- `Tab` switches between the tiles and the alignment view

With a 1 x 1 grid there is normally no tile view: the single variant is shown live in the
alignment view and `Tab` does nothing. Turn on the **Tile view** button if you want a single
static tile, for example to page through variants with the mouse wheel.

Folders of pre-made images (`-i`) are always shown as tiles.

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

## File and label information

The info icon next to the file icon opens a window describing the variant file in view:

- **File** — path, format, size, the number of variants read and the sample names, with a count of
  each variant type
- **Labels** — the label file in use, how many labels it holds for this variant file, how many
  labels you have changed in this session, and a count of each label
- **VCF header** — for VCF and BCF files, the header laid out as tables of `FILTER`, `INFO` and
  `FORMAT` fields, contigs and other lines. **Copy header** copies the raw header text

Large files are read as you scroll, so the counts cover the variants read so far until the whole
file has been loaded.

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

To add a comment, select the variant (move the mouse over its tile, or open it in the alignment
view), open the command box and type `"` or `'` followed by the comment. The comment attaches to the label (orange dot) and is saved in the
output.

## Label file format

Tab-separated columns:

| chrom | pos | variant_ID | label | var_type | labelled_date | variant_filename | comment |
|-------|-----|-----------|-------|----------|---------------|------------------|---------|
| chr1 | 200000 | 27390 | PASS | DEL | | test.vcf | |
| chr1 | 250000 | 2720 | FAIL | SNP | 14-10-2022 16-05-46 | test.vcf | |

`labelled_date` is only filled when a label was set by hand — blank means the label came from
`--parse-label`. `--out-labels` auto-saves every minute.

## Writing labels to a VCF

Writes the whole VCF to `--out-vcf`, with the final label in the `filter` column. Date and
previous filter are kept under `GW_DATE` and `GW_PREV`:

```shell
gw -v input_variants.vcf --in-labels labels.tsv --out-vcf output_variants.vcf
```
