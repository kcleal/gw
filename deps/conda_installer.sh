#!/bin/bash
set -e

# Function to check if command exists
command_exists() {
    command -v "$1" >/dev/null 2>&1
}

# Check for conda or mamba
if command_exists mamba; then
    CONDA_CMD="mamba"
elif command_exists conda; then
    CONDA_CMD="conda"
else
    echo "Error: Neither conda nor mamba found. Please install one of them first."
    exit 1
fi

# Get conda paths
CONDA_PREFIX=$(conda info --base)
CONDA_BIN="$CONDA_PREFIX/bin"
CONDA_LIB="$CONDA_PREFIX/lib"

# Install dependencies
$CONDA_CMD install -c conda-forge -c bioconda -y \
    fontconfig libuuid libcurl "libdeflate>=1.0" "htslib>=1.12" "glfw>=3.3" \
    unzip freetype zlib ninja harfbuzz libwebp gn \
    pthread-stubs xorg-libsm xorg-libice xorg-libx11 libxcb \
    libegl-devel libgl-devel libglx-devel libgles-devel wayland \
    conda-build

# Create temp directory
TEMP_DIR=$(mktemp -d)
cd "$TEMP_DIR"

# Download latest release
LATEST_RELEASE_URL=$(curl -s https://api.github.com/repos/kcleal/gw/releases/latest | grep "tarball_url" | cut -d '"' -f 4)
curl -L "$LATEST_RELEASE_URL" -o gw.tar.gz
tar -xzf gw.tar.gz
cd kcleal-gw-*

# Set environment variables
export CPPFLAGS="-I$CONDA_PREFIX/include $CPPFLAGS"
export LDFLAGS="-L$CONDA_PREFIX/lib $LDFLAGS"

# Build GW
make prep > /dev/null 2>&1
make -j4

# Create conda package
mkdir -p conda_pkg/gw/bin
cp gw conda_pkg/gw/bin/

# Create post-link script
mkdir -p conda_pkg/gw/
cat << EOF > conda_pkg/gw/post-link.sh
#!/bin/bash
set -e
PACKAGE_DIR=\$(dirname \$(dirname \$0))
cp "\$PACKAGE_DIR/gw/bin/gw" "\$PREFIX/bin/gw"
EOF
chmod +x conda_pkg/gw/post-link.sh

# Create meta.yaml for conda package
cat << EOF > conda_pkg/meta.yaml
package:
  name: gw
  version: $(./gw --version | cut -d ' ' -f 2)
build:
  number: 0
  script_env:
    - PREFIX
  post-link: gw/post-link.sh
requirements:
  run:
    - fontconfig
    - libuuid
    - libcurl
    - libdeflate >=1.0
    - htslib >=1.12
    - glfw >=3.3
    - freetype
    - zlib
    - harfbuzz
    - libwebp
    - pthread-stubs  # [linux]
    - xorg-libsm     # [linux]
    - xorg-libice    # [linux]
    - xorg-libx11    # [linux]
    - libxcb         # [linux]
    - libegl-devel   # [linux]
    - libgl-devel    # [linux]
    - libglx-devel   # [linux]
    - libgles-devel  # [linux]
    - wayland        # [linux]
about:
  home: https://github.com/kcleal/gw
  license: MIT
  summary: GW package
EOF

# Build and install the conda package
conda build --output-folder $TEMP_DIR/conda-bld conda_pkg
$CONDA_CMD install -y $TEMP_DIR/conda-bld/*/gw-*.tar.bz2

# Manually link the binary if it's not in the right place
if [ ! -f "$CONDA_PREFIX/bin/gw" ]; then
    cp "$CONDA_PREFIX/pkgs/gw-$(./gw --version | cut -d ' ' -f 2)-0/info/recipe/gw/bin/gw" "$CONDA_PREFIX/bin/gw"
fi

echo "GW has been successfully installed as a conda package."
echo "The GW binary should now be available in your conda environment."
echo "You can use 'conda uninstall gw' to remove it if needed."
