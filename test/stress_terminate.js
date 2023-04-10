const t = require('tap');
const {Worker} = require('worker_threads');
const script = `
const fastBlockedAt = require('${__dirname}/../index.js');
fastBlockedAt(() => {}, {
    threshold: 1 + (Math.random() * 100) | 0,
    interval: 1 + (Math.random() * 100) | 0,
});
function blockSync(blockMs) {
    const start = Date.now();
    while (Date.now() - start < blockMs);
}
blockSync(Math.random() * 200);
if (Math.random() < 0.5) setTimeout(() => {}, Math.random() * 100);
if (Math.random() < 0.5) setTimeout(() => {}, Math.random() * 100);
`;
t.test('stress termination', async t => {
    for (let i = 0; i < 10; i++) {
        const parallelWorkers = 4;
        let done = 0;
        await new Promise((resolve) => {
            for (let j = 0; j < parallelWorkers; j++) {
                const w = new Worker(script, {eval: true});
                setTimeout(() => {
                    w.terminate();
                    done++;
                    if (done === parallelWorkers) {
                        resolve();
                    }
                }, Math.random() * 300);
            }
        });
        t.ok(true);
    }
    t.end();
});
