/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
const fs = require('fs');
const path = require('path');

// A local build (`npm run build:local`) wins so the addon can be developed in
// this repo. Installs never build, so they load the prebuilt binary for this
// platform and Node ABI; see "Prebuilt binaries" in README.md.
function loadAddon() {
  const target = `${process.platform}-${process.arch}`;
  const abi = process.versions.modules;
  const candidates = [
    path.join(__dirname, 'src', 'build', 'Release', 'addon.node'),
    path.join(__dirname, 'prebuilds', target, `node.abi${abi}.node`),
  ];
  for (const candidate of candidates) {
    if (fs.existsSync(candidate)) {
      return require(candidate);
    }
  }
  throw new Error(
    `fast-blocked-at has no prebuilt binary for ${target} on Node ABI ${abi} (Node ${process.version}). ` +
      'See "Prebuilt binaries" in its README.md.'
  );
}

const addon = loadAddon();
module.exports = function (callback, options) {
  const { interval, threshold } = options;
  if (typeof callback !== 'function') {
    throw new TypeError('callback must be a function');
  }
  if (
    typeof interval !== 'number' ||
    interval <= 0 ||
    interval > Number.MAX_SAFE_INTEGER
  ) {
    throw new Error('invalid value for interval');
  }
  if (
    typeof threshold !== 'number' ||
    threshold <= 0 ||
    threshold > Number.MAX_SAFE_INTEGER
  ) {
    throw new Error('invalid value for threshold');
  }
  if (!addon.startWatchdog(callback, interval, threshold)) {
    throw new Error('attempted to start addon twice');
  }
  setInterval(addon.heartbeat, interval).unref();
};
