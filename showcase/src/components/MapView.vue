<script setup>
import { ref, onMounted, onUnmounted } from 'vue'
import L from 'leaflet'
import { api } from '../api'

const mapEl = ref(null)
let map = null
let markers = []
let timer = null

function markerIcon(color) {
  return L.divIcon({
    className: '',
    html: `<div style="width:14px;height:14px;border-radius:50%;background:${color};border:2px solid #fff;box-shadow:0 1px 4px rgba(16,30,47,.35)"></div>`,
    iconSize: [18, 18],
    iconAnchor: [9, 9],
  })
}

function initMap() {
  // 默认中心：贵阳（项目托管地）
  map = L.map(mapEl.value).setView([26.65, 106.63], 6)
  L.tileLayer('https://webrd0{s}.is.autonavi.com/appmaptile?lang=zh_cn&size=1&scale=1&style=7&x={x}&y={y}&z={z}', {
    subdomains: ['1', '2', '3', '4'],
    attribution: '高德地图',
  }).addTo(map)
}

async function refreshMarkers() {
  try {
    const devs = await api.getDevices()
    markers.forEach((m) => map.removeLayer(m))
    markers = []

    for (const d of devs) {
      if (d.latitude == null || d.longitude == null) continue
      const m = L.marker([d.latitude, d.longitude], { icon: markerIcon('#1668dc') }).addTo(map)
      m.bindPopup(`<b>${d.name || d.device_id}</b><br>${d.device_id}<br>${d.location || ''}`)
      markers.push(m)
    }

    if (markers.length) {
      map.fitBounds(L.featureGroup(markers).getBounds().pad(0.3), { maxZoom: 12 })
    }
  } catch (e) {
    console.error(e)
  }
}

onMounted(() => {
  initMap()
  refreshMarkers()
  timer = setInterval(refreshMarkers, 5000)
})
onUnmounted(() => {
  clearInterval(timer)
  if (map) map.remove()
})
</script>

<template>
  <section class="card">
    <div class="card-head">
      <div>
        <div class="card-title">GIS 地图 · 节点分布</div>
        <div class="card-sub">已配置经纬度的设备会显示在地图上</div>
      </div>
    </div>
    <div class="card-body card-body--flush">
      <div ref="mapEl" class="map-container"></div>
    </div>
  </section>
</template>
