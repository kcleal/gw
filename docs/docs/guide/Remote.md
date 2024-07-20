---
title: Remote
layout: home
parent: User guide
nav_order: 11
---

# Loading remote data

GW can open reference genomes, bam files and data tracks from remote servers. Just supply a suitable path starting
with `http` or `ftp` and GW will attempt to load the file. 


# Using GW on remote machines

GW can be used on remote servers by using `ssh -X` when logging on to the server, and a 
GW window will show up on your local screen. This should work seamlessly with 
linux server-client machines, although there are known issues with Mac-Linux server-client interfaces (although these
still work sometimes).


We also recommend adding an update delay (in miliseconds) using `gw --delay 100` which can help prevent bandwidth/latency issues.

Alternatively, the screen sharing tool Xpra can offer much better performance for rendering over a remote connection.

Xpra will need to be installed on local and remote machines. One way to use Xpra is to start GW on port 100 (on remote machine) using:

```shell
xpra start :100 --start="gw ref.fa -b your.bam -r chr1:50000-60000" --sharing=yes --daemon=no
```

You (or potentially multiple users) can view the GW window on your local machine using:

```shell
xpra attach ssh:ubuntu@18.234.114.252:100
```

The :100 indicates the port. If you need to supply more options to the ssh command use e.g. 
`xpra attach ssh:ubuntu@18.234.114.252:102 --ssh "ssh -o IdentitiesOnly=yes -i .ssh/dysgu.pem"`
