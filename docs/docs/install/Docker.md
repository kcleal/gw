---
title: Docker
layout: home
parent: Install
nav_order: 6
---

# Install using Docker

A GW docker file for x64 chips can be found in the GW repository: https://github.com/kcleal/gw/blob/master/deps/Dockerfile


- Build the container using:

    sudo docker build -t gw .

- Run using:

    sudo docker run -it --cpus=4 --memory=8g -p 9876:9876 --mount src=DATA_PATH,target=TARGET_PATH,type=bind gw


The -p option binds the port 9876 between host and container.
The --mount option is used to share the folders containing your reference genome and bam files (DATA_PATH)
The TARGET_PATH will be the location of these files in the docker container.


Now open a web-browser and type in the address `0.0.0.0:9876`. A window into the container will be shown. 
You can now run gw using the CLI interface e.g.:

```shell
gw genome/ref.fasta -b bams/your.bam -r chr1:1-20000
```


Also of note, the container contains the micromamba package manager, so you can download and install new packages as needed.
