const assert = require('assert')
const {
  secondsToDisplayMinutes,
  minutesToDeviceSeconds,
  parseStatusPayload
} = require('./control-helpers')

assert.strictEqual(secondsToDisplayMinutes(0), 0)
assert.strictEqual(secondsToDisplayMinutes(1), 1)
assert.strictEqual(secondsToDisplayMinutes(60), 60)
assert.strictEqual(secondsToDisplayMinutes(61), 61)

assert.strictEqual(minutesToDeviceSeconds('0'), 0)
assert.strictEqual(minutesToDeviceSeconds('5'), 5)
assert.strictEqual(minutesToDeviceSeconds('65535'), 65535)
assert.strictEqual(minutesToDeviceSeconds('70000'), 65535)
assert.strictEqual(minutesToDeviceSeconds('abc'), 0)

const parsed = parseStatusPayload([
  1, 2, 48, 52, 1, 1, 0x00, 0x3d, 7,
  0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  1, 2, 3, 4
])

assert.strictEqual(parsed.state, 1)
assert.strictEqual(parsed.mode, 2)
assert.strictEqual(parsed.tempNow, 48)
assert.strictEqual(parsed.tempSet, 52)
assert.strictEqual(parsed.pressurePresent, 1)
assert.strictEqual(parsed.pressureAdc, 0)
assert.strictEqual(parsed.bleConnected, 1)
assert.strictEqual(parsed.rawSecondsLeft, 61)
assert.strictEqual(parsed.displayMinutesLeft, 61)
assert.strictEqual(parsed.faultCode, 7)
assert.strictEqual(parsed.nfcFwText, 'IC=2 VER=3.4')

const parsedExtended = parseStatusPayload([
  1, 2, 48, 52, 1, 1, 0x00, 0x3d, 7,
  0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  1, 2, 3, 4, 9, 0xaa, 6, 0x04, 0x08
])

assert.strictEqual(parsedExtended.bleRxCount, 9)
assert.strictEqual(parsedExtended.bleLastRxByte, 0xaa)
assert.strictEqual(parsedExtended.bleLastRxByteHex, 'AA')
assert.strictEqual(parsedExtended.bleFrameCount, 6)
assert.strictEqual(parsedExtended.bleLastCmd, 0x04)
assert.strictEqual(parsedExtended.bleLastCmdHex, '04')
assert.strictEqual(parsedExtended.bleUartErrorFlags, 0x08)
assert.strictEqual(parsedExtended.bleUartErrorFlagsHex, '08')

console.log('control-helpers tests passed')