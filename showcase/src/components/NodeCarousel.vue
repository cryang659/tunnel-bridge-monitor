<script setup>
// ============================================================
// 功能模块 3D 卡片轮播（由「银行卡片 3D 轮播」转换而来）
// 卡片 = 前端六页功能模块，鼠标滚轮切换，点击进入对应功能
// 纯黑沉浸式，作为独立项目首页主视图
// ============================================================
import { ref, onMounted, onBeforeUnmount } from 'vue'

const emit = defineEmits(['navigate'])

// 功能模块（对应六页）
const MODULES = [
  {
    key: 'dashboard', name: '数据看板', en: 'Dashboard',
    desc: '实时监测数据总览与统计',
    color: '#1668dc',
    icon: '<svg viewBox="0 0 18 18" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><rect x="2.5" y="2.5" width="5.2" height="5.2" rx="1.2"/><rect x="10.3" y="2.5" width="5.2" height="5.2" rx="1.2"/><rect x="2.5" y="10.3" width="5.2" height="5.2" rx="1.2"/><rect x="10.3" y="10.3" width="5.2" height="5.2" rx="1.2"/></svg>',
  },
  {
    key: 'map', name: 'GIS 地图', en: 'GIS Map',
    desc: '监测节点地理分布与定位',
    color: '#16a34a',
    icon: '<svg viewBox="0 0 18 18" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><path d="M9 16.5S3.5 13 3.5 8.2A5.5 5.5 0 0 1 9 2.7a5.5 5.5 0 0 1 5.5 5.5c0 4.8-5.5 8.3-5.5 8.3z"/><circle cx="9" cy="8.2" r="2"/></svg>',
  },
  {
    key: '3d', name: '3D 模型', en: '3D Model',
    desc: '桥梁结构三维可视化',
    color: '#5856D6',
    icon: '<svg viewBox="0 0 18 18" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><path d="M2.8 5.2l6.2-3.1 6.2 3.1v7.6L9 15.9l-6.2-3.1z"/><path d="M2.8 5.2l6.2 3.1 6.2-3.1"/><path d="M9 8.3v7.6"/></svg>',
  },
  {
    key: 'alarm', name: '告警列表', en: 'Alarms',
    desc: '告警记录查询与追溯',
    color: '#FF3B30',
    icon: '<svg viewBox="0 0 18 18" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><path d="M14.8 6.5a6 6 0 0 0-11.6 0C3.2 11.3 1.5 12 1.5 12h15s-1.7-.7-1.7-5.5z"/><path d="M10.3 2.2V3.8"/><path d="M7.4 14.5a1.8 1.8 0 0 0 3.2 0"/></svg>',
  },
  {
    key: 'threshold', name: '阈值管理', en: 'Thresholds',
    desc: '各传感器告警阈值配置',
    color: '#FF9500',
    icon: '<svg viewBox="0 0 18 18" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round"><path d="M6.5 4.5h9M2.5 4.5h1M10.5 9h5M2.5 9h5M13.5 13.5h2M2.5 13.5h8"/><circle cx="5" cy="4.5" r="1.5" fill="currentColor" stroke="none"/><circle cx="9" cy="9" r="1.5" fill="currentColor" stroke="none"/><circle cx="12" cy="13.5" r="1.5" fill="currentColor" stroke="none"/></svg>',
  },
  {
    key: 'device', name: '设备管理', en: 'Devices',
    desc: '节点注册、经纬度与状态',
    color: '#00C7BE',
    icon: '<svg viewBox="0 0 18 18" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><rect x="2.5" y="2.5" width="13" height="5" rx="1.2"/><rect x="2.5" y="10.5" width="13" height="5" rx="1.2"/><path d="M6.5 5v.01M6.5 13v.01"/></svg>',
  },
]

const cards = MODULES
const cardCount = cards.length

// 体积厚度层（正面 / 背面 + 3 层中间结构）
const midLayers = [-0.73, 0, 0.73]
const frontZ = 1.47
const backZ = -1.47

// ---- 轮播状态（非响应式，rAF 循环里高频读写）----
const cardEls = []
let frameId = 0
const progress = { value: 0 }
const mouse = { x: 0, y: 0, targetX: 0, targetY: 0 }

// ---- 滚轮切换状态（滚到阈值切一张，吸附到整卡）----
let wheelAccum = 0      // 滚轮累积量
let settleTarget = null  // 吸附目标（整卡索引）

// ---- 响应式：卡片尺寸 ----
const metrics = ref({ cardW: 480, cardH: 300 })

function calcMetrics() {
  const w = window.innerWidth
  const h = window.innerHeight
  let cardW = Math.round(w * 0.20 + 200)
  const heightFactor = Math.min(1.0, Math.max(0.6, h / 900))
  cardW = Math.round(cardW * heightFactor)
  cardW = Math.min(520, Math.max(280, cardW))
  const cardH = Math.round(cardW / 1.6)
  metrics.value = { cardW, cardH }
}

// ---- 滚轮：累积到阈值切一张，方向 = 上一张 / 下一张 ----
function onWheel(e) {
  e.preventDefault()
  wheelAccum += e.deltaY
  const threshold = 400
  while (Math.abs(wheelAccum) >= threshold) {
    const dir = wheelAccum > 0 ? 1 : -1
    wheelAccum -= dir * threshold
    if (settleTarget === null) settleTarget = Math.round(progress.value) + dir
    else settleTarget += dir
  }
}

// ---- 鼠标视差倾斜 ----
function onMouseMove(e) {
  const rx = (e.clientX - window.innerWidth / 2) / (window.innerWidth / 2)
  const ry = (e.clientY - window.innerHeight / 2) / (window.innerHeight / 2)
  mouse.targetX = Math.max(-1, Math.min(1, rx))
  mouse.targetY = Math.max(-1, Math.min(1, ry))
}
function onMouseLeave() {
  mouse.targetX = 0
  mouse.targetY = 0
}

function onCardClick(key) {
  emit('navigate', key)
}

// ---- 60fps 渲染循环（滚轮吸附 + 视差倾斜阻尼 + smoothstep 圆柱排布）----
function renderLoop() {
  // 吸附到整卡（避免卡在两张卡之间），指数缓动保留切换感
  if (settleTarget !== null) {
    const diff = settleTarget - progress.value
    if (Math.abs(diff) < 0.0005) {
      progress.value = settleTarget
      settleTarget = null
    } else {
      progress.value += diff * 0.05
    }
  }

  mouse.x += (mouse.targetX - mouse.x) * 0.08
  mouse.y += (mouse.targetY - mouse.y) * 0.08

  const h = window.innerHeight
  const { cardH } = metrics.value

  const continuousProgress = progress.value
  const roundedIndex = Math.round(continuousProgress)
  const diffFromRound = continuousProgress - roundedIndex
  const easedDiff = Math.sign(diffFromRound) * Math.pow(Math.abs(diffFromRound) * 2, 4.2) / 2
  const virtualActiveIndex = roundedIndex + easedDiff

  for (let i = 0; i < cardCount; i++) {
    const card = cardEls[i]
    if (!card) continue

    let offset = i - virtualActiveIndex
    const halfCount = cardCount / 2
    while (offset > halfCount) offset -= cardCount
    while (offset < -halfCount) offset += cardCount

    const absOffset = Math.abs(offset)
    const sign = Math.sign(offset)

    // 只有接近主页面（居中）的卡片才显示，其余隐藏
    if (absOffset > 1.2) {
      card.style.visibility = 'hidden'
      continue
    }
    card.style.visibility = 'visible'

    const gap = 80
    const peekAmount = -55
    const D = 1350

    let y = 0
    let z = 0
    let rot = 0

    if (absOffset <= 1) {
      const t = absOffset
      const easedT = t * t * (3 - 2 * t)
      const targetY = cardH + gap
      y = -sign * (easedT * targetY)
      z = 400 + easedT * (220 - 400)
      rot = easedT * 132
    } else if (absOffset <= 2) {
      const t = absOffset - 1
      const easedT = t * t * (3 - 2 * t)
      const yStart = cardH + gap
      const zStart = 220
      const rotStart = 132
      const zEnd = -60
      const rotEnd = 175
      const sEnd = D / (D - zEnd)
      const yEnd = (h / 2 - peekAmount) / sEnd - (cardH / 2)
      const currentY = yStart + easedT * (yEnd - yStart)
      y = -sign * currentY
      z = zStart + easedT * (zEnd - zStart)
      rot = rotStart + easedT * (rotEnd - rotStart)
    } else {
      const t = Math.min(absOffset - 2, 1)
      const easedT = t * t * (3 - 2 * t)
      const zStart = -60
      const rotStart = 175
      const zEnd3 = -250
      const rotEnd3 = 195
      const sEnd2 = D / (D - zStart)
      const yEnd2 = (h / 2 - peekAmount) / sEnd2 - (cardH / 2)
      const sEnd3 = D / (D - zEnd3)
      const yEnd3 = (h / 2 + 100) / sEnd3 + (cardH / 2)
      const currentY = yEnd2 + easedT * (yEnd3 - yEnd2)
      y = -sign * currentY
      z = zStart + easedT * (zEnd3 - zStart)
      rot = rotStart + easedT * (rotEnd3 - rotStart)
    }

    const localCardRotation = -sign * rot
    const centerFactor = Math.max(0, 1 - absOffset)

    const maxTiltY = 15
    const maxTiltX = 12
    const activeTiltX = -mouse.y * maxTiltX * centerFactor
    const activeTiltY = mouse.x * maxTiltY * centerFactor

    const totalRotX = localCardRotation + activeTiltX
    const totalRotY = activeTiltY

    card.style.zIndex = Math.round(z).toString()
    // 从主页面滑出时渐隐（offset 1 → 1.2 之间淡出）
    card.style.opacity = absOffset > 1 ? String(Math.max(0, 1 - (absOffset - 1) * 5)) : '1'
    card.style.transform =
      `translateY(${y.toFixed(2)}px) translateZ(${z.toFixed(2)}px) ` +
      `rotateX(${totalRotX.toFixed(2)}deg) rotateY(${totalRotY.toFixed(2)}deg) rotateZ(-3deg)`

    // 「进入功能」提示只在卡片居中（正面）时显示
    const cta = card.querySelector('.face-cta')
    if (cta) cta.style.opacity = centerFactor.toFixed(3)
  }
}

function setRef(el, i) {
  if (el) cardEls[i] = el
}

function frontBg(card) {
  return `linear-gradient(160deg, ${card.color} 0%, #0a0a0a 76%)`
}

onMounted(() => {
  calcMetrics()
  window.addEventListener('wheel', onWheel, { passive: false })
  window.addEventListener('mousemove', onMouseMove)
  document.addEventListener('mouseleave', onMouseLeave)
  window.addEventListener('resize', calcMetrics)

  const tick = () => {
    renderLoop()
    frameId = requestAnimationFrame(tick)
  }
  frameId = requestAnimationFrame(tick)
})

onBeforeUnmount(() => {
  cancelAnimationFrame(frameId)
  window.removeEventListener('wheel', onWheel)
  window.removeEventListener('mousemove', onMouseMove)
  document.removeEventListener('mouseleave', onMouseLeave)
  window.removeEventListener('resize', calcMetrics)
})
</script>

<template>
  <div class="carousel-wrap">
    <div class="carousel-scene">
      <div
        class="carousel-viewport"
        :style="{ width: metrics.cardW + 'px', height: metrics.cardH + 'px' }"
      >
        <div
          v-for="(card, i) in cards"
          :key="card.key"
          :ref="(el) => setRef(el, i)"
          class="card3d"
          :style="{ width: metrics.cardW + 'px', height: metrics.cardH + 'px' }"
          @click="onCardClick(card.key)"
        >
          <!-- 中间结构层（体积厚度） -->
          <div
            v-for="z in midLayers"
            :key="'m' + z"
            class="slice slice-mid"
            :style="{ transform: `translateZ(${z}px)` }"
          ></div>

          <!-- 正面：功能标识 + 进入提示 -->
          <div
            class="slice slice-face"
            :style="{ transform: `translateZ(${frontZ}px)`, background: frontBg(card) }"
          >
            <div class="face-content">
              <div class="face-icon" v-html="card.icon"></div>
              <div class="face-mid">
                <div class="face-name">{{ card.name }}</div>
                <div class="face-en">{{ card.en }}</div>
                <div class="face-desc">{{ card.desc }}</div>
              </div>
              <div class="face-cta">
                <span>进入功能</span>
                <span class="cta-arrow">→</span>
              </div>
            </div>
            <div class="face-corner"></div>
          </div>

          <!-- 背面：功能描述 -->
          <div
            class="slice slice-face slice-back"
            :style="{ transform: `translateZ(${backZ}px) rotateX(180deg)` }"
          >
            <div class="back-stripe"></div>
            <div class="back-content">
              <div class="back-key">{{ card.key.toUpperCase() }}</div>
              <div class="back-name">{{ card.name }}</div>
              <div class="back-desc">{{ card.desc }}</div>
            </div>
          </div>
        </div>
      </div>
    </div>
  </div>
</template>

<style scoped>
/* ============ 纯黑沉浸式 ============ */
.carousel-wrap {
  position: absolute;
  inset: 0;
  width: 100%;
  height: 100%;
  background: transparent;
  overflow: hidden;
  user-select: none;
  z-index: 2;
}
.carousel-scene {
  position: absolute;
  inset: 0;
  display: flex;
  align-items: center;
  justify-content: center;
  perspective: 1350px;
}
.carousel-viewport {
  position: relative;
  transform-style: preserve-3d;
}

/* ============ 卡片与体积厚度 ============ */
.card3d {
  position: absolute;
  inset: 0;
  transform-style: preserve-3d;
  cursor: pointer;
}
.slice {
  position: absolute;
  inset: 0;
  border-radius: 16px;
  overflow: hidden;
}
.slice-mid {
  background: #808080;
  border: 1px solid #808080;
}
.slice-face {
  backface-visibility: hidden;
  box-shadow: inset 0 1px 1px rgba(255, 255, 255, 0.15);
}
.slice-back {
  background: linear-gradient(160deg, #1a1a1a 0%, #050505 80%);
}

/* ============ 正面内容 ============ */
.face-content {
  position: absolute;
  inset: 0;
  padding: 34px 32px;
  display: flex;
  flex-direction: column;
  justify-content: space-between;
  color: #fff;
}
.face-icon {
  width: 52px;
  height: 52px;
  display: grid;
  place-items: center;
  border-radius: 13px;
  background: rgba(255, 255, 255, 0.12);
  backdrop-filter: blur(2px);
  color: #fff;
}
.face-icon :deep(svg) {
  width: 30px;
  height: 30px;
}
.face-mid {
  display: flex;
  flex-direction: column;
}
.face-name {
  font-size: 34px;
  font-weight: 700;
  letter-spacing: 0.01em;
  line-height: 1.1;
}
.face-en {
  font-size: 13px;
  letter-spacing: 0.22em;
  text-transform: uppercase;
  color: rgba(255, 255, 255, 0.66);
  margin-top: 2px;
}
.face-desc {
  font-size: 14px;
  color: rgba(255, 255, 255, 0.82);
  margin-top: 12px;
}
.face-cta {
  display: inline-flex;
  align-items: center;
  gap: 8px;
  align-self: flex-start;
  padding: 9px 18px;
  border-radius: 8px;
  background: rgba(255, 255, 255, 0.14);
  color: #fff;
  font-size: 14px;
  font-weight: 500;
  opacity: 0; /* 由 renderLoop 根据居中程度控制 */
}
.cta-arrow {
  font-size: 16px;
}
.face-corner {
  position: absolute;
  right: -46px;
  bottom: -46px;
  width: 150px;
  height: 150px;
  border-radius: 50%;
  background: rgba(255, 255, 255, 0.06);
}

/* ============ 背面内容 ============ */
.back-stripe {
  position: absolute;
  left: 0;
  right: 0;
  top: 26px;
  height: 34px;
  background: rgba(0, 0, 0, 0.85);
}
.back-content {
  position: absolute;
  left: 32px;
  top: 96px;
  right: 32px;
  color: #fff;
}
.back-key {
  font-family: "SFMono-Regular", Consolas, "Liberation Mono", monospace;
  font-size: 12px;
  letter-spacing: 0.18em;
  color: rgba(255, 255, 255, 0.45);
  margin-bottom: 8px;
}
.back-name {
  font-size: 26px;
  font-weight: 700;
  margin-bottom: 10px;
}
.back-desc {
  font-size: 14px;
  line-height: 1.6;
  color: rgba(255, 255, 255, 0.72);
}

/* ============ 响应式：窄屏卡片缩小 ============ */
@media (max-width: 900px) {
  .face-name { font-size: 26px; }
  .face-icon { width: 44px; height: 44px; }
  .face-icon :deep(svg) { width: 24px; height: 24px; }
}
</style>
