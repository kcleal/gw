# GW documentation

Documentation site for [GW](https://github.com/kcleal/gw), built with
[Jekyll](https://jekyllrb.com) and [Just the Docs](https://just-the-docs.github.io/just-the-docs/),
published to GitHub Pages.

## Structure

- `index.md`, `quickstart.md`, `install.md` — GW version 2 landing pages
- `guide/` — version 2 user guide (interface, commands, tracks, images, labelling, remote access, settings)
- `api/` — version 2 API: libgw C++ API, remote viewer protocol, Python (gwplot)
- `v1/` — archived GW version 1 documentation
- `assets/` — images and demo data

The `app/` directory (in-browser WASM demo) is not committed — the pages
workflow builds it and adds it to the site on each deploy.

Screenshot placeholders reference files under `assets/images/v2/` — drop PNGs there using the
indicated names to replace the placeholders.

## Building locally

```shell
cd docs
bundle install
bundle exec jekyll serve   # preview at localhost:4000/gw
```

The built site goes to `_site/`.

## Publishing

Built and deployed by `.github/workflows/pages.yml` on pushes to `main`.
