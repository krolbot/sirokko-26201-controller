'use strict';
const assert = require('node:assert/strict');
const { assertSameBytes } = require('./binary_contract.cjs');

const original = Buffer.alloc(512 * 1024, 0x61);
assertSameBytes(original, Buffer.from(original), 'equal');
assertSameBytes(Buffer.alloc(0), Buffer.alloc(0), 'empty');
for (const offset of [0, original.length / 2, original.length - 1]) {
  const changed = Buffer.from(original);
  changed[offset] = 0x62;
  assert.throws(() => assertSameBytes(changed, original, 'mutation'), error => {
    assert(error.message.includes(`first_difference=${offset};`));
    assert(error.message.includes('actual_sha256='));
    assert(error.message.length < 400);
    assert.equal(error.actual, undefined, 'do not retain binary payload on errors');
    return true;
  });
}
assert.throws(() => assertSameBytes(original.subarray(0, -1), original, 'length'),
  /binary mismatch/);
assert.throws(() => assertSameBytes('bad', original, 'type'), TypeError);
for (let iteration = 0; iteration < 100; iteration += 1) {
  assert.throws(() => assertSameBytes(original, Buffer.alloc(original.length, 0x62), 'repeat'),
    /first_difference=0;/);
}
console.log('PASS binary contract: equality, length, mutations, bounded errors, 100 repeated failures');
