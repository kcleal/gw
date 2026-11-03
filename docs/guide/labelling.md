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

<div style="border: 2px dashed #5c5c5c; border-radius: 8px; padding: 3rem 1rem; text-align: center; color: #8a8a8a; margin: 1rem 0;">
  <strong>Screenshot placeholder</strong> — the label table dialog<br>
  <code>docs/assets/images/v2/label_table.png</code>
</div>

## Basic workflow

1. Open a VCF with `-v` so variants are drawn as
   [image tiles]({{ site.baseurl }}/guide/images.html)
2. `↑`/`↓` changes the number of tiles; `←`/`→` navigates
3. **Left-click** a tile to cycle labels
4. Save with `save labels` / `--out-labels`; reload with `--in-labels`

```shell
gw hg38 -b your.bam -v variants.vcf --in-labels labels.tsv --out-labels labels.tsv
```

Ensure all variant IDs in the input VCF/BCF are unique.

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
