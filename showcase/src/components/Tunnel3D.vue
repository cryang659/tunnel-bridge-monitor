<script setup>
import { ref, onMounted, onUnmounted } from 'vue'
import * as THREE from 'three'
import { OrbitControls } from 'three/addons/controls/OrbitControls.js'
import { GLTFLoader } from 'three/addons/loaders/GLTFLoader.js'
import { RoomEnvironment } from 'three/addons/environments/RoomEnvironment.js'
import { api } from '../api'

const container = ref(null)
let scene, camera, renderer, controls
let markers = []
let loaded = false
let animationId = null
let timer = null

const MODEL_URL = '/models/tunnel.glb'
const NORMAL_COLOR = 0x1668dc
const ALARM_COLOR = 0xdc2626
const DEVICE_ID = 'NODE_002'

/* GLB 对象名 → 后端物模型 type。
 * 隧道模型按硬件型号命名，与上报的 type 对不上，必须映射才能查阈值。
 * 先按完整名查（区分同型多测点），再退回前缀名。
 * light（5506 感光）已取消、buzzer/relay 是执行器 —— 不映射，保持模型原样。 */
const SENSOR_TYPE_MAP = {
  th: 'temperature_1',        // SHT30#1 温湿度，取温度代表
  water: 'water_level_1',     // 水位
  soil: 'seepage_1',          // MH-RD 雨滴（渗水）
  laser: 'convergence',       // 默认：侧壁两颗测净空收敛
  laser_L1: 'displacement',   // 覆盖：穹顶那颗测位移（Y=5.41，另两颗 Y=1.61）
}

function frameModel(root) {
  const box = new THREE.Box3().setFromObject(root)
  const size = new THREE.Vector3(); box.getSize(size)
  const center = new THREE.Vector3(); box.getCenter(center)
  const radius = Math.max(size.x, size.y, size.z) / 2
  const dir = new THREE.Vector3(0.8, 0.5, 1).normalize()
  camera.position.copy(center).add(dir.multiplyScalar(radius * 1.45))
  camera.near = radius / 200
  camera.far = radius * 100
  camera.updateProjectionMatrix()
  controls.target.copy(center)
  controls.minDistance = radius * 0.3
  controls.maxDistance = radius * 12
  controls.update()

  // 网格地面：让底部不再是死黑空场
  const grid = new THREE.GridHelper(radius * 4, 30, 0x2e3a50, 0x1b2436)
  grid.position.set(center.x, box.min.y, center.z)
  scene.add(grid)
}

function initScene() {
  const w = container.value.clientWidth || 600
  const h = container.value.clientHeight || 400

  scene = new THREE.Scene()
  scene.background = new THREE.Color(0x757a80)

  camera = new THREE.PerspectiveCamera(50, w / h, 0.1, 5000)
  camera.position.set(20, 15, 45)

  renderer = new THREE.WebGLRenderer({ antialias: true })
  renderer.setSize(w, h)
  renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2))
  container.value.appendChild(renderer.domElement)

  // 环境贴图：让 PBR/漆面材质有正常反射质感
  const pmrem = new THREE.PMREMGenerator(renderer)
  scene.environment = pmrem.fromScene(new RoomEnvironment(), 0.05).texture
  scene.environmentIntensity = 0.35

  controls = new OrbitControls(camera, renderer.domElement)
  controls.enableDamping = true
  controls.maxPolarAngle = Math.PI * 0.55

  scene.add(new THREE.AmbientLight(0xffffff, 0.2))
  const sun = new THREE.DirectionalLight(0xffffff, 0.4)
  sun.position.set(30, 40, 40)
  scene.add(sun)
  const sun2 = new THREE.DirectionalLight(0xffffff, 0.08)
  sun2.position.set(-30, 10, -30)
  scene.add(sun2)

  const loader = new GLTFLoader()
  loader.load(MODEL_URL, (gltf) => {
    gltf.scene.traverse((o) => {
      if (o.isMesh && o.name && o.name.startsWith('sensor_')) {
        const rest = o.name.slice('sensor_'.length)
        const idx = rest.lastIndexOf('_')
        const key = idx > 0 ? rest.slice(0, idx) : rest
        const type = SENSOR_TYPE_MAP[rest] || SENSOR_TYPE_MAP[key]
        if (!type) return   // 执行器/已取消的测点：保持模型原样
        o.userData.type = type
        if (o.material) o.material = o.material.clone()
        o.geometry = o.geometry.clone()
        o.geometry.scale(2.2, 2.2, 2.2)
        markers.push({ obj: o, type })
      }
    })
    scene.add(gltf.scene)
    frameModel(gltf.scene)
    loaded = true
    refreshStatus()
  }, undefined, (err) => console.error('GLB load failed', err))

  animate()
}

/* 取每个类型的最新一条读数。
 * 不能用 getStats() —— 它返回的是历史全量 MAX，只要历史上超限过一次就永远判红。 */
async function refreshStatus() {
  if (!loaded) return
  try {
    const [rows, thresholds] = await Promise.all([
      api.getData({ device_id: DEVICE_ID, limit: 300 }),
      api.getThresholds(),
    ])
    // query_data 按 timestamp DESC 返回，每种类型首次出现即最新值
    const latest = {}
    for (const r of rows) {
      if (!(r.type in latest)) latest[r.type] = r.value
    }
    for (const m of markers) {
      const mat = m.obj.material
      const th = thresholds[m.type]
      const val = latest[m.type]
      const alarm = th !== undefined && val !== undefined && val >= th
      mat.color.setHex(alarm ? ALARM_COLOR : NORMAL_COLOR)
      mat.emissive.setHex(alarm ? ALARM_COLOR : NORMAL_COLOR)
      mat.emissiveIntensity = alarm ? 0.9 : 0.35
    }
  } catch (e) {
    console.error(e)
  }
}

function animate() {
  animationId = requestAnimationFrame(animate)
  controls.update()
  renderer.render(scene, camera)
}

function handleResize() {
  if (!container.value || !renderer) return
  const w = container.value.clientWidth
  const h = container.value.clientHeight
  camera.aspect = w / h
  camera.updateProjectionMatrix()
  renderer.setSize(w, h)
}

onMounted(() => {
  initScene()
  timer = setInterval(refreshStatus, 4000)
  window.addEventListener('resize', handleResize)
})

onUnmounted(() => {
  cancelAnimationFrame(animationId)
  clearInterval(timer)
  window.removeEventListener('resize', handleResize)
  if (renderer && container.value) container.value.removeChild(renderer.domElement)
  renderer = null
  markers = []
})
</script>

<template>
  <div ref="container" class="model-box"></div>
</template>

<style scoped>
.model-box { width: 100%; height: 100%; }
</style>
