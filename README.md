# fast-blocked-at

Detect event loop blockages and get a stack trace; fast enough for production.

## Installation

```
npm install fast-blocked-at
```
(Tested with NodeJS 14, 16 & 18.)

## Usage

```javascript
const blocked = require('fast-blocked-at');
blocked((durationMs, stack) => {
    console.log(`Blocked for ${durationMs}ms:\n${stack}`);
}, {
    // Frequency with which the event loop is checked in ms
    // (Report event loop blockages which have taken longer than this)
    threshold: 200 /* milliseconds */,
    // How often to "heartbeat" to the "watchdog" (see below)
    // Lower values use more resources but makes it more accurate
    interval: 50 /* milliseconds */,
})
```

## Description

This module sets a timer which runs every `interval` milliseconds on the
event loop. A separate "watchdog" thread is started which polls every
`threshold` milliseconds. If the event loop has been blocked for longer
than `threshold` milliseconds (i.e., since the last watchdog poll), then
a stack trace is captured and the callback will be invoked when the
event loop is unblocked.

This differs from [blocked-at][ba] in two important ways:

1. It is low-overhead and so suitable for production use.
2. It captures the stack trace somewhere between `threshold` and `2 *
   threshold` milliseconds after the start of the event loop cycle,
   unlike blocked-at which tries to get the stack trace at the "start"
   of the blockage.

While we currently just capture a stack trace, future versions of this package
may include more advanced functionality like automatically starting the CPU
profiler.

[ba]: https://github.com/naugtur/blocked-at

## Formal Verification

This module is formally verified to be deadlock-free via PlusCal/TLA+. Refer to
`verify_tla.sh` for steps on how to create the TLA+ specification and verify it.

The formal verification is checked on every CI run and changes to the C++ code
should be matched with changes to the PlusCal code.

## Contributing

Contributions are welcome! This project is hosted on sourcehut.

* [Project homepage](https://sr.ht/~kvakil/fast-blocked-at/)
* [Source code](https://git.sr.ht/~kvakil/fast-blocked-at)
* [Mailing list for bug reports, feature requests, help, et cetera](https://lists.sr.ht/~kvakil/fast-blocked-at-discuss)
* CI badge: [![builds.sr.ht status](https://builds.sr.ht/~kvakil/fast-blocked-at.svg)](https://builds.sr.ht/~kvakil/fast-blocked-at?)
