#!/usr/bin/env bash
# Builds the native addon for every platform we ship and writes it to
# prebuilds/<platform>-<arch>/node.abi<N>.node, where N is the Node ABI.
# Run on an Apple Silicon Mac with Docker running. See "Prebuilt binaries" in
# README.md.
set -euo pipefail

cd "$(dirname "$0")/.."
ROOT="$PWD"

NODE_VERSION="$(tr -d '[:space:]' < .node-version)"
NODE_GYP="node-gyp@13.1.0"
# manylinux_2_28 is glibc 2.28, the same floor as Node 24's own Linux builds,
# so the Linux binaries load anywhere Node 24 runs.
MANYLINUX_IMAGE="quay.io/pypa/manylinux_2_28"

die() { echo "error: $*" >&2; exit 1; }

[[ "$(uname -s)" == Darwin && "$(uname -m)" == arm64 ]] || die "run this on an Apple Silicon Mac"
docker info >/dev/null 2>&1 || die "Docker must be running"
[[ "$(node -p 'process.versions.node.split(".")[0]')" == "${NODE_VERSION%%.*}" ]] ||
  die "host Node is $(node -v) but .node-version is $NODE_VERSION; the majors must match"
ABI="$(node -p process.versions.modules)"
export NODE_VERSION NODE_GYP ABI

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

# macOS builds must use Apple's toolchain. Dev environments such as devbox
# point DEVELOPER_DIR, SDKROOT and CC at a Nix toolchain, which links the wrong
# compiler runtime into the x64 build. So run them with only node on PATH.
mkdir "$TMP/bin"
for tool in node npm npx; do ln -s "$(command -v "$tool")" "$TMP/bin/$tool"; done
apple() { env -i HOME="$HOME" PATH="$TMP/bin:/usr/bin:/bin:/usr/sbin:/sbin" "$@"; }

# A leftover local build would be picked up by index.js instead of the prebuilt.
clean_local_build() { apple npx --yes "$NODE_GYP" clean --directory src >/dev/null; }

build_darwin() {
  local arch="$1" dest="$ROOT/prebuilds/darwin-$1"
  echo "==> darwin-$arch"
  apple npx --yes "$NODE_GYP" rebuild --directory src --arch="$arch" --target="$NODE_VERSION" >/dev/null
  mkdir -p "$dest"
  cp src/build/Release/addon.node "$dest/node.abi$ABI.node"
  strip -x "$dest/node.abi$ABI.node"
  echo "    minimum macOS: $(otool -l "$dest/node.abi$ABI.node" | awk '/minos/ {print $2; exit}')"
  clean_local_build
}

build_linux() {
  local docker_arch="$1" node_arch="$2" image_arch="$3" dest="$ROOT/prebuilds/linux-$2"
  echo "==> linux-$node_arch"
  mkdir -p "$dest"
  docker run --rm --platform "linux/$docker_arch" \
    -v "$ROOT/src:/in:ro" -v "$dest:/out" \
    -e NODE_VERSION -e NODE_GYP -e ABI -e NODE_ARCH="$node_arch" \
    "${MANYLINUX_IMAGE}_${image_arch}" bash -euo pipefail -c '
      cd /tmp
      tarball="node-v$NODE_VERSION-linux-$NODE_ARCH.tar.xz"
      curl -fsSLO "https://nodejs.org/dist/v$NODE_VERSION/$tarball"
      curl -fsSL "https://nodejs.org/dist/v$NODE_VERSION/SHASUMS256.txt" | grep " $tarball\$" | sha256sum -c - >/dev/null
      tar -xJf "$tarball" -C /opt
      export PATH="/opt/node-v$NODE_VERSION-linux-$NODE_ARCH/bin:/opt/python/cp312-cp312/bin:$PATH"
      cp -r /in /work
      cd /work
      npx --yes "$NODE_GYP" rebuild >/dev/null
      strip --strip-unneeded build/Release/addon.node
      echo "    newest glibc symbol required: $(objdump -T build/Release/addon.node | grep -o "GLIBC_[0-9.]*" | sort -uV | tail -1)"
      cp build/Release/addon.node "/out/node.abi$ABI.node"
    '
}

# Loads the package from a directory and checks the callback fires on a real
# event-loop block.
# shellcheck disable=SC2016 # JavaScript template literals, not shell expansions.
SMOKE_TEST='
const blocked = require(process.argv[1]);
blocked((ms) => { console.log(`    ${process.platform}-${process.arch}: callback fired (blocked ${ms} ms)`); process.exit(0); },
  { threshold: 200, interval: 50 });
setTimeout(() => { const end = Date.now() + 500; while (Date.now() < end) {} }, 100);
setTimeout(() => { console.error("callback did not fire"); process.exit(1); }, 3000);
'

# Loads darwin-x64 with an x64 Node under Rosetta, if Rosetta is installed.
smoke_darwin_x64() {
  if ! arch -x86_64 /usr/bin/true 2>/dev/null; then
    echo "    darwin-x64: Rosetta not installed, not loaded: $(file -b "prebuilds/darwin-x64/node.abi$ABI.node")"
    return
  fi
  local tarball="node-v$NODE_VERSION-darwin-x64.tar.gz"
  curl -fsSL -o "$TMP/$tarball" "https://nodejs.org/dist/v$NODE_VERSION/$tarball"
  (cd "$TMP" && curl -fsSL "https://nodejs.org/dist/v$NODE_VERSION/SHASUMS256.txt" | grep " $tarball\$" | shasum -a 256 -c - >/dev/null)
  tar -xzf "$TMP/$tarball" -C "$TMP"
  arch -x86_64 "$TMP/node-v$NODE_VERSION-darwin-x64/bin/node" -e "$SMOKE_TEST" "$ROOT"
}

clean_local_build
build_darwin arm64
build_darwin x64
build_linux amd64 x64 x86_64
build_linux arm64 arm64 aarch64

echo "==> smoke tests"
node -e "$SMOKE_TEST" "$ROOT"
smoke_darwin_x64
for pair in amd64:x64 arm64:arm64; do
  docker run --rm --platform "linux/${pair%%:*}" -v "$ROOT:/pkg:ro" "node:$NODE_VERSION-bookworm-slim" \
    node -e "$SMOKE_TEST" /pkg
done

echo "==> sha256 (put these in the PR description)"
shasum -a 256 prebuilds/*/"node.abi$ABI.node"
