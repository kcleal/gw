---
title: Linux
layout: home
parent: Install
nav_order: 2
---

# Installing GW on Linux x86_64 Systems

To install GW as a desktop application on debian systems (Intel only),
click the download links below, or head over to GitHub [Releases page](https://github.com/kcleal/gw/releases).

|  [GW Intel x86_64 debian installer]()


GW is also available for Linux x86_64 systems as a command-line tool and can be installed using Conda.

## Using Conda

Once [Conda](https://docs.conda.io/projects/miniconda/en/latest/miniconda-other-installer-links.html) is installed, 
you can install GW by running the following command in your terminal:

```shell
conda install -c conda-forge -c bioconda gw
```


# Building from source

Before building GW, you need to install the glfw3 and htslib libraries.
For Ubuntu, fetch these using:

```shell
sudo apt install libgl1-mesa-dev libfontconfig-dev libhts-dev glfw
```

GW can be built and installed using:
```shell
git clone https://github.com/kcleal/gw.git && cd gw
make prep && make
cp gw /usr/local/bin
```

<br>
After installation, you can verify that GW has been installed correctly by running:

```shell
gw --version
```