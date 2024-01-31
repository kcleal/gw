---
title: MacOS
layout: home
parent: Install
nav_order: 1
---

# Installing GW on macOS

For best performance, install GW as a desktop application (Intel or Apple silicone), or build from source. Use one of the
installers below, or head over to the GitHub [Releases page](https://github.com/kcleal/gw/releases).

|  [GW Intel x86_64 mac dmg installer](https://github.com/kcleal/gw/releases/download/v0.9.3/gw_macos_intel.dmg)
|  [GW Apple arm64 mac dmg installer](https://github.com/kcleal/gw/releases/download/v0.9.3/gw-macos-arm.dmg)

GW can be copied to your bin directory to make it available as a command-line tool:
```shell
cp /Applications/gw.app/Contents/MacOS/gw /usr/local/bin
```

To install GW as a command-line tool, you have two primary options depending on your system 
architecture: using Conda or Homebrew. 
These instructions cover both Intel-based (x86_64) and Apple Silicon 
(M1, M1 Pro, M1 Max, M2, etc.) Macs.

## Using Conda (Intel chips)

For users who prefer using [Conda](https://docs.conda.io/projects/miniconda/en/latest/miniconda-other-installer-links.html)
, GW can be installed from the Conda-Forge 
and Bioconda channels. This method works for Intel architectures only.
Open your terminal and execute the following command:

```shell
conda install -c conda-forge -c bioconda gw
```

## Using Homebrew (Intel + Apple chips)

For those who prefer Homebrew, GW has a Homebrew tap available. 
This method is compatible with both Intel and Apple Silicon Macs. 
Open your terminal and run:

```shell
brew install kcleal/homebrew-gw/gw
```


# Building from source

Before building GW, you need to install the glfw3 and htslib libraries.
These can be easily installed using Conda, and optionally,
setting the CONDA_PREFIX environment variable is advised 
so gw can find the required libraries.

```shell
conda install -c conda-forge -c bioconda glfw htslib
export CONDA_PREFIX=~/miniconda3
```

Next clone the GW repository and build GW:

```shell
git clone https://github.com/kcleal/gw.git && cd gw
make prep && make -j8
```

Also, it is recommended to add GW to your bin directory:

```shell
cp gw /usr/local/bin
```

<br>
After installation, you can verify that GW has been installed correctly by running:

```shell
gw --version
```