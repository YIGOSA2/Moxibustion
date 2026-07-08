const assert = require('assert')
const { chooseGattEndpoint, isStandardBluetoothUuid } = require('./ble-gatt')

assert.strictEqual(isStandardBluetoothUuid('00001800-0000-1000-8000-00805F9B34FB'), true)
assert.strictEqual(isStandardBluetoothUuid('0000FFF0-0000-1000-8000-00805F9B34FB'), true)
assert.strictEqual(isStandardBluetoothUuid('6E400001-B5A3-F393-E0A9-E50E24DCCA9E'), false)

const endpoint = chooseGattEndpoint([
  {
    service: { uuid: '00001800-0000-1000-8000-00805F9B34FB' },
    characteristics: [
      { uuid: '00002A00-0000-1000-8000-00805F9B34FB', properties: { read: true } }
    ]
  },
  {
    service: { uuid: '0000FFF0-0000-1000-8000-00805F9B34FB' },
    characteristics: [
      { uuid: '0000FFF1-0000-1000-8000-00805F9B34FB', properties: { notify: true, read: true } },
      { uuid: '0000FFF2-0000-1000-8000-00805F9B34FB', properties: { writeNoResponse: true } }
    ]
  }
])

assert.ok(endpoint)
assert.strictEqual(endpoint.service.uuid, '0000FFF0-0000-1000-8000-00805F9B34FB')
assert.strictEqual(endpoint.notifyChar.uuid, '0000FFF1-0000-1000-8000-00805F9B34FB')
assert.strictEqual(endpoint.writeChar.uuid, '0000FFF2-0000-1000-8000-00805F9B34FB')

const preferFffOverFe59 = chooseGattEndpoint([
  {
    service: { uuid: '0000FE59-0000-1000-8000-00805F9B34FB' },
    characteristics: [
      { uuid: '8EC90001-F315-4F60-9FB8-838830DAEA50', properties: { notify: true } },
      { uuid: '8EC90002-F315-4F60-9FB8-838830DAEA50', properties: { write: true } }
    ]
  },
  {
    service: { uuid: '0000FFF0-0000-1000-8000-00805F9B34FB' },
    characteristics: [
      { uuid: '0000FFF1-0000-1000-8000-00805F9B34FB', properties: { notify: true } },
      { uuid: '0000FFF2-0000-1000-8000-00805F9B34FB', properties: { writeNoResponse: true } }
    ]
  }
])

assert.ok(preferFffOverFe59)
assert.strictEqual(preferFffOverFe59.service.uuid, '0000FFF0-0000-1000-8000-00805F9B34FB')

console.log('ble-gatt tests passed')