const fastBlockedAt = require('../index.js');
const t = require('tap')
const yield = () => new Promise((r) => setTimeout(r, 0));

function blockSync(blockMs) {
    const start = Date.now();
    while (Date.now() - start < blockMs);
}

t.test('basic', async t => {
    t.plan(2);
    fastBlockedAt((durationMs, stack) => {
        t.ok(durationMs >= 200);
        t.match(stack, /blockSync/);
    }, {
        threshold: 100,
        interval: 50,
    });
    blockSync(300);
    // Allow the heartbeat invoke the callback.
    await yield();
});
