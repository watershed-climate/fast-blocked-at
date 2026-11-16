#!/usr/bin/env bash
# Builds the addon for one Linux architecture. build-prebuilds.sh runs this
# inside a manylinux_2_28 container, with src/ mounted read-only at /in and the
# output directory mounted at /out.
set -euo pipefail

: "${NODE_VERSION:?}" "${NODE_ARCH:?}" "${NODE_GYP:?}" "${ABI:?}"

# Node's official Linux build, checked against its published checksums.
cd /tmp
tarball="node-v$NODE_VERSION-linux-$NODE_ARCH.tar.xz"
curl -fsSLO "https://nodejs.org/dist/v$NODE_VERSION/$tarball"
curl -fsSL "https://nodejs.org/dist/v$NODE_VERSION/SHASUMS256.txt" | grep " $tarball\$" | sha256sum -c - >/dev/null
tar -xJf "$tarball" -C /opt
export PATH="/opt/node-v$NODE_VERSION-linux-$NODE_ARCH/bin:/opt/python/cp312-cp312/bin:$PATH"

# Build a copy so the read-only mount isn't touched.
cp -r /in /work
cd /work
npx --yes "$NODE_GYP" rebuild >/dev/null
strip --strip-unneeded build/Release/addon.node

echo "    newest glibc symbol required: $(objdump -T build/Release/addon.node | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1)"
cp build/Release/addon.node "/out/node.abi$ABI.node"
