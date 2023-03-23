TERMUX_PKG_HOMEPAGE=https://github.com/kcleal/gw
TERMUX_PKG_DESCRIPTION="Genome browser"
TERMUX_PKG_LICENSE="MIT"
TERMUX_PKG_MAINTAINER="clealk@cardiff.ac.uk"
TERMUX_PKG_VERSION=0.7.0
TERMUX_PKG_SRCURL=https://github.com/kcleal/gw/archive/v${TERMUX_PKG_VERSION}.tar.gz
TERMUX_PKG_SHA256=92599d100755a5a20dc15ebfb86fdf0818ccc87c6e0a2cc0a7c3061661ed3d25
TERMUX_PKG_AUTO_UPDATE=true
TERMUX_PKG_DEPENDS=glfw
TERMUX_PKG_DEPENDS=x11-repo
TERMUX_PKG_BUILD_DEPENDS="make, autoconf, git, fontconfig, freetype"
TERMUX_PKG_DEPENDS="mesa"
TERMUX_PKG_BLACKLISTED_ARCHES="arm, i686"


termux_step_make_install() {

	# git clone https://github.com/kcleal/gw
	cd gw
	
	git clone https://github.com/ebiggers/libdeflate.git
        cd libdeflate
        cmake -B build && cmake --build build
        cd ../
	
	git clone https://github.com/samtools/htslib.git
	cd htslib
	cp ../libdeflate/build/libdeflate.a .
	cp ../libdeflate/libdeflate.h .
	git submodule update --init --recursive
	autoreconf -i && ./configure && make
	cd .. 
	
	sed -i 's/Release-x64/Release-arm64/g' Makefile
	sed -i 's/linux/android/g' Makefile
	make prep
	CPPFLAGS+=" -I./htslib -I./lib/skia/include" LDFLAGS+=" -L./htslib" LDLIBS+=" -lEGL -llog" make
	cp gw ~/.local/bin
        cp htslib/libhts.* ${PREFIX}/lib
}
