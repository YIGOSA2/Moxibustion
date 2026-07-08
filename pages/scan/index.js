Page({
  data: {
    devices: [],
    statusText: '未开始',
    isScanning: false,
    isConnecting: false,
    connectingDeviceId: ''
  },

  onLoad() {
    this.deviceMap = {}
    wx.openBluetoothAdapter({
      success: () => {
        console.log('[扫描页] 蓝牙适配器已就绪')
        this.setData({ statusText: '蓝牙已就绪' })
      },
      fail: (e) => {
        console.error('[扫描页] 蓝牙适配器打开失败:', e.errMsg || e)
        this.setData({ statusText: '蓝牙不可用: ' + (e.errMsg || '') })
      }
    })

    wx.onBluetoothDeviceFound((res) => {
      const list = res.devices || []
      list.forEach((d) => {
        if (!d.deviceId) return
        this.deviceMap[d.deviceId] = { ...this.deviceMap[d.deviceId], ...d }
      })
      this.syncDevices()
    })
  },

  onUnload() {
    this.stopScan()
    wx.offBluetoothDeviceFound()
  },

  startScan() {
    if (this.data.isConnecting) return
    wx.startBluetoothDevicesDiscovery({
      allowDuplicatesKey: false,
      interval: 0,
      success: () => this.setData({ statusText: '扫描中...', isScanning: true }),
      fail: (e) => this.setData({ statusText: '扫描失败: ' + (e.errMsg || '') })
    })
  },

  stopScan() {
    wx.stopBluetoothDevicesDiscovery({
      complete: () => this.setData({ statusText: '已停止扫描', isScanning: false })
    })
  },

  connect(e) {
    const deviceId = e.currentTarget.dataset.id
    if (!deviceId || this.data.isConnecting) return

    this.setData({
      isConnecting: true,
      connectingDeviceId: deviceId,
      statusText: '连接中...'
    })

    console.log('[扫描页] 开始连接:', deviceId)
    wx.createBLEConnection({
      deviceId,
      timeout: 10000,
      success: () => {
        console.log('[扫描页] 连接成功:', deviceId)
        this.setData({ isConnecting: false, connectingDeviceId: '' })
        getApp().globalData = getApp().globalData || {}
        getApp().globalData.deviceId = deviceId
        wx.navigateTo({ url: `/pages/control/index?deviceId=${encodeURIComponent(deviceId)}` })
      },
      fail: (err) => {
        console.error('[扫描页] 连接失败:', deviceId, err.errMsg || err)
        this.setData({
          isConnecting: false,
          connectingDeviceId: '',
          statusText: '连接失败: ' + (err.errMsg || '')
        })
      }
    })
  },

  syncDevices() {
    const devices = Object.values(this.deviceMap).map((device) => ({
      ...device,
      displayName: this.getDisplayName(device)
    }))
    this.setData({ devices })
  },

  getDisplayName(device) {
    const rawName = device.name || device.localName || ''
    if (rawName === 'NB-C790211A2011') {
      return '艾灸垫'
    }
    return rawName || '未命名设备'
  }
})
