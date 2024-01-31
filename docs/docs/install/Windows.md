---
title: Windows
layout: home
parent: Install
nav_order: 3
---


# Installing GW on Windows
GW can be installed on Windows using MSYS2 or Windows Subsystem for Linux (WSL). 
Below are the instructions for both methods.

## Using MSYS2

MSYS2 is a software distro and building platform for Windows. 
Follow these steps to install GW using MSYS2:

- Download and Install MSYS2:

Visit the [MSYS2 website](https://www.msys2.org) and download the installer.
Follow the installation instructions on the website to set up MSYS2 on your Windows system.

- Install GW using install script:

Download the GW installer script below:


[GW Intel x86_64 Windows installer](https://github.com/kcleal/gw/releases/download/v0.9.3/gw-windows-installer.vbs)

[GW Intel x86_64 Windows installer](https://github.com/kcleal/gw/releases/download/v0.9.2/gw-windows-installer.vbs)


Run the downloaded visual-basic script by double-clicking, or right-clicking and selecting Run as program.
The script will install GW, add a shortcut to GW in the Start Menu and put GW on your PATH. This means GW
will be accessible from the Start Menu as well as an ucrt64 terminal, command prompt and powershell.

Alternatively, GW can be installed by opening a ucrt64 shell after MYSYS2 installation,
and running the following command:

```shell
pacman -Sy mingw-w64-ucrt-x86_64-gw
```

## Using Windows Subsystem for Linux (WSL)

If you prefer using a Linux environment on your Windows machine, you can use WSL:

- Install WSL:

Follow Microsoft’s [guide on installing WSL](https://learn.microsoft.com/en-us/windows/wsl/install).
Choose a Linux distribution of your choice (like Ubuntu) from the Microsoft Store.
- Install GW within WSL:

Once WSL is set up with your chosen Linux distribution, open the WSL terminal.
You can then follow the Linux installation instructions for GW (as provided in the Linux section of the documentation).
