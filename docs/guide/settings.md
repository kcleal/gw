---
title: Settings
layout: default
parent: User guide
nav_order: 8
description: Settings, configuration and themes in GW
---

# Settings and config
{: .no_toc }

Open Settings from the top-left menu. Settings save automatically; a status message at the bottom
reports any save failure.

The config file is `.gw.ini`, usually in your home directory or under `~/.config`. You can edit
it by hand — delete it and GW creates a fresh one on restart.

Settings sections can also be opened as commands: `settings`, `general`, `genomes`,
`interaction`, `labelling`, `navigation`, `tracks`, `thresholds`, `keymap`.

<div style="border: 2px dashed #5c5c5c; border-radius: 8px; padding: 3rem 1rem; text-align: center; color: #8a8a8a; margin: 1rem 0;">
  <strong>Screenshot placeholder</strong> — settings dialog with the theme editor<br>
  <code>docs/assets/images/v2/settings.png</code>
</div>

## Reference genomes

A reference genome is a unique genome-tag (e.g. `hg19`, `t2t`) plus a local or online FASTA path.
Always use a local file for best performance. Genomes can be selected in three ways:

- **Startup picker** — when GW starts without a reference, a terminal (ncurses) picker lists the
  online genomes. Type a number or a tag such as `hg38`.
- **ImGui menus** — Settings → Genomes (`+`, tag, path), or the online files dialog.
- **Config file** — the `[genomes]` section of `.gw.ini`, or `load genome hg38` in the command box.

## Custom themes and color palettes

Open the custom theme settings and enable **Use custom colors**. Create your own color palettes:
select a saved theme, or enter a **Name** and click **New**/**Duplicate**. Each theme has an `igv`,
`dark` or `slate` base. Saved themes appear in Settings → General and via the `theme <name>`
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
