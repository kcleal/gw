---
title: Install
layout: default
nav_order: 3
description: Build GW version 2 from source
---

# Installing GW version 2
{: .no_toc }

{: .warning }
**GW v2 is in beta.** It is not available from package managers (conda, Homebrew) or as a
downloadable app — you must build it from source. The v1 packages remain available from the
[v1 install docs]({{ site.baseurl }}/v1/install/Install.html).

<details open markdown="block">
  <summary>Table of contents</summary>
  {: .text-delta }
1. TOC
{:toc}
</details>

## Requirements

- C++17 compiler (clang or gcc)
- [htslib](https://github.com/samtools/htslib) and [GLFW3](http://www.glfw.org)
- `ncurses` (`ncursesw` on Windows/MSYS2), for the startup genome picker
- `pkg-config`
- ~4 GB disk for the Skia dependency (downloaded by `make prep`)

Easiest route — conda:

```shell
conda create -y -n gw_env -c conda-forge glfw htslib ncurses
conda activate gw_env
```

## Build

```shell
git clone https://github.com/kcleal/gw.git
cd gw
make prep   # downloads pre-built Skia (m143)
CONDA_PREFIX=$(conda info --base) LDLIBS+="-lcrypto -lssl" make -j4
```

Produces a single `gw` binary.

### macOS

Or with Homebrew:

```shell
brew install glfw htslib ncurses pkg-config
make prep && make -j4
```

On Apple Silicon you may need:

```shell
make -j4 LDFLAGS="-L/opt/homebrew/lib" CPPFLAGS="-I/opt/homebrew/include"
```

### Linux

```shell
sudo apt-get install libglfw3-dev libhts-dev libncurses-dev pkg-config
make prep && make -j4
```

### Windows

Build under MSYS2 (UCRT64):

```shell
pacman -S mingw-w64-ucrt-x86_64-glfw mingw-w64-ucrt-x86_64-ncurses mingw-w64-ucrt-x86_64-htslib pkg-config
make prep && make -j4
```

## Make targets

| Target | Purpose |
| ------ | ------- |
| `make` | Build the `gw` binary |
| `make debug` | Debug build |
| `make shared` | Build `libgw` shared library |
| `make server` | Build `gw-server` (see the [viewer protocol]({{ site.baseurl }}/api/protocol.html)) |
| `make clean` | Remove objects |

## Verify

```shell
gw --version    # e.g. 2.0.0
gw hg38         # launch with the hg38 online genome
```

## Upgrading from v1

GW v2 reads the same `.gw.ini` config, sessions and file formats as v1. Note:

- The overlay UI has moved to ImGui — see [The interface]({{ site.baseurl }}/guide/interface.html)
- New commands are available — see [Commands]({{ site.baseurl }}/guide/commands.html)
- The v1 command-line options remain supported
