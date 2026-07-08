const SERVICE_SHORT = 'FFF0'
const WRITE_HINT = 'FFF2'
const NOTIFY_HINT = 'FFF1'
const CUSTOM_SERVICE_HINTS = ['0000FFF0', 'FFF0']

function upper(uuid) {
  return (uuid || '').toUpperCase()
}

function hasShort(uuid, shortId) {
  return upper(uuid).includes(shortId)
}

function isStandardBluetoothUuid(uuid) {
  return /^0000[0-9A-F]{4}-0000-1000-8000-00805F9B34FB$/i.test(uuid || '')
}

function isWritable(c) {
  return !!(c && c.properties && (c.properties.write || c.properties.writeNoResponse))
}

function isNotifiable(c) {
  return !!(c && c.properties && (c.properties.notify || c.properties.indicate))
}

function pickCharacteristic(list, predicate, hint) {
  const candidates = (list || []).filter(predicate)
  return candidates.find((c) => hasShort(c.uuid, hint)) || candidates[0] || null
}

function scoreService(service, writeChar, notifyChar) {
  const serviceUuid = upper(service.uuid)
  const writeUuid = upper(writeChar.uuid)
  const notifyUuid = upper(notifyChar.uuid)
  let score = 0

  if (CUSTOM_SERVICE_HINTS.some((hint) => hasShort(serviceUuid, hint))) score += 300
  if (hasShort(serviceUuid, SERVICE_SHORT)) score += 200
  if (hasShort(writeUuid, WRITE_HINT)) score += 120
  if (hasShort(notifyUuid, NOTIFY_HINT)) score += 120
  if (hasShort(serviceUuid, 'FE59')) score -= 120
  if (!isStandardBluetoothUuid(serviceUuid)) score += 20
  if (!isStandardBluetoothUuid(writeUuid)) score += 10
  if (!isStandardBluetoothUuid(notifyUuid)) score += 10
  if (writeChar.properties && writeChar.properties.writeNoResponse) score += 6
  if (notifyChar.properties && notifyChar.properties.notify) score += 6

  return score
}

function chooseGattEndpoint(servicesWithCharacteristics) {
  const list = servicesWithCharacteristics || []
  const scored = list
    .map((item) => {
      const service = item.service || {}
      const characteristics = item.characteristics || []
      const writeChar = pickCharacteristic(characteristics, isWritable, WRITE_HINT)
      const notifyChar = pickCharacteristic(characteristics, isNotifiable, NOTIFY_HINT)
      if (!writeChar || !notifyChar) {
        return null
      }
      return {
        service,
        characteristics,
        writeChar,
        notifyChar,
        score: scoreService(service, writeChar, notifyChar)
      }
    })
    .filter(Boolean)
    .sort((a, b) => b.score - a.score)

  return scored[0] || null
}

module.exports = {
  hasShort,
  isStandardBluetoothUuid,
  isWritable,
  isNotifiable,
  pickCharacteristic,
  chooseGattEndpoint,
  SERVICE_SHORT,
  WRITE_HINT,
  NOTIFY_HINT,
  CUSTOM_SERVICE_HINTS
}