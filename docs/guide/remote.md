---
title: Remote access
layout: default
parent: User guide
nav_order: 7
description: Remote data access, online genomes and embedding GW
---

# Remote access and online data
{: .no_toc }

<details open markdown="block">
  <summary>Table of contents</summary>
  {: .text-delta }
1. TOC
{:toc}
</details>

## Online genomes and files

GW loads online reference genomes directly — when GW starts without a reference, a terminal picker
lists the online genomes (type a number or a tag such as `hg38`). The same catalogue is browsable
from the ImGui file dialogs, which can also download remote data into a session.

The `online` command prints links for the current region in online genome browsers:

```shell
online        # using the loaded genome tag
online hg19
```

## UCSC tables

The online files dialog (top menu → Online files) includes a **Search UCSC** interface. Enter an
assembly (e.g. `hg38`) and a search term to query UCSC table tracks directly:

- Matching tracks are listed with their name, assembly and description
- Select a track and click **Download** to fetch it as a local BED file, which GW then draws as a
  track
- Some results are only available as a raw table dump — these download but cannot be drawn

This is the quickest way to pull annotation tracks (genes, repeats, regulatory features and more)
from UCSC into a GW session without leaving the app.

## Direct remote data access

GW reads remote data over HTTP/HTTPS or FTP. Provide full URLs:

```shell
gw https://example.org/ref.fa -b https://example.org/your.bam -r chr1
```

## Secure access

For data behind SSH authentication:

### SSHFS mount

Mount the remote directory locally so GW sees remote files as local.
macOS: install [macFuse](https://macfuse.github.io). Linux/Windows: [SSHFS guide](https://phoenixnap.com/kb/sshfs).

### X11 forwarding

```shell
ssh -X username@remote-server
gw --delay 100   # add delay (ms) to handle latency
```

Best between Linux systems; Mac-to-Linux mostly does not work.

### Xpra

Better performance than X11, supports multiple users:

```shell
xpra start :100 --start="gw ref.fa -b your.bam -r chr1:50000-60000" --sharing=yes --daemon=no
xpra attach ssh:username@remote-server:100
```

Custom SSH: `xpra attach ssh:username@remote-server:100 --ssh "ssh -o IdentitiesOnly=yes -i /path/to/key.pem"`.

### VNC

Full remote desktop (e.g. TigerVNC). Requires administrator setup on the server.

## Comparison

| Method | Performance | Setup | Best for |
|--------|-------------|-------|----------|
| SSHFS | Good | Low | Direct file access |
| X11 | Fair | Low | Quick access, Linux-to-Linux |
| Xpra | Excellent | Medium | Regular remote usage |
| VNC | Good | High | Full desktop |

## Embedding GW in web apps (v2)

GW v2 adds a versioned remote [viewer protocol]({{ site.baseurl }}/api/protocol.html) (JSON over
HTTP/WebSocket) with a WASM build of the renderer — embed GW views in web apps and drive them
remotely, no X11 or VNC needed. See [API & integrations]({{ site.baseurl }}/api/).

## Graphics troubleshooting

Check OpenGL with `glxinfo | grep "direct rendering"` (install `mesa-utils` if missing). Expect
`direct rendering: Yes` and OpenGL 2.0+. Test rendering with `glxgears`.

- **Blank/black window**: `export LIBGL_ALWAYS_SOFTWARE=1`
- **Slow over X11**: increase the delay, e.g. `gw --delay 200`
- **Connection refused**: check `ssh -v username@remote-server` and `X11Forwarding yes` in
  `/etc/ssh/sshd_config`
- **Display export errors**: check `echo $DISPLAY`; set `export DISPLAY=:0` if missing
