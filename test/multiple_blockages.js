const fastBlockedAt = require('../index.js');
const t = require('tap')
const yield = () => new Promise((r) => setTimeout(r, 0));

function blockSync(blockMs) {
    const start = Date.now();
    while (Date.now() - start < blockMs);
}

t.test('multiple blockages', async t => {
    t.plan(6);
    fastBlockedAt((durationMs, stack) => {
        console.log(durationMs);
        t.ok(durationMs >= 200);
        t.match(stack, /blockSync/);
    }, {
        threshold: 100,
        interval: 10,
    });
    blockSync(300);
    await yield();
    blockSync(300);
    await yield();
    blockSync(300);
    await yield();
});
