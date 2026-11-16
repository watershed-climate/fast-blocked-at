// Loads fast-blocked-at from the directory given as the first argument and
// checks that its callback fires on a real event-loop block.
// Usage: node scripts/smoke-test.js <package directory>
const blocked = require(process.argv[2]);
const target = `${process.platform}-${process.arch}`;

blocked((ms) => {
    console.log(`    ${target}: callback fired (blocked ${ms} ms)`);
    process.exit(0);
}, { threshold: 200, interval: 50 });

setTimeout(() => {
    const end = Date.now() + 500;
    while (Date.now() < end) {}
}, 100);

setTimeout(() => {
    console.error(`    ${target}: callback did not fire`);
    process.exit(1);
}, 3000);
