const fastBlockedAt = require('../index.js');
const t = require('tap')
fastBlockedAt(() => {}, {threshold: 100, interval: 50});
t.throws(() => fastBlockedAt(() => {}, {threshold: 100, interval: 50}), 'twice');
