'use strict'

const STORAGE_KEY = 'moxibustion_session_history_v1'
const MAX_RECORDS = 3

function isHeatingState(state) {
  return state === 1 || state === 2 || state === 3
}

function normalizeRecord(record) {
  const startEpoch = Number(record && record.startEpoch) || 0
  const endEpoch = Number(record && record.endEpoch) || startEpoch
  const durationMin = Math.max(0, Number(record && record.durationMin) || 0)

  return { startEpoch, endEpoch, durationMin }
}

function mergeNewest(records, record, limit = MAX_RECORDS) {
  const next = (Array.isArray(records) ? records : []).map(normalizeRecord)
  next.push(normalizeRecord(record))
  return next.slice(-limit)
}

function makeRecord(startEpoch, endEpoch) {
  const safeStart = Number(startEpoch) || 0
  const safeEnd = Math.max(safeStart, Number(endEpoch) || safeStart)
  const durationMin = Math.max(0, Math.ceil((safeEnd - safeStart) / 60))
  return {
    startEpoch: safeStart,
    endEpoch: safeEnd,
    durationMin
  }
}

function beginManualSession(prevTracker, epoch) {
  const tracker = Object.assign({ active: false, startEpoch: 0 }, prevTracker || {})
  if (!tracker.active) {
    tracker.active = true
    tracker.startEpoch = Number(epoch) || 0
  }
  return tracker
}

function finishManualSession(prevTracker, epoch) {
  const tracker = Object.assign({ active: false, startEpoch: 0 }, prevTracker || {})
  let completedRecord = null
  if (tracker.active) {
    completedRecord = makeRecord(tracker.startEpoch || Number(epoch) || 0, epoch)
    tracker.active = false
    tracker.startEpoch = 0
  }
  return { tracker, completedRecord }
}

function updateTracker(prevTracker, prevState, nextState, epoch) {
  const tracker = Object.assign({ active: false, startEpoch: 0 }, prevTracker || {})
  const wasHeating = isHeatingState(prevState)
  const isHeating = isHeatingState(nextState)
  const nowEpoch = Number(epoch) || 0
  let completedRecord = null

  if (!wasHeating && isHeating) {
    if (!tracker.active) {
      tracker.active = true
      tracker.startEpoch = nowEpoch
    }
  } else if (wasHeating && !isHeating && tracker.active) {
    completedRecord = makeRecord(tracker.startEpoch || nowEpoch, nowEpoch)
    tracker.active = false
    tracker.startEpoch = 0
  }

  return { tracker, completedRecord }
}

function loadHistory(storage) {
  if (!storage || typeof storage.getStorageSync !== 'function') {
    return []
  }

  const records = storage.getStorageSync(STORAGE_KEY)
  return Array.isArray(records) ? records.map(normalizeRecord) : []
}

function saveHistory(storage, records) {
  if (!storage || typeof storage.setStorageSync !== 'function') {
    return
  }

  storage.setStorageSync(STORAGE_KEY, (Array.isArray(records) ? records : []).map(normalizeRecord).slice(-MAX_RECORDS))
}

module.exports = {
  STORAGE_KEY,
  MAX_RECORDS,
  isHeatingState,
  normalizeRecord,
  mergeNewest,
  makeRecord,
  beginManualSession,
  finishManualSession,
  updateTracker,
  loadHistory,
  saveHistory
}
