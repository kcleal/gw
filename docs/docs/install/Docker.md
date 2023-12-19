---
title: Docker
layout: home
parent: Install
nav_order: 6
---

# Install using Docker

To use the [Docker container](https://hub.docker.com/repository/docker/kcleal/gw) interactively (i.e. not just from the command line) you must have 
a screen-sharing tool available on your system - Xpra (https://xpra.org/) can work well for this. This 
allows a screen to be shared between the container and host.

- Grab the gw docker image:

```shell
docker pull kcleal/gw
```

- Run the docker container using:

```shell
sudo docker run -it -p 9876:9876 \
--mount src=/path/genome,target=/home/genome,type=bind,readonly \
--mount src=/path/bams,target=/home/bams,type=bind,readonly \
kcleal/gw
```

The -p option binds the port 9876 between host and container. The --mount options are used to share the folders containing your reference genome and bam files - modify the src=... path as needed.

Now open a web-browser and type in the address `0.0.0.0:9876`. A window into the container will be shown, with an xterm terminal open. You can then run gw using:

```shell
gw genome/ref.fasta -b bams/your.bam -r chr1:1-20000
```

If you want to get data out of the container, you will need a setup another mount point when starting the container e.g.:

```shell
sudo docker run -it -p 9876:9876 \
--mount src=/path/genome,target=/home/genome,type=bind,readonly \
--mount src=/path/bams,target=/home/bams,type=bind,readonly \
--mount src=${PWD},target=/home/out,type=bind \
kcleal/gw
```

Anything saved in /home/out when in the container will be saved in the present-working-directory on your host machine.

Also of note, the container contains the micromamba package manager, so you can download and install new packages as needed.
