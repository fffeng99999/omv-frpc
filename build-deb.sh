#!/bin/sh
#
# Download the frp release archive from GitHub and stage the frpc
# binary under usr/bin/ so that dpkg-buildpackage can pack it into the
# architecture-specific .deb. Only frpc is shipped; frps is not needed
# on an OpenMediaVault client.
#
# Usage:
#   ./build-deb.sh [deb-architecture] [explicit-tag]
#
# Defaults: architecture=amd64, tag=latest non-draft/non-prerelease.
#
# This file is part of OpenMediaVault openmediavault-frpc packaging.
# @license https://www.gnu.org/licenses/gpl.html GPL Version 3

set -eu

REPO="fatedier/frp"
API="https://api.github.com/repos/${REPO}/releases"
ARCH="${1:-amd64}"
TAG="${2:-}"

if [ -n "${GH_TOKEN:-}" ]; then
	AUTH_HEADER="Authorization: Bearer ${GH_TOKEN}"
else
	AUTH_HEADER=""
fi
api_get() {
	if [ -n "$AUTH_HEADER" ]; then
		curl -fsSL -H "$AUTH_HEADER" "$@"
	else
		curl -fsSL "$@"
	fi
}

# Map Debian architecture to the frp upstream linux arch token.
case "$ARCH" in
	amd64) UARCH="amd64" ;;
	arm64) UARCH="arm64" ;;
	armhf) UARCH="arm" ;;
	*)
		echo "ERROR: unsupported architecture '$ARCH' (use amd64|arm64|armhf)" >&2
		exit 1
	;;
esac

# Resolve the upstream frp tag for this build, in order of priority:
#   1. the explicit tag argument (CI workflow_dispatch input / local use)
#   2. the +frpX.Y.Z suffix pinned in debian/changelog (tagged releases
#      register their frp version there, so builds are reproducible)
#   3. the latest stable release from the GitHub API (convenience for
#      untagged local builds only; prints a warning because such a
#      build is not reproducible)
if [ -z "$TAG" ]; then
	CHANGELOG_HEAD="$(head -n 1 debian/changelog 2>/dev/null || true)"
	UPSTREAM_VER="$(printf '%s\n' "$CHANGELOG_HEAD" | sed -n 's/.*(\([^)]*\)).*/\1/p' | sed 's/-[^-]*$//')"
	FRP_FROM_CHANGELOG="$(printf '%s\n' "$UPSTREAM_VER" | sed -n 's/.*+frp//p')"
	if [ -n "$FRP_FROM_CHANGELOG" ]; then
		TAG="v${FRP_FROM_CHANGELOG}"
		echo "Using frp tag pinned in debian/changelog: ${TAG}"
	fi
fi

if [ -z "$TAG" ]; then
	echo "WARNING: no explicit tag and no +frpX.Y.Z suffix in debian/changelog;" >&2
	echo "WARNING: resolving the latest frp release -- this build is NOT reproducible." >&2
	echo "Resolving latest frp release tag from GitHub ..."
	TAG=$(api_get "${API}" \
		| awk '
			/"tag_name":/   { gsub(/.*"tag_name": *"|",?$/, ""); tag=$0 }
			/"draft":/      { gsub(/.*"draft": *|,?$/, ""); draft=$0 }
			/"prerelease":/ {
				gsub(/.*"prerelease": *|,?$/, ""); pre=$0
				if (draft != "true" && pre == "false") { print tag; exit }
			}
		' \
		| head -n 1)
	if [ -z "$TAG" ]; then
		echo "ERROR: could not resolve the latest frp release tag" >&2
		exit 1
	fi
fi

VERSION="${TAG#v}"
ASSET="frp_${VERSION}_linux_${UARCH}.tar.gz"
URL="https://github.com/${REPO}/releases/download/${TAG}/${ASSET}"

echo "Tag        : ${TAG}"
echo "Version    : ${VERSION}"
echo "Debian arch: ${ARCH} (upstream: ${UARCH})"
echo "Asset      : ${ASSET}"
echo "URL        : ${URL}"

WORKDIR="$(mktemp -d)"
trap 'rm -rf "$WORKDIR"' EXIT

echo "Downloading ..."
curl -fL --retry 5 --retry-delay 2 -o "${WORKDIR}/${ASSET}" "$URL"

echo "Extracting ..."
tar xzf "${WORKDIR}/${ASSET}" -C "$WORKDIR"

SRC="${WORKDIR}/frp_${VERSION}_linux_${UARCH}"
if [ ! -f "${SRC}/frpc" ]; then
	SRC=$(find "$WORKDIR" -mindepth 1 -maxdepth 1 -type d | head -n 1)
fi
if [ ! -f "${SRC}/frpc" ]; then
	echo "ERROR: frpc binary not found in ${ASSET}" >&2
	ls -la "$WORKDIR" >&2 || true
	exit 1
fi

DEST="$(dirname "$0")/usr/bin"
echo "Staging into ${DEST} ..."
mkdir -p "$DEST"
cp -a "${SRC}/frpc" "$DEST/frpc"
chmod 0755 "$DEST/frpc"

echo "Done. Staged file:"
ls -la "$DEST/frpc"
"$DEST/frpc" -v || true
