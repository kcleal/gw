---
title: Android
layout: home
parent: Install
nav_order: 4
---

# Installing GW on Android


This is a guide to installing GW on your android phone/tablet. 
Currently, using GW on android is only advised for advanced users. Furthermore,
we recommend using a bluetooth mouse and keyboard, as a touch interface can be fiddly to use with a VNC viewer.
Installation involves adding a Linux desktop environment (DE) to your device, so you can potentially use other bioinformatics tools, not just GW!

## Prerequisites

- [Termux](https://f-droid.org/en/packages/com.termux/>)  - a terminal emulator. Get this from the [fdroid repository](https://f-droid.org/en/)
- [VNC Viewer](https://play.google.com/store/apps/details?id=com.realvnc.viewer.android&hl=en_GB&gl=US/>)  - this is the screen sharing tool for your Linux DE

## Installation

GW can be installed using the package manager. Open the termux app, update the package manager and grab GW

```shell
pkg update -y
pkg install -y x11-repo
pkg install -y gw aterm tigervnc
```

The following are recommended to use GW interactively, but not essential:
```shell
pkg install -y xfce4 vim kitty
```

## Usage

- Start the vncserver by opening the termux app. This will enable screen sharing with your ``VNC Viewer`` app. You will be asked to set a password!
  ``vncserver -localhost``

You should see a message like ``New 'localhost:1 ()' desktop is localhost:1``. This is telling you that the screen is hosted at display `:1`, so the connection port will (normally) be 5901.

- Open ``VNC Viewer``. Add a new connection to ``127.0.0.1:5901`` and type in password.

You will see an ``aterm`` terminal window. It is possible to run GW from here, although it's worth going an extra step and starting xfce for a full linux desktop. Type this into the aterm window:
```shell
xfce4-session &
```

You should now see a Linux desktop environment, with access to kitty - a nicer terminal than aterm (see top-left menu Applications -> System -> Kitty). Kitty also works well for accessing data on remote servers using your Android device.

- To access files outside of termux on the device, run ``termux-setup-storage`` in termux and allow file permissions. You can then connect external hard drives, for example.

## Testing


To test gw, you can use any bam file with its reference fasta file. A test bam and fasta can be downloaded by cloning gw from github.

```shell
pkg install git
git clone https://github.com/kcleal/gw.git
```

These files are found in the gw/test directory, and can be viewed with.

```shell
cd gw/test
gw -r chr1 -b test.bam test.fa
```

## Next steps

The gw config file currently ends up in an obscure location. To get gw to read it copy it to your home directory using ``cp $PREFIX/share/doc/gw/gw.ini ~``.

You can also save yourself some typing every session by editing .bashrc.

If you dont have .bashrc in your termux home directory, copy the one from ``cp $PREFIX/etc/bash.bashrc ~/.bashrc``.

Add this to your .bashrc:

```shell
alias vnc="vncserver -geometry 1920x1080 -localhost"
alias desktop="xfce4-session &"
```

Now, typing ``vnc`` in termux will launch the vncserver. When you open your VNC Viewer, typing desktop will start an xfce4 session.


## Debugging

- Check to see what vncservers are already running using ``vncserver -list``. You can kill a server by using ``vncserver -kill :1``, for example.

- You may have an x11 lock file located in ``$PREFIX/tmp``, remove this might help ``rm -rf $PREFIX/tmp/.X*``

Useful links to aid with debugging:

https://wiki.termux.com/wiki/Building_packages
https://wiki.termux.com/wiki/Termux-setup-storage
https://wiki.termux.com/wiki/Graphical_Environment
