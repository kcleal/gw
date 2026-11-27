---
title: Settings
layout: default
parent: User guide
nav_order: 8
description: Settings, configuration and themes in GW
---

# Settings and config
{: .no_toc }

Open **Main menu → Settings**. The sections — General, Genomes, Interaction, Labelling,
Navigation, Tracks, Thresholds and Keymap — apply changes immediately and save automatically; a
status message at the bottom reports any save failure. The one exception is `dimensions`, the
starting window size, which is used the next time GW starts.

![The Settings dialog]({{ site.baseurl }}/assets/images/v2/settings.png)

The config file is `.gw.ini`, usually in your home directory or under `~/.config`. You can edit
it by hand — delete it and GW creates a fresh one on restart.

Each section can also be opened from the command bar: `settings`, `general`, `genomes`,
`interaction`, `labelling`, `navigation`, `tracks`, `thresholds`, `keymap`.

## Reference genomes

A reference genome is a unique genome-tag (e.g. `hg19`, `t2t`) plus a local or online FASTA path.
Always use a local file for best performance. Genomes can be selected in three ways:

- **Startup chooser** — when GW starts without a reference, a Reference genome chooser opens in
  the window.
- **Menus** — Settings → Genomes (`+`, tag, path), or Main menu → Open file → Online file.
- **Config file** — the `[genomes]` section of `.gw.ini`, or `load genome hg38` in the command box.

## Custom themes and color palettes

Open the custom theme settings and enable **Use custom colors**. Create your own color palettes:
select a saved theme, or enter a **Name** and click **New**/**Duplicate**. Each theme has an `igv`,
`dark`, `slate`, `tide` or `frost` base. Saved themes appear in Settings → General and via the `theme <name>`
command (names can contain spaces).

Click a swatch to open the colour picker and build a palette — backgrounds, reads, nucleotides,
coverage, tracks, text, highlights, base modifications and translation colors are all adjustable.
A live preview updates as you edit.

Themes live in `.gw.theme.json` beside `.gw.ini`, using gwplot palette names and
`[alpha, red, green, blue]` arrays (0–255); only overridden colours are saved:

```json
{
  "version": 1,
  "themes": [
    {"name": "Ocean", "base": "dark", "colors": {"BACKGROUND": [255, 20, 30, 45]}}
  ]
}
```

Copy the JSON to share themes; **Reload JSON** reads external changes. Legacy gwplot colour maps
load as a theme called **Custom**.

## Default tracks

Default tracks (loaded automatically at startup) are set in Settings → Tracks.
