<script setup>
import { ref, onMounted, onUnmounted } from 'vue'
import * as THREE from 'three'
import { OrbitControls } from 'three/addons/controls/OrbitControls.js'
import { GLTFLoader } from 'three/addons/loaders/GLTFLoader.js'
import { api } from '../api'

const container = ref(null)
let scene, camera, renderer, controls
let markers = []
let loaded = false
let animationId = null
let timer = null

const MODEL_URL = '/models/bridge.glb'
const NORMAL_COLOR = 0x1668dc
const ALARM_COLOR = 0xdc2626
const DEVICE_ID = 'NODE_001'

/* GLB 对象名 → 后端物模型 type。
 * Blender 里按硬件型号命名（accel/th/...），与上报的 type 对不上，
 * 必须映射到 type 才能查阈值。执行器和已取消的传感器不在此表内 —— 保持模型原样。 */
const SENSOR_TYPE_MAP = {
  accel: 'vibration',       // MPU-6050 桥面震动
  vibration: 'vibration',   // 索力测点兼振动
  stress: 'stress',
  load: 'load',
  temperature: 'temperature',
  humidity: 'humidity',
  th: 'temperature',        // 温湿度一体模块，取温度代表
}

function frameModel(root) {
  const box = new THREE.Box3().setFromObject(root)
  const size = new THREE.Vector3(); box.getSize(size)
  const center = new THREE.Vector3(); box.getCenter(center)
  const radius = Math.max(size.x, size.y, size.z) / 2
  const dir = new THREE.Vector3(0.7, 0.6, 1).normalize()
  camera.position.copy(center).add(dir.multiplyScalar(radius * 1.6))
  camera.near = radius / 200
  camera.far = radius * 100
  camera.updateProjectionMatrix()
  controls.target.copy(center)
  controls.minDistance = radius * 0.3
  controls.maxDistance = radius * 12
  controls.update()

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
  camera.position.set(45, 40, 80)

  renderer = new THREE.WebGLRenderer({ antialias: true })
  renderer.setSize(w, h)
  renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2))
  renderer.toneMapping = THREE.NoToneMapping
  container.value.appendChild(renderer.domElement)

  controls = new OrbitControls(camera, renderer.domElement)
  controls.enableDamping = true
  controls.maxPolarAngle = Math.PI * 0.55

  // 无反光：烘焙贴图已含光影，提高环境光让整体明亮
  scene.add(new THREE.AmbientLight(0xffffff, 1.8))
  const sun = new THREE.DirectionalLight(0xffffff, 0.6)
  sun.position.set(60, 90, 50)
  scene.add(sun)
  const sun2 = new THREE.DirectionalLight(0xffffff, 0.2)
  sun2.position.set(-50, 20, -40)
  scene.add(sun2)

  const loader = new GLTFLoader()
  loader.load(MODEL_URL, (gltf) => {
    gltf.scene.traverse((o) => {
      const nm = o.name || ''
      if (!o.isMesh) return
      if (nm.indexOf('sensor_') === 0) {
        const rest = nm.slice('sensor_'.length)
        const idx = rest.lastIndexOf('_')
        const key = idx > 0 ? rest.slice(0, idx) : rest
        const type = SENSOR_TYPE_MAP[key]
        if (!type) return   // 执行器/未映射的测点：保持模型原材质
        o.userData.type = type
        const mat = new THREE.MeshStandardMaterial({
          color: NORMAL_COLOR, roughness: 0.3, metalness: 0.1,
          emissive: new THREE.Color(NORMAL_COLOR), emissiveIntensity: 0.5,
        })
        o.material = mat
        o.geometry = o.geometry.clone()
        o.geometry.scale(2.2, 2.2, 2.2)
        markers.push({ obj: o, type })
      } else if (/water/i.test(nm)) {
        o.material = new THREE.MeshLambertMaterial({
          color: 0x6b8099, transparent: true, opacity: 0.82,
        })
      }
      // 其余结构件保留烘焙贴图材质
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
      mat.emissiveIntensity = alarm ? 0.9 : 0.5
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
