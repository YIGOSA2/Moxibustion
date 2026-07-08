'use strict'

const assert = require('assert')
const {
  MAX_RECORDS,
  mergeNewest,
  makeRecord,
  updateTracker,
  loadHistory,
  saveHistory,
  beginManualSession,
  finishManualSession
} = require('./session-history')

assert.deepStrictEqual(
  makeRecord(1000, 1121),
  { startEpoch: 1000, endEpoch: 1121, durationMin: 3 }
)

let state = updateTracker(null, 0, 1, 2000)
assert.strictEqual(state.tracker.active, true)
assert.strictEqual(state.tracker.startEpoch, 2000)
assert.strictEqual(state.completedRecord, null)

state = updateTracker(state.tracker, 1, 0, 2121)
assert.deepStrictEqual(
  state.completedRecord,
  { startEpoch: 2000, endEpoch: 2121, durationMin: 3 }
)
assert.strictEqual(state.tracker.active, false)

const merged = [
  { startEpoch: 1, endEpoch: 61, durationMin: 1 },
  { startEpoch: 2, endEpoch: 62, durationMin: 1 },
  { startEpoch: 3, endEpoch: 63, durationMin: 1 }
]
const mergedNext = mergeNewest(merged, { startEpoch: 4, endEpoch: 125, durationMin: 3 })
assert.strictEqual(mergedNext.length, MAX_RECORDS)
assert.strictEqual(mergedNext[0].startEpoch, 2)
assert.strictEqual(mergedNext[2].startEpoch, 4)

const fakeStorage = {
  data: {},
  getStorageSync(key) {
    return this.data[key]
  },
  setStorageSync(key, value) {
    this.data[key] = value
  }
}

saveHistory(fakeStorage, mergedNext)
assert.deepStrictEqual(loadHistory(fakeStorage), mergedNext)

console.log('session-history tests passed')


let tracker = beginManualSession(null, 3000)
assert.strictEqual(tracker.active, true)
assert.strictEqual(tracker.startEpoch, 3000)

let manual = finishManualSession(tracker, 3121)
assert.deepStrictEqual(manual.completedRecord, { startEpoch: 3000, endEpoch: 3121, durationMin: 3 })
assert.strictEqual(manual.tracker.active, false)

tracker = beginManualSession(null, 3000)
state = updateTracker(tracker, 0, 1, 3060)
assert.strictEqual(state.tracker.active, true)
assert.strictEqual(state.tracker.startEpoch, 3000)
assert.strictEqual(state.completedRecord, null)