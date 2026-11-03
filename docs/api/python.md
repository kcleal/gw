---
title: Python (gwplot)
layout: default
parent: API and integrations
nav_order: 3
description: Using GW from Python via the gwplot package
---

# Python (gwplot)

The [gwplot](https://github.com/kcleal/gwplot) package is a Python interface to GW's renderer.
The gwplot API is unchanged in v2 — existing scripts keep working.

```shell
pip install gwplot
```

```python
from gwplot import Gw

gw = Gw("hg38")                    # reference genome tag or fasta path
gw.add_bam("sample.bam")
gw.add_track("variants.vcf")
gw.add_region("chr1", 1000000, 1100000)
gw.save_png("output.png")
```

Themes save/load with `save_theme_to_json` / `load_theme_from_json` — the same `.gw.theme.json`
format as the desktop app (see [Settings]({{ site.baseurl }}/guide/settings.html)).

To serve GW views to many clients, gwplot sessions can back the
[remote viewer protocol]({{ site.baseurl }}/api/protocol.html) by implementing the `Backend`
interface in `deps/protocol/viewer/v1` (importable as `protocol.viewer.v1` with `deps/` on `sys.path`).
