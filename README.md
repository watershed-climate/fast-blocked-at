# fast-blocked-at

Detect event loop blockages and get a stack trace; fast enough for production.

## Installation

Install a tagged release from GitHub, for example with pnpm:

```
pnpm add github:watershed-climate/fast-blocked-at#v0.1.0
```

Installing never compiles anything: the package ships prebuilt binaries (see [Prebuilt binaries](#prebuilt-binaries)). It currently supports Node 24.

## Usage

```javascript
const blocked = require('fast-blocked-at');
blocked(
  (durationMs, stack) => {
    console.log(`Blocked for ${durationMs}ms:\n${stack}`);
  },
  {
    // Frequency with which the event loop is checked in ms
    // (Report event loop blockages which have taken longer than this)
    threshold: 200 /* milliseconds */,
    // How often to "heartbeat" to the "watchdog" (see below)
    // Lower values use more resources but makes it more accurate
    interval: 50 /* milliseconds */,
  }
);
```

## Description

This module sets a timer which runs every `interval` milliseconds on the event loop. A separate "watchdog" thread is started which polls every `threshold` milliseconds. If the event loop has been blocked for longer than `threshold` milliseconds (i.e., since the last watchdog poll), then a stack trace is captured and the callback will be invoked when the event loop is unblocked.

This differs from [blocked-at][ba] in two important ways:

1. It is low-overhead and so suitable for production use.
2. It captures the stack trace somewhere between `threshold` and `2 * threshold` milliseconds after the start of the event loop cycle, unlike blocked-at which tries to get the stack trace at the "start" of the blockage.

While we currently just capture a stack trace, future versions of this package may include more advanced functionality like automatically starting the CPU profiler.

[ba]: https://github.com/naugtur/blocked-at

## Prebuilt binaries

The native addon lives in `src/`. Instead of compiling it during install, the package ships prebuilt binaries in `prebuilds/<platform>-<arch>/node.abi<N>.node`, where `N` is the Node ABI version (`process.versions.modules`; 137 for Node 24). `index.js` loads the file matching the running platform and Node, so installing this package runs no install script and needs no compiler.

Shipped platforms: `darwin-arm64`, `darwin-x64`, `linux-x64`, `linux-arm64`. The Linux binaries are built in `manylinux_2_28` containers and need glibc 2.28 or newer, the same as Node 24 itself (Debian 10+, Ubuntu 20.04+, RHEL 8+). There are no builds for musl (Alpine) or Windows; on those, or on a Node version with no matching binary, `require('fast-blocked-at')` throws an error naming the missing platform and ABI.

### When to rebuild

- When moving to a new Node major version. Binaries for one major work on any release of that major, so minor and patch upgrades don't need a rebuild.
- When `src/native.cc` or `src/binding.gyp` changes.

### How to rebuild

On an Apple Silicon Mac with Docker running, and with the Node major from `.node-version` installed:

1. Update `.node-version` if you're moving to a new Node major.
2. Run `npm run build:prebuilds`. It builds the macOS binaries locally with Apple's toolchain and the Linux binaries in `manylinux_2_28` containers, smoke-tests each one, and prints their SHA-256 checksums.
3. Run `npm test`.
4. Bump `version` in `package.json`, commit it together with the new files in `prebuilds/` on a branch, and open a PR. Never push to `main`. Put the checksums and the commit you built from in the PR description. When moving to a new Node major, keep the previous major's files until every consumer has upgraded.
5. After the PR merges, tag the merged commit with the new version, e.g. `v0.2.0`.
6. Update the tag in the consumer, e.g. `fast-blocked-at` in ghg's `pnpm-workspace.yaml`.

### Developing the addon

`npm run build:local` compiles `src/` into `src/build/`. `index.js` prefers that local build over the prebuilt binaries, so tests run against your changes. Delete `src/build/` (or run `npx node-gyp clean --directory src`) to go back to the prebuilt binaries.

## Contributing

We're not accepting contributions at this time.
