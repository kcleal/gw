pkg update
pkg install git wget
mkdir -p ~/.local/bin
[ ! -f ~/.bashrc ] && cp $PREFIX/etc/bash.bashrc ~/.bashrc
local="~/.local/bin"
if [ -d "$local" ] && [[ ":$PATH:" != *":$local:"* ]]; then
    PATH="${PATH:+"$PATH:"}$local"
fi
git clone https://github.com/termux/termux-packages.git
mkdir -p ./termux-packages/x11-repo/gw
cp ./gw/deps/build.sh ./termux-packages/x11-repo/gw
./termux-packages/scripts/./setup-termux.sh
./termux-packages/build-package.sh -I ~/termux-packages/x11-repo/gw
rm -rf termux-packages

# These are optional but recommended unless similar packages are already installed
pkg install x11-repo tigervnc xfce4 xfce4-goodies vim

mkdir -p ~/.vnc
echo "#!/data/data/com.termux/files/usr/bin/sh" > ~/.vnc/xstartup # to clear file
echo "xfce4-session &" >> ~/.vnc/xstartup

# The port by default is 5900, so display port will be 5901 on the display server
if grep "DISPLAY=" ~/.bashrc 
then 
    echo "DSIPLAY already set"
else 
    echo "DISPLAY=:1" >> .bashrc
fi
