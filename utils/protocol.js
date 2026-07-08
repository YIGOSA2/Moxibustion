const CMD = {
  SET_TEMP: 0x01,
  SET_MODE: 0x02,
  SET_TIME: 0x03,
  START: 0x04,
  PAUSE: 0x05,
  STOP: 0x06,
  GET_STATUS: 0x07,
  CLEAR_FAULT: 0x08,
  ENROLL_NFC_USER: 0x09,
  SET_CLOCK: 0x0a,
  GET_SESSION_COUNT: 0x0b,
  GET_SESSION: 0x0c,
  CLEAR_SESSIONS: 0x0d,
  ACK: 0x81,
  STATUS: 0x90,
  FAULT: 0x91,
  SESSION_COUNT: 0x92,
  SESSION: 0x93
}

const ACK_TEXT = {
  0: 'OK',
  1: 'INVALID_PARAM',
  2: 'INVALID_STATE',
  3: 'BUSY',
  4: 'NOT_ALLOWED',
  5: 'INTERNAL_ERROR'
}

function xorChecksum(bytes) {
  return bytes.reduce((acc, b) => (acc ^ (b & 0xff)) & 0xff, 0)
}

function buildFrame(cmd, payload = []) {
  const bodyLen = 2 + payload.length
  const head = [0xaa, 0x55, bodyLen & 0xff, cmd & 0xff, ...payload.map((b) => b & 0xff)]
  const chk = xorChecksum(head)
  return new Uint8Array([...head, chk]).buffer
}

function parseFrameBytes(bytes) {
  if (!bytes || bytes.length < 5) return null
  if (bytes[0] !== 0xaa || bytes[1] !== 0x55) return null

  const len = bytes[2]
  if (len < 2) return null
  if (bytes.length !== len + 3) return null

  const chk = bytes[bytes.length - 1]
  const expected = xorChecksum(Array.from(bytes.slice(0, bytes.length - 1)))
  if (chk !== expected) return null

  const cmd = bytes[3]
  const payload = Array.from(bytes.slice(4, bytes.length - 1))

  return { cmd, payload, type: cmd }
}

function parseFrame(buffer) {
  return parseFrameBytes(new Uint8Array(buffer))
}

function parseFrames(buffer) {
  const bytes = new Uint8Array(buffer)
  const frames = []
  let offset = 0

  while (offset < bytes.length) {
    if ((offset + 1) >= bytes.length) break

    if (bytes[offset] !== 0xaa || bytes[offset + 1] !== 0x55) {
      offset += 1
      continue
    }

    if ((offset + 2) >= bytes.length) break

    const frameLen = bytes[offset + 2] + 3
    if (frameLen < 5) {
      offset += 1
      continue
    }

    if ((offset + frameLen) > bytes.length) break

    const frame = parseFrameBytes(bytes.slice(offset, offset + frameLen))
    if (frame) {
      frames.push(frame)
      offset += frameLen
      continue
    }

    offset += 1
  }

  return frames
}

module.exports = {
  CMD,
  ACK_TEXT,
  buildFrame,
  parseFrame,
  parseFrames
}
