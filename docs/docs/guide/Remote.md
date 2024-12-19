---
title: Remote Access
layout: home
parent: User guide
nav_order: 11
---

# Accessing GW Remotely

GW supports multiple methods for accessing and visualizing data over remote connections. This guide covers the available approaches and their recommended use cases.

## Direct Remote Data Access

GW can directly access remote data through HTTP/HTTPS or FTP protocols. Simply provide the full URL path (starting with `http://` or `ftp://`) to your data files, and GW will attempt to load them from your local machine.

## Secure Access Methods

For data requiring secure access (e.g., SSH authentication), you have several options:

### 1. SSHFS Mount Method

SSHFS allows you to mount remote directories to your local filesystem, enabling GW to access remote files as if they were local.

#### Platform-Specific Setup:
- **MacOS**: Install macFuse from [macFuse website](https://macfuse.github.io)
- **Linux/Windows**: Follow the [SSHFS setup guide](https://phoenixnap.com/kb/sshfs)

**Advantages:**
- Simple to use once configured
- Transparent access to remote and local files
- No modification to GW usage required

### 2. X11 Forwarding

Run GW on a remote server while displaying the interface on your local machine.

```bash
ssh -X username@remote-server
gw --delay 100  # Add delay to handle latency
```

**Important Notes:**
- Works best between Linux systems
- Mac-to-Linux connections mostly do not work (you may be lucky)
- Use the `--delay` parameter (in milliseconds) to handle latency issues

### 3. Xpra

Xpra offers superior performance compared to X11 forwarding, especially for graphical applications.

#### Setup Instructions:

1. Install Xpra on both local and remote machines
2. Start GW on the remote machine:
   ```bash
   xpra start :100 --start="gw ref.fa -b your.bam -r chr1:50000-60000" --sharing=yes --daemon=no
   ```
3. Connect from your local machine:
   ```bash
   xpra attach ssh:username@remote-server:100
   ```

For custom SSH configurations:
```bash
xpra attach ssh:username@remote-server:100 --ssh "ssh -o IdentitiesOnly=yes -i /path/to/key.pem"
```

**Key Features:**
- Better performance than X11
- Supports multiple simultaneous users
- More resilient to network issues

### 4. VNC Access

For a full remote desktop experience, consider using VNC (e.g., TigerVNC).

**Note:** This method requires administrator setup on the remote server.

## Comparison of Methods

| Method | Performance | Setup Complexity | Best For |
|--------|-------------|------------------|-----------|
| SSHFS | Good | Low | Direct file access |
| X11 | Fair | Low | Quick access, Linux-to-Linux |
| Xpra | Excellent | Medium | Regular remote usage |
| VNC | Good | High | Full desktop access |


### Graphics and Display Issues

#### Testing OpenGL Support

1. Check OpenGL capabilities using `glxinfo`:
```bash
glxinfo | grep "direct rendering"
glxinfo | grep "OpenGL version"
```

If these commands aren't available, install them:
- Ubuntu/Debian: `sudo apt-get install mesa-utils`
- RHEL/CentOS: `sudo yum install glx-utils`

Expected output should show:
- "direct rendering: Yes"
- OpenGL version 2.0 or higher

#### Testing Graphics Performance

Use `glxgears` to test basic 3D rendering:
```bash
glxgears -info
```

This will display rotating gears and report FPS (Frames Per Second). Low FPS (<30) may indicate:
- Graphics driver issues
- X11 forwarding bandwidth limitations
- Hardware acceleration problems

### Common Issues and Solutions

1. Blank or Black Window
    - Check OpenGL support using methods above
    - Try forcing software rendering:
   ```bash
   export LIBGL_ALWAYS_SOFTWARE=1
   ```

2. Slow Performance
    - For X11: Increase delay value
   ```bash
   gw --delay 200  # Adjust value as needed
   ```
    - For Xpra: Use compression options
   ```bash
   xpra start :100 --encoding=rgb --compress=0
   ```

3. Connection Refused
    - Verify SSH access:
   ```bash
   ssh -v username@remote-server
   ```
    - Check if X11 forwarding is enabled in `/etc/ssh/sshd_config`:
   ```
   X11Forwarding yes
   ```

4. Display Export Errors
    - Ensure DISPLAY variable is set:
   ```bash
   echo $DISPLAY
   ```
    - Set if missing:
   ```bash
   export DISPLAY=:0
   ```

### Getting Help

If problems persist please raise an issue:

1. Collect system information:
```bash
gw --version
GW_DEBUG=1 gw hg19 
glxinfo
echo $DISPLAY
```
