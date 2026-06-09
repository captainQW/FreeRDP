#!/usr/bin/env bash
#
# Build the FreeRDP clients on Debian / Ubuntu.
#
# Produces:
#   - sdl-freerdp / sdl3-freerdp  (SDL based client, the recommended one)
#   - xfreerdp                    (X11 client, incl. the RemoteApp launch splash)
#
# Usage:
#   chmod +x scripts/build-debian.sh
#   ./scripts/build-debian.sh                 # configure + build into ./build-debian
#   sudo ./scripts/build-debian.sh --deps     # only install apt build dependencies
#   ./scripts/build-debian.sh --install /opt/freerdp   # build + install to a prefix
#
# Tested on Debian 12 (bookworm) and Ubuntu 22.04/24.04.
set -euo pipefail

# ---------------------------------------------------------------------------
# Configuration (override via environment)
# ---------------------------------------------------------------------------
SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${SRC_DIR}/build-debian}"
BUILD_TYPE="${BUILD_TYPE:-Release}"
INSTALL_PREFIX=""
DO_DEPS=0
DO_INSTALL=0
JOBS="${JOBS:-$(nproc)}"

log()  { printf '\033[1;34m==>\033[0m %s\n' "$*"; }
die()  { printf '\033[1;31mERROR:\033[0m %s\n' "$*" >&2; exit 1; }

while [ $# -gt 0 ]; do
	case "$1" in
		--deps)     DO_DEPS=1 ;;
		--install)  DO_INSTALL=1; INSTALL_PREFIX="${2:?--install needs a prefix}"; shift ;;
		--build-dir) BUILD_DIR="${2:?}"; shift ;;
		--debug)    BUILD_TYPE="Debug" ;;
		-h|--help)
			grep '^#' "$0" | sed 's/^#\s\?//'; exit 0 ;;
		*) die "unknown argument: $1" ;;
	esac
	shift
done

# ---------------------------------------------------------------------------
# 1. Build dependencies
# ---------------------------------------------------------------------------
APT_PACKAGES=(
	# tool chain
	build-essential cmake ninja-build pkg-config git
	# crypto / auth
	libssl-dev libkrb5-dev libpam0g-dev
	# unicode
	libicu-dev
	# audio
	libasound2-dev libpulse-dev
	# image / scaling
	libswscale-dev libcairo2-dev
	# H.264
	libavcodec-dev libavutil-dev libopenh264-dev
	# JSON / smartcard / usb / fuse / cups
	libcjson-dev libpcsclite-dev libusb-1.0-0-dev libfuse3-dev libcups2-dev
	libsystemd-dev
	# X11 client
	libx11-dev libxext-dev libxinerama-dev libxcursor-dev libxdamage-dev
	libxv-dev libxkbfile-dev libxrandr-dev libxi-dev libxrender-dev libxfixes-dev
	libxtst-dev
	# Wayland client (optional)
	libwayland-dev libxkbcommon-dev wayland-protocols
	# SDL client
	libsdl2-dev libsdl2-ttf-dev libsdl3-dev libsdl3-ttf-dev
	# FIDO2 / WebAuthn redirection (optional)
	libcbor-dev libfido2-dev
)

install_deps() {
	log "Installing build dependencies via apt (requires root)"
	export DEBIAN_FRONTEND=noninteractive
	apt-get update
	# Some packages (libsdl3-dev) are only on newer releases; install best-effort.
	for pkg in "${APT_PACKAGES[@]}"; do
		if ! apt-get install -y --no-install-recommends "$pkg" 2>/dev/null; then
			printf '   (skipping unavailable package: %s)\n' "$pkg"
		fi
	done
	log "Dependencies installed"
}

if [ "$DO_DEPS" -eq 1 ]; then
	[ "$(id -u)" -eq 0 ] || die "--deps must run as root (use sudo)"
	install_deps
	# If only --deps was requested, stop here.
	[ "$DO_INSTALL" -eq 0 ] && exit 0
fi

# ---------------------------------------------------------------------------
# 2. Configure
# ---------------------------------------------------------------------------
command -v cmake >/dev/null   || die "cmake not found - run: sudo $0 --deps"
command -v ninja >/dev/null   || die "ninja not found - run: sudo $0 --deps"

CMAKE_ARGS=(
	-GNinja
	-DCMAKE_BUILD_TYPE="${BUILD_TYPE}"
	-DWITH_VERBOSE_WINPR_ASSERT=OFF

	# clients
	-DWITH_CLIENT=ON
	-DWITH_CLIENT_SDL=ON          # sdl-freerdp / sdl3-freerdp
	-DWITH_X11=ON                 # xfreerdp (incl. RemoteApp launch splash)
	-DWITH_WAYLAND=ON

	# servers off (client only build)
	-DWITH_SERVER=OFF
	-DWITH_SHADOW=OFF
	-DWITH_PROXY=OFF
	-DWITH_SAMPLE=OFF

	# codecs / features
	-DWITH_OPENSSL=ON
	-DWITH_OPENH264=ON
	-DWITH_SWSCALE=ON
	-DWITH_CAIRO=ON
	-DWITH_FFMPEG=ON
	-DWITH_KRB5=ON
	-DWITH_PULSE=ON
	-DWITH_ALSA=ON
	-DWITH_CUPS=ON
	-DWITH_FUSE=ON
	-DWITH_WINPR_TOOLS=ON

	-DWITH_CHANNELS=ON
    -DWITH_CLIENT_CHANNELS=ON
    -DWITH_OPUS=ON
    -DWITH_DSP_FFMPEG=OFF
    -DWITH_INTERNAL_MD4=ON
    -DWITH_INTERNAL_MD5=ON
    -DWITH_INTERNAL_RC4=ON
)

if [ "$DO_INSTALL" -eq 1 ]; then
	CMAKE_ARGS+=( -DCMAKE_INSTALL_PREFIX="${INSTALL_PREFIX}" )
fi

log "Source : ${SRC_DIR}"
log "Build  : ${BUILD_DIR} (${BUILD_TYPE})"
log "Configuring"
cmake "${CMAKE_ARGS[@]}" -S "${SRC_DIR}" -B "${BUILD_DIR}"

# ---------------------------------------------------------------------------
# 3. Build
# ---------------------------------------------------------------------------
log "Building with ${JOBS} jobs"
cmake --build "${BUILD_DIR}" -j "${JOBS}"

# ---------------------------------------------------------------------------
# 4. Install (optional) + summary
# ---------------------------------------------------------------------------
if [ "$DO_INSTALL" -eq 1 ]; then
	log "Installing to ${INSTALL_PREFIX}"
	cmake --install "${BUILD_DIR}"
fi

log "Build finished. Client binaries:"
find "${BUILD_DIR}/client" -maxdepth 3 -type f -perm -u+x \
	\( -name 'xfreerdp' -o -name 'sdl-freerdp' -o -name 'sdl3-freerdp' -o -name 'wlfreerdp' \) \
	2>/dev/null | sed 's/^/   /' || true

cat <<'EOF'

Done.

Run the X11 client (RemoteApp with launch splash):
   ./build-debian/client/X11/xfreerdp /v:HOST:PORT /u:USER \
       /app:program:"||APP" /cert:ignore

Run the SDL client:
   ./build-debian/client/SDL/SDL3/sdl3-freerdp /v:HOST:PORT /u:USER /cert:ignore
EOF
