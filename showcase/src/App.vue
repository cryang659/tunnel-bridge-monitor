<script setup>
import { ref, computed } from 'vue'
import { store, setLogin, clearLogin } from './store'
import { api } from './api'
import NodeCarousel from './components/NodeCarousel.vue'
import DataBoard from './components/DataBoard.vue'
import MapView from './components/MapView.vue'
import Bridge3D from './components/Bridge3D.vue'
import Tunnel3D from './components/Tunnel3D.vue'
import AlarmList from './components/AlarmList.vue'
import ThresholdPanel from './components/ThresholdPanel.vue'
import DevicePanel from './components/DevicePanel.vue'

// 当前视图：home = 3D 卡片轮播首页，其余为六页功能
const view = ref('home')
// 3D 模型板块内部切换：桥梁 / 隧道
const model3d = ref('bridge')
const showLogin = ref(false)
const username = ref('')
const password = ref('')
const loginError = ref('')

const PAGES = {
  dashboard: '数据看板',
  map: 'GIS 地图',
  '3d': '3D 模型',
  alarm: '告警列表',
  threshold: '阈值管理',
  device: '设备管理',
}

const currentLabel = computed(() => PAGES[view.value] || '')

function navigate(key) {
  view.value = key
  showLogin.value = false
}

function goHome() {
  view.value = 'home'
}

async function doLogin() {
  try {
    const r = await api.login(username.value, password.value)
    setLogin(r.token, r.username)
    showLogin.value = false
    loginError.value = ''
    password.value = ''
  } catch (e) {
    loginError.value = e.message
  }
}

function logout() {
  clearLogin()
}
</script>

<template>
  <!-- ============ 首页：全屏 3D 卡片轮播（纯黑沉浸式） ============ -->
  <div v-if="view === 'home'" class="home">
    <NodeCarousel @navigate="navigate" />

    <header class="home-head">
      <div class="home-brand">
        <div class="home-logo">桥</div>
        <div class="home-title">
          <span class="home-name">隧道 / 桥梁结构健康监测系统</span>
          <span class="home-sub">TUNNEL &amp; BRIDGE · STRUCTURAL HEALTH MONITORING</span>
        </div>
      </div>
    </header>

    <footer class="home-foot">点击卡片进入对应功能模块</footer>
  </div>

  <!-- ============ 功能页 ============ -->
  <div v-else class="page">
    <header class="page-head">
      <button class="back-btn" @click="goHome">
        <span class="back-arrow">←</span> 返回
      </button>
      <div class="page-title">
        <span class="crumb">监测平台 / <b>{{ currentLabel }}</b></span>
        <h1>{{ currentLabel }}</h1>
      </div>
      <div class="page-actions">
        <template v-if="store.token">
          <span class="page-user">{{ store.username }}</span>
          <button class="btn btn-ghost btn-sm" @click="logout">退出登录</button>
        </template>
        <button v-else class="btn btn-primary btn-sm" @click="showLogin = !showLogin">登录</button>
      </div>
    </header>

    <div v-if="showLogin && !store.token" class="login-box">
      <h2>登录</h2>
      <div v-if="loginError" class="error">{{ loginError }}</div>
      <input v-model="username" class="field" placeholder="用户名" />
      <input v-model="password" class="field" type="password" placeholder="密码" @keyup.enter="doLogin" />
      <button class="btn btn-primary" style="width: 100%" @click="doLogin">登录</button>
    </div>

    <main class="page-body">
      <DataBoard v-if="view === 'dashboard'" />
      <MapView v-else-if="view === 'map'" />
      <div v-else-if="view === '3d'" class="model3d">
        <div class="model3d-tabs">
          <button :class="{ on: model3d === 'bridge' }" @click="model3d = 'bridge'">桥梁</button>
          <button :class="{ on: model3d === 'tunnel' }" @click="model3d = 'tunnel'">隧道</button>
        </div>
        <Bridge3D v-if="model3d === 'bridge'" class="model3d-view" />
        <Tunnel3D v-else class="model3d-view" />
      </div>
      <AlarmList v-else-if="view === 'alarm'" />
      <ThresholdPanel v-else-if="view === 'threshold'" />
      <DevicePanel v-else />
    </main>
  </div>
</template>

<style scoped>
/* ============ 首页：纯黑沉浸式 ============ */
.home {
  position: fixed;
  inset: 0;
  background: #000;
  overflow: hidden;
}
.home::before {
  content: "";
  position: absolute;
  inset: 0;
  background: url('/bg.jpg') center / cover no-repeat;
  z-index: 0;
}
.home::after {
  content: "";
  position: absolute;
  inset: 0;
  background: rgba(0, 0, 0, 0.45);
  z-index: 1;
}
.home-head {
  position: absolute;
  top: 0;
  left: 0;
  right: 0;
  padding: 28px 40px;
  pointer-events: none;
  z-index: 10;
}
.home-brand {
  display: flex;
  align-items: center;
  gap: 14px;
}
.home-logo {
  width: 40px;
  height: 40px;
  border-radius: 10px;
  background: linear-gradient(180deg, #2b6cd4 0%, #1554ad 100%);
  display: grid;
  place-items: center;
  color: #fff;
  font-weight: 700;
  font-size: 20px;
}
.home-title { line-height: 1.3; }
.home-name {
  display: block;
  color: #fff;
  font-size: 16px;
  font-weight: 600;
  letter-spacing: 0.5px;
}
.home-sub {
  display: block;
  font-size: 11px;
  letter-spacing: 0.16em;
  color: rgba(255, 255, 255, 0.5);
}
.home-foot {
  position: absolute;
  bottom: 26px;
  left: 0;
  right: 0;
  text-align: center;
  font-size: 13px;
  letter-spacing: 0.08em;
  color: rgba(255, 255, 255, 0.55);
  pointer-events: none;
  z-index: 10;
}

/* ============ 功能页 ============ */
.page {
  min-height: 100vh;
  background: var(--bg);
}
.page-head {
  position: sticky;
  top: 0;
  z-index: 100;
  height: 60px;
  padding: 0 28px;
  background: rgba(23, 23, 23, 0.92);
  backdrop-filter: blur(10px);
  -webkit-backdrop-filter: blur(10px);
  border-bottom: 1px solid var(--border);
  display: flex;
  align-items: center;
  gap: 20px;
}
.back-btn {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  padding: 8px 14px;
  border-radius: var(--radius-sm);
  border: 1px solid var(--border-strong);
  background: var(--surface);
  color: var(--text-secondary);
  font-size: 13px;
  font-weight: 500;
  cursor: pointer;
  white-space: nowrap;
  transition: color .18s ease, border-color .18s ease, background .18s ease;
}
.back-btn:hover {
  color: var(--primary);
  border-color: var(--primary-border);
  background: var(--primary-soft);
}
.back-arrow { font-size: 15px; }
.page-title { display: flex; flex-direction: column; flex: 1; min-width: 0; }
.page-title .crumb { font-size: 12px; color: var(--text-muted); }
.page-title .crumb b { color: var(--text-secondary); font-weight: 500; }
.page-title h1 { font-size: 17px; font-weight: 600; letter-spacing: -0.01em; }
.page-actions { display: flex; align-items: center; gap: 12px; }
.page-user { font-size: 13px; color: var(--text-secondary); }
.model3d { position: relative; width: 100%; height: calc(100vh - 150px); overflow: hidden; }
.model3d-view { width: 100%; height: 100%; }
.model3d-tabs {
  position: absolute; top: 12px; left: 50%; transform: translateX(-50%);
  z-index: 6; display: flex; gap: 6px; padding: 5px;
  background: rgba(18, 24, 36, 0.72); border-radius: 10px;
}
.model3d-tabs button {
  border: none; background: transparent; color: #c7cdd8;
  padding: 6px 20px; border-radius: 7px; cursor: pointer; font-size: 13px;
}
.model3d-tabs button.on { background: #1668dc; color: #fff; }
.page-body {
  max-width: var(--content-max);
  margin: 0 auto;
  padding: 24px 28px 40px;
}

@media (max-width: 640px) {
  .home-head { padding: 20px; }
  .home-name { font-size: 14px; }
  .page-head { padding: 0 16px; gap: 12px; }
}
</style>
