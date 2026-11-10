'use strict';

const { createHash } = require('node:crypto');

// Never pass whole binary resources to assert.deepEqual: formatting its diff
// can allocate gigabytes for two differing, otherwise small HTML buffers.
function assertSameBytes(actual, expected, description) {
  if (!Buffer.isBuffer(actual) || !Buffer.isBuffer(expected)) {
    throw new TypeError('Binary contract comparison requires Buffer inputs');
  }
  if (actual.equals(expected)) return;
  let offset = 0;
  const common = Math.min(actual.length, expected.length);
  while (offset < common && actual[offset] === expected[offset]) offset += 1;
  const digest = value => createHash('sha256').update(value).digest('hex');
  throw new Error(`${description}: binary mismatch; first_difference=${offset}; ` +
    `actual_bytes=${actual.length}; expected_bytes=${expected.length}; ` +
    `actual_sha256=${digest(actual)}; expected_sha256=${digest(expected)}`);
}

module.exports = { assertSameBytes };
