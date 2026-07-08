const { buildFrame, parseFrames, CMD, ACK_TEXT } = require('../../utils/protocol')
const { minutesToDeviceSeconds, parseStatusPayload } = require('../../utils/control-helpers')
const { updateTracker, loadHistory, saveHistory, mergeNewest, beginManualSession, finishManualSession } = require('../../utils/session-history')
const { chooseGattEndpoint, isNotifiable, isWritable } = require('../../utils/ble-gatt')

const STATUS_POLL_MS = 1000
const TEMP_MIN = 20
const TEMP_MAX = 50

function stateTextOf(v) {
  const map = {
    0: '空闲',
    1: '预热',
    2: '恒温',
    3: '暂停',
    4: '完成',
    5: '故障'
  }
  return map[v] || '未知'
}

function connectionTextOf(connected) {
  return connected ? '蓝牙已连接' : '等待蓝牙连接'
}

function cmdNameOf(cmd) {
  const map = {
    [CMD.SET_TEMP]: 'SET_TEMP',
    [CMD.SET_MODE]: 'SET_MODE',
    [CMD.SET_TIME]: 'SET_TIME',
    [CMD.START]: 'START',
    [CMD.STOP]: 'STOP',
    [CMD.GET_STATUS]: 'GET_STATUS',
    [CMD.CLEAR_FAULT]: 'CLEAR_FAULT',
    [CMD.ENROLL_NFC_USER]: 'ENROLL_NFC_USER',
    [CMD.SET_CLOCK]: 'SET_CLOCK',
    [CMD.GET_SESSION_COUNT]: 'GET_SESSION_COUNT',
    [CMD.GET_SESSION]: 'GET_SESSION'
  }
  return map[cmd] || `0x${(cmd || 0).toString(16)}`
}

function toHex(bufferOrArray) {
  const bytes = bufferOrArray instanceof ArrayBuffer ? new Uint8Array(bufferOrArray) : bufferOrArray
  return Array.from(bytes || [])
    .map((b) => b.toString(16).padStart(2, '0').toUpperCase())
    .join(' ')
}

Page({
  data: {
    deviceId: '',
    serviceId: '',
    writeCharId: '',
    notifyCharId: '',
    connected: false,
    connectionText: '等待蓝牙连接',
    state: 0,
    stateText: '未知',
    tempNow: 0,
    tempSet: 20,
    minutesLeft: 30,
    minutesInput: '30',
    faultCode: 0,
    ackText: ''
  },

  onLoad(options) {
    const deviceId = decodeURIComponent(options.deviceId || '')
    const app = getApp()
    if (app && app.globalData) {
      app.globalData.deviceId = deviceId
    }

    this.notifyTargets = []
    this.writeTargets = []
    this.tempDirty = false
    this.timeDirty = false
    this.sessionTracker = { active: false, startEpoch: 0 }
    this.lastDeviceState = 0
    this.setData({ deviceId })

    this.initialEpoch = Math.floor(Date.now() / 1000)

    wx.onBLEConnectionStateChange((res) => {
      if (res.deviceId === this.data.deviceId && !res.connected) {
        this.setData({
          connected: false,
          connectionText: connectionTextOf(false),
          ackText: '蓝牙已断开'
        })
        this.stopStatusPolling()
      }
    })

    this.initGatt()
  },

  onShow() {
    this.bindNotify()
    if (this.data.connected) {
      this.startStatusPolling()
      this.getStatus()
    }
  },

  onHide() {
    this.stopStatusPolling()
  },

  onUnload() {
    const deviceId = this.data.deviceId
    this.stopStatusPolling()
    if (this.statusRefreshTimer) {
      clearTimeout(this.statusRefreshTimer)
      this.statusRefreshTimer = null
    }
    wx.offBLECharacteristicValueChange()
    wx.offBLEConnectionStateChange()
    if (!deviceId) return
    wx.closeBLEConnection({ deviceId })
  },

  initGatt() {
    const deviceId = this.data.deviceId
    if (!deviceId) return

    wx.getBLEDeviceServices({
      deviceId,
      success: (sres) => {
        const services = sres.services || []
        if (!services.length) {
          this.setData({ ackText: '未找到蓝牙服务' })
          return
        }

        const pending = services.map((service) => new Promise((resolve) => {
          wx.getBLEDeviceCharacteristics({
            deviceId,
            serviceId: service.uuid,
            success: (cres) => resolve({ service, characteristics: cres.characteristics || [] }),
            fail: () => resolve({ service, characteristics: [] })
          })
        }))

        Promise.all(pending).then((serviceEntries) => {
          const endpoint = chooseGattEndpoint(serviceEntries)
          if (!endpoint) {
            const serviceText = serviceEntries.map((item) => item.service.uuid).join(' | ')
            this.setData({ ackText: '未找到可写/可通知特征: ' + serviceText })
            return
          }

          const notifyTargets = []
          const selectedWriteTargets = []
          serviceEntries.forEach((item) => {
            ;(item.characteristics || []).forEach((ch) => {
              if (isNotifiable(ch)) {
                notifyTargets.push({ serviceId: item.service.uuid, characteristicId: ch.uuid })
              }
              if (isWritable(ch) && item.service.uuid === endpoint.service.uuid) {
                selectedWriteTargets.push({ serviceId: item.service.uuid, characteristicId: ch.uuid })
              }
            })
          })

          this.notifyTargets = notifyTargets
          this.writeTargets = selectedWriteTargets

          this.setData({
            serviceId: endpoint.service.uuid,
            writeCharId: endpoint.writeChar.uuid,
            notifyCharId: endpoint.notifyChar.uuid,
            connected: true,
            connectionText: connectionTextOf(true),
            ackText: `GATT ready write=${selectedWriteTargets.length} notify=${notifyTargets.length}`
          })

          this.bindNotify()
          this.enableAllNotify(deviceId, notifyTargets, 0)
        })
      },
      fail: (err) => {
        this.setData({ ackText: '获取服务失败: ' + (err.errMsg || '') })
      }
    })
  },

  syncClock() {
    const epoch = this.initialEpoch || Math.floor(Date.now() / 1000)
    const payload = [
      (epoch >> 24) & 0xff,
      (epoch >> 16) & 0xff,
      (epoch >> 8) & 0xff,
      epoch & 0xff
    ]
    this.writeCmd(CMD.SET_CLOCK, payload)
  },

  enableAllNotify(deviceId, targets, index) {
    if (!targets || index >= targets.length) {
      this.syncClock()
      setTimeout(() => {
        this.startStatusPolling()
        this.getStatus()
      }, 200)
      return
    }

    const target = targets[index]
    wx.notifyBLECharacteristicValueChange({
      deviceId,
      serviceId: target.serviceId,
      characteristicId: target.characteristicId,
      state: true,
      success: () => this.enableAllNotify(deviceId, targets, index + 1),
      fail: () => this.enableAllNotify(deviceId, targets, index + 1)
    })
  },

  bindNotify() {
    wx.offBLECharacteristicValueChange()
    wx.onBLECharacteristicValueChange((res) => {
      const rawHex = toHex(res.value)
      const frames = parseFrames(res.value)
      if (!frames.length) {
        this.setData({ ackText: `Non-protocol notify ${res.characteristicId} data=${rawHex}` })
        return
      }

      frames.forEach((frame) => {
        if (frame.cmd === CMD.ACK) {
          const reqCmd = frame.payload[0] || 0
          const result = frame.payload[1] || 0
          if (reqCmd === CMD.SET_TEMP && result === 0) this.tempDirty = false
          if (reqCmd === CMD.SET_TIME && result === 0) this.timeDirty = false
          if ([CMD.SET_TEMP, CMD.SET_TIME, CMD.START, CMD.STOP].includes(reqCmd)) {
            this.queueStatusRefresh()
          }
          this.setData({ ackText: `ACK ${cmdNameOf(reqCmd)} result=${ACK_TEXT[result] || result}` })
          return
        }

        if (frame.cmd === CMD.STATUS) {
          const status = parseStatusPayload(frame.payload)
          this.handleSessionHistory(status)
          if (status.state !== 0) {
            this.tempDirty = false
            this.timeDirty = false
          }
          const nextData = {
            state: status.state,
            stateText: stateTextOf(status.state),
            tempNow: status.tempNow,
            faultCode: status.faultCode,
            connected: true,
            connectionText: connectionTextOf(true)
          }
          if (!this.tempDirty) nextData.tempSet = status.tempSet
          if (!this.timeDirty) {
            nextData.minutesLeft = status.displayMinutesLeft
            nextData.minutesInput = String(status.displayMinutesLeft)
          }
          this.setData(nextData)
          return
        }

        if (frame.cmd === CMD.FAULT) {
          const faultCode = frame.payload[0] || 0
          this.setData({ faultCode, ackText: `FAULT code=${faultCode}` })
          return
        }

        if (frame.cmd === CMD.SESSION_COUNT) {
          const count = frame.payload[0] || 0
          this.setData({ ackText: `Session count: ${count}` })
          return
        }

        if (frame.cmd === CMD.SESSION) {
          const index = frame.payload[0] || 0
          const total = frame.payload[1] || 0
          const startEpoch = ((frame.payload[2] || 0) << 24) | ((frame.payload[3] || 0) << 16) | ((frame.payload[4] || 0) << 8) | (frame.payload[5] || 0)
          const endEpoch = ((frame.payload[6] || 0) << 24) | ((frame.payload[7] || 0) << 16) | ((frame.payload[8] || 0) << 8) | (frame.payload[9] || 0)
          const durationMin = ((frame.payload[10] || 0) << 8) | (frame.payload[11] || 0)
          const startDate = new Date(startEpoch * 1000).toLocaleString('zh-CN')
          const endDate = new Date(endEpoch * 1000).toLocaleString('zh-CN')
          this.setData({ ackText: `Session ${index + 1}/${total}: ${startDate} ~ ${endDate} (${durationMin}min)` })
        }
      })
    })
  },

  startStatusPolling() {
    this.stopStatusPolling()
    this.statusPollTimer = setInterval(() => this.getStatus(), STATUS_POLL_MS)
  },

  stopStatusPolling() {
    if (this.statusPollTimer) {
      clearInterval(this.statusPollTimer)
      this.statusPollTimer = null
    }
  },

  writeCmd(cmd, payload) {
    const { deviceId, serviceId, writeCharId } = this.data
    const targets = (this.writeTargets && this.writeTargets.length)
      ? this.writeTargets
      : [{ serviceId, characteristicId: writeCharId }]
    if (!deviceId || !targets.length || !targets[0].serviceId || !targets[0].characteristicId) {
      this.setData({ ackText: '写入通道未就绪' })
      return
    }

    const value = buildFrame(cmd, payload)
    const sendNext = (index) => {
      if (index >= targets.length) return
      const target = targets[index]
      wx.writeBLECharacteristicValue({
        deviceId,
        serviceId: target.serviceId,
        characteristicId: target.characteristicId,
        value,
        fail: (err) => {
          this.setData({ ackText: `Write failed ${target.characteristicId}: ` + (err.errMsg || '') })
        },
        complete: () => setTimeout(() => sendNext(index + 1), 40)
      })
    }
    sendNext(0)
  },

  getStatus() {
    if (!this.data.connected) return
    this.writeCmd(CMD.GET_STATUS, [])
  },

  queueStatusRefresh() {
    if (this.statusRefreshTimer) clearTimeout(this.statusRefreshTimer)
    this.statusRefreshTimer = setTimeout(() => {
      this.getStatus()
      this.statusRefreshTimer = null
    }, 300)
  },

  persistCompletedRecord(record) {
    if (!record) return
    const history = loadHistory(wx)
    saveHistory(wx, mergeNewest(history, record))
  },

  handleSessionHistory(status) {
    const epoch = Math.floor(Date.now() / 1000)
    const result = updateTracker(this.sessionTracker, this.lastDeviceState, status.state, epoch)
    this.sessionTracker = result.tracker
    this.lastDeviceState = status.state
    this.persistCompletedRecord(result.completedRecord)
  },

  runWriteSequence(steps, doneDelay = 400) {
    const queue = Array.isArray(steps) ? steps.filter(Boolean) : []
    this.stopStatusPolling()
    const runNext = (index) => {
      if (index >= queue.length) {
        this.queueStatusRefresh()
        setTimeout(() => {
          this.startStatusPolling()
          this.getStatus()
        }, doneDelay)
        return
      }
      queue[index]()
      setTimeout(() => runNext(index + 1), 160)
    }
    runNext(0)
  },

  decTemp() {
    this.tempDirty = true
    this.setData({ tempSet: Math.max(TEMP_MIN, this.data.tempSet - 1) })
  },

  incTemp() {
    this.tempDirty = true
    this.setData({ tempSet: Math.min(TEMP_MAX, this.data.tempSet + 1) })
  },

  sendSetTemp() {
    this.writeCmd(CMD.SET_TEMP, [this.data.tempSet & 0xff])
    this.queueStatusRefresh()
  },

  decMinutes() {
    const current = parseInt(this.data.minutesInput || '0', 10)
    const base = Number.isNaN(current) ? 0 : current
    const next = Math.max(0, base - 5)
    this.timeDirty = true
    this.setData({ minutesLeft: next, minutesInput: String(next) })
  },

  incMinutes() {
    const current = parseInt(this.data.minutesInput || '0', 10)
    const base = Number.isNaN(current) ? 0 : current
    const next = Math.min(65535, base + 5)
    this.timeDirty = true
    this.setData({ minutesLeft: next, minutesInput: String(next) })
  },

  onMinutesInput(e) {
    const raw = e.detail.value || ''
    const parsed = parseInt(raw, 10)
    const nextData = { minutesInput: raw }
    this.timeDirty = true
    if (!Number.isNaN(parsed)) nextData.minutesLeft = Math.max(0, Math.min(65535, parsed))
    this.setData(nextData)
  },

  applyMinutesInput() {
    const raw = String(this.data.minutesInput || '').trim()
    let value = parseInt(raw, 10)
    if (Number.isNaN(value)) value = this.data.minutesLeft || 0
    value = Math.max(0, Math.min(65535, value))
    this.timeDirty = true
    this.setData({ minutesLeft: value, minutesInput: String(value) })
  },

  sendSetTime() {
    this.applyMinutesInput()
    const v = minutesToDeviceSeconds(this.data.minutesLeft) & 0xffff
    this.writeCmd(CMD.SET_TIME, [(v >> 8) & 0xff, v & 0xff])
    this.queueStatusRefresh()
  },

  sendStart() {
    this.applyMinutesInput()
    if (!this.data.minutesLeft || this.data.minutesLeft <= 0) {
      wx.showToast({ title: '请先设置时间', icon: 'none' })
      return
    }
    const temp = this.data.tempSet & 0xff
    const v = minutesToDeviceSeconds(this.data.minutesLeft) & 0xffff
    this.sessionTracker = beginManualSession(this.sessionTracker, Math.floor(Date.now() / 1000))
    this.runWriteSequence([
      () => this.writeCmd(CMD.SET_TEMP, [temp]),
      () => this.writeCmd(CMD.SET_TIME, [(v >> 8) & 0xff, v & 0xff]),
      () => this.writeCmd(CMD.START, [])
    ], 700)
  },

  openHistory() {
    const app = getApp()
    if (app && app.globalData) app.globalData.deviceId = this.data.deviceId || ''
    wx.navigateTo({ url: '/pages/history/index?deviceId=' + encodeURIComponent(this.data.deviceId || '') })
  },

  sendStop() {
    const manual = finishManualSession(this.sessionTracker, Math.floor(Date.now() / 1000))
    this.sessionTracker = manual.tracker
    this.persistCompletedRecord(manual.completedRecord)
    this.runWriteSequence([
      () => this.writeCmd(CMD.STOP, [])
    ], 400)
  }
})