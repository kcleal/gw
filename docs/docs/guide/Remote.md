---
title: Remote
layout: home
parent: User guide
nav_order: 11
---

# Loading remote data

If your remote data is available via a http/https or ftp site then GW should be able to open them.
Just supply a suitable path starting with `http` or `ftp` and GW will attempt to load the file. 

If data access requires a login e.g. via ssh, then there are a few options:

## Mounting remote data using sshfs

Using sshfs, data on your remote server can be mounted to 
your local file system. GW will then be able to load data directly from the mounted drive, in addition to 
your local file system. 

MacOS uses will want to use macFuse for this: https://macfuse.github.io.
An installation tutorial can be found here: https://phoenixnap.com/kb/sshfs-mac. 

For Linux and Windows users, follow this guide: https://phoenixnap.com/kb/sshfs

## X11 forwarding from the command line

GW can be used on remote servers by using `ssh -X` when logging on to the server, and a 
GW window will show up on your local screen. This should work seamlessly with 
linux server-client machines, although there are known issues with Mac-Linux server-client interfaces (although these
still work sometimes).

Also, an update delay (in miliseconds) should be added using `gw --delay 100` which can help prevent bandwidth/latency issues.


## Xpra from the command line
The screen sharing tool Xpra can offer much better performance for rendering over a remote connection
compared to X11 forwarding.

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
