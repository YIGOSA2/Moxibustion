function secondsToDisplayMinutes(minutes) {
  const value = Number(minutes) || 0
  if (value <= 0) {
    return 0
  }
  return value
}

function minutesToDeviceSeconds(minutes) {
  let value = parseInt(minutes, 10)
  if (Number.isNaN(value)) {
    value = 0
  }
  value = Math.max(0, Math.min(65535, value))
  return value
}

function parseStatusPayload(payload) {
  const p = payload || []
  const rawMinutesLeft = (((p[6] || 0) << 8) | (p[7] || 0)) & 0xffff
  const nfcFwOk = p[29] || 0
  const nfcFwIc = p[30] || 0
  const nfcFwVer = p[31] || 0
  const nfcFwRev = p[32] || 0
  const bleRxCount = p.length >= 35 ? (p[33] || 0) : 0
  const bleLastRxByte = p.length >= 35 ? (p[34] || 0) : 0
  const bleFrameCount = p.length >= 38 ? (p[35] || 0) : 0
  const bleLastCmd = p.length >= 38 ? (p[36] || 0) : 0
  const bleUartErrorFlags = p.length >= 38 ? (p[37] || 0) : 0

  return {
    state: p[0] || 0,
    mode: p[1] || 0,
    tempNow: p[2] || 0,
    tempSet: p[3] || 0,
    pressurePresent: p[4] || 0,
    pressureAdc: 0,
    bleConnected: p[5] || 0,
    rawSecondsLeft: rawMinutesLeft,
    displayMinutesLeft: secondsToDisplayMinutes(rawMinutesLeft),
    faultCode: p[8] || 0,
    bleRxCount,
    bleLastRxByte,
    bleLastRxByteHex: bleLastRxByte.toString(16).padStart(2, '0').toUpperCase(),
    bleFrameCount,
    bleLastCmd,
    bleLastCmdHex: bleLastCmd.toString(16).padStart(2, '0').toUpperCase(),
    bleUartErrorFlags,
    bleUartErrorFlagsHex: bleUartErrorFlags.toString(16).padStart(2, '0').toUpperCase(),
    nfcFwText: nfcFwOk ? `IC=${nfcFwIc} VER=${nfcFwVer}.${nfcFwRev}` : 'Unknown'
  }
}

module.exports = {
  secondsToDisplayMinutes,
  minutesToDeviceSeconds,
  parseStatusPayload
}