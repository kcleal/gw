pkg update
pkg install git wget
[ ! -d ~/.local/bin ] && mkdir ~/.local/bin
[ ! -f ~/.bashrc ] && cp $PREFIX/etc/bash.bashrc ~/.bashrc
local="~/.local/bin"
if [ -d "$local" ] && [[ ":$PATH:" != *":$local:"* ]]; then
    PATH="${PATH:+"$PATH:"}$local"
fi
cd ~
git clone https://github.com/termux/termux-packages.git
cd termux-packages
mkdir x11-repo/gw
cp ../gw/deps/build.sh x11-repo/gw
./scripts/./setup-termmux.sh
./build-package.sh -I gw
cd ../
rm -rf termux-packages
