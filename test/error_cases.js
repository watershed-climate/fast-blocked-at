const fastBlockedAt = require('../index.js');
const t = require('tap')

const never = () => t.fail();
function mustFail(options, whichFails) {
    t.throws(() => fastBlockedAt(never, options), whichFails);
}
mustFail({interval: 100, threshold: 0}, 'threshold');
mustFail({interval: 0, threshold: 100}, 'interval');
mustFail({interval: 'foo', threshold: 100}, 'interval');
mustFail({}, 'interval');
mustFail({interval: 100, threshold: 'foo'}, 'threshold');
const tooLarge = Number.MAX_SAFE_INTEGER * 2;
mustFail({interval: tooLarge, threshold: 100}, 'interval');
mustFail({interval: 100, threshold: tooLarge}, 'threshold');

t.throws(() => fastBlockedAt('foo', {interval: 100, threshold: 200}), 'callback');
