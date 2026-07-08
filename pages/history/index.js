const { buildFrame, parseFrames, CMD } = require('../../utils/protocol')
const { chooseGattEndpoint, isNotifiable } = require('../../utils/ble-gatt')
const { loadHistory, saveHistory } = require('../../utils/session-history')

function epochToLocal(epoch) {
  if (!epoch) return '未知'
  const d = new Date(epoch * 1000)
  const pad = (n) => String(n).padStart(2, '0')
  return `${d.getFullYear()}年${pad(d.getMonth() + 1)}月${pad(d.getDate())}日 ${pad(d.getHours())}:${pad(d.getMinutes())}`
}

function mapSession(record, index) {
  const startEpoch = Number(record && record.startEpoch) || 0
  const endEpoch = Number(record && record.endEpoch) || startEpoch
  const durationMin = Number(record && record.durationMin) || 0
  return {
    index,
    startEpoch,
    endEpoch,
    durationMin,
    startText: epochToLocal(startEpoch),
    endText: epochToLocal(endEpoch),
    durationText: `${durationMin} 分钟`
  }
}

Page({
  data: {
    sessions: [],
    totalCount: 0,
    statusText: '请先连接设备',
    loading: false,
    totalMinutes: 0
  },

  onLoad() {
    const app = getApp()
    this.deviceId = (app.globalData && app.globalData.deviceId) || ''
    this.writeTargets = []
    this.notifyTargets = []
    this.pendingSessions = []
    this.expectedCount = 0
    this.fetchIndex = 0
    this.localSessions = loadHistory(wx).map((item, index) => mapSession(item, index)).reverse()
  },

  onShow() {
    const app = getApp()
    const deviceId = (app.globalData && app.globalData.deviceId) || ''
    this.deviceId = deviceId
    this.localSessions = loadHistory(wx).map((item, index) => mapSession(item, index)).reverse()
    if (!deviceId) {
      if (this.localSessions.length) {
        const totalMinutes = this.localSessions.reduce((acc, s) => acc + s.durationMin, 0)
        this.setData({
          sessions: this.localSessions,
          totalCount: this.localSessions.length,
          totalMinutes,
          statusText: `本地记录 ${this.localSessions.length} 条，累计 ${totalMinutes} 分钟`,
          loading: false
        })
      } else {
        this.setData({ statusText: '请先在控制页连接设备', sessions: [], totalCount: 0, totalMinutes: 0, loading: false })
      }
      return
    }
    this.initGatt()
  },

  initGatt() {
    const deviceId = this.deviceId
    if (!deviceId) return

    wx.getBLEDeviceServices({
      deviceId,
      success: (sres) => {
        const services = sres.services || []
        const pending = services.map((service) => new Promise((resolve) => {
          wx.getBLEDeviceCharacteristics({
            deviceId,
            serviceId: service.uuid,
            success: (cres) => resolve({ service, characteristics: cres.characteristics || [] }),
            fail: () => resolve({ service, characteristics: [] })
          })
        }))

        Promise.all(pending).then((entries) => {
          const endpoint = chooseGattEndpoint(entries)
          if (!endpoint) {
            this.setData({ statusText: '未找到可用的蓝牙通道', loading: false })
            return
          }

          this.writeTargets = [{ serviceId: endpoint.service.uuid, characteristicId: endpoint.writeChar.uuid }]
          const notifyTargets = []
          entries.forEach((item) => {
            ;(item.characteristics || []).forEach((ch) => {
              if (isNotifiable(ch)) {
                notifyTargets.push({ serviceId: item.service.uuid, characteristicId: ch.uuid })
              }
            })
          })
          this.notifyTargets = notifyTargets

          this.bindNotify()
          this.enableAllNotify(deviceId, notifyTargets, 0)
        })
      },
      fail: () => this.setData({ statusText: '获取蓝牙服务失败', loading: false })
    })
  },

  enableAllNotify(deviceId, targets, index) {
    if (!targets || index >= targets.length) {
      this.fetchSessionCount()
      return
    }
    const target = targets[index]
    wx.notifyBLECharacteristicValueChange({
      deviceId,
      serviceId: target.serviceId,
      characteristicId: target.characteristicId,
      state: true,
      complete: () => this.enableAllNotify(deviceId, targets, index + 1)
    })
  },

  bindNotify() {
    wx.offBLECharacteristicValueChange()
    wx.onBLECharacteristicValueChange((res) => {
      const frames = parseFrames(res.value)
      frames.forEach((frame) => {
        if (frame.cmd === CMD.SESSION_COUNT) {
          const count = frame.payload[0] || 0
          this.expectedCount = count
          this.pendingSessions = new Array(count)
          this.fetchIndex = 0
          this.setData({ totalCount: count, loading: count > 0 })
          if (count === 0) {
            if (this.localSessions.length) {
              const totalMinutes = this.localSessions.reduce((acc, s) => acc + s.durationMin, 0)
              this.setData({
                statusText: `本地记录 ${this.localSessions.length} 条，累计 ${totalMinutes} 分钟`,
                sessions: this.localSessions,
                totalMinutes,
                loading: false,
                totalCount: this.localSessions.length
              })
            } else {
              this.setData({ statusText: '暂无使用记录', sessions: [], totalMinutes: 0, loading: false })
            }
          } else {
            this.setData({ statusText: `正在读取记录 0 / ${count}...` })
            this.fetchNextSession()
          }
          return
        }

        if (frame.cmd === CMD.SESSION) {
          const index = frame.payload[0] || 0
          const total = frame.payload[1] || 0
          const startEpoch = ((frame.payload[2] || 0) << 24) | ((frame.payload[3] || 0) << 16) |
            ((frame.payload[4] || 0) << 8) | (frame.payload[5] || 0)
          const endEpoch = ((frame.payload[6] || 0) << 24) | ((frame.payload[7] || 0) << 16) |
            ((frame.payload[8] || 0) << 8) | (frame.payload[9] || 0)
          const durationMin = ((frame.payload[10] || 0) << 8) | (frame.payload[11] || 0)

          this.pendingSessions[index] = {
            index,
            startEpoch,
            endEpoch,
            durationMin,
            startText: epochToLocal(startEpoch),
            endText: epochToLocal(endEpoch),
            durationText: `${durationMin} 分钟`
          }

          const received = this.pendingSessions.filter(Boolean).length
          this.setData({ statusText: `正在读取记录 ${received} / ${total}...` })

          this.fetchIndex = index + 1
          if (this.fetchIndex < this.expectedCount) {
            setTimeout(() => this.fetchNextSession(), 100)
          } else {
            const sessions = this.pendingSessions.filter(Boolean).reverse()
            const totalMinutes = sessions.reduce((acc, s) => acc + s.durationMin, 0)
            this.setData({
              sessions,
              totalMinutes,
              loading: false,
              statusText: `共 ${sessions.length} 条记录，累计 ${totalMinutes} 分钟`
            })
          }
        }
      })
    })
  },

  writeCmd(cmd, payload) {
    const deviceId = this.deviceId
    const targets = this.writeTargets
    if (!deviceId || !targets.length) return

    const value = buildFrame(cmd, payload)
    wx.writeBLECharacteristicValue({
      deviceId,
      serviceId: targets[0].serviceId,
      characteristicId: targets[0].characteristicId,
      value
    })
  },

  fetchSessionCount() {
    this.setData({ statusText: '正在查询记录数量...', loading: true })
    this.writeCmd(CMD.GET_SESSION_COUNT, [])
  },

  fetchNextSession() {
    this.writeCmd(CMD.GET_SESSION, [this.fetchIndex & 0xff])
  },

  refresh() {
    this.pendingSessions = []
    this.expectedCount = 0
    this.fetchIndex = 0
    this.localSessions = loadHistory(wx).map((item, index) => mapSession(item, index)).reverse()
    this.setData({ sessions: [], totalCount: 0, totalMinutes: 0 })
    this.fetchSessionCount()
  },

  onUnload() {
    wx.offBLECharacteristicValueChange()
  },

  clearHistory() {
    wx.showModal({
      title: '确认清除',
      content: '确定要清除所有使用记录吗？此操作不可恢复。',
      confirmText: '清除',
      confirmColor: '#e53e3e',
      success: (res) => {
        if (res.confirm) {
          this.writeCmd(CMD.CLEAR_SESSIONS, [])
          saveHistory(wx, [])
          setTimeout(() => {
            this.pendingSessions = []
            this.expectedCount = 0
            this.fetchIndex = 0
            this.localSessions = []
            this.setData({ sessions: [], totalCount: 0, totalMinutes: 0, statusText: '记录已清除', loading: false })
          }, 300)
        }
      }
    })
  }
})