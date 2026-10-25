<script setup>
import { ref, onMounted, onUnmounted } from 'vue'
import { api } from '../api'
import { typeLabel, statusLabel, TYPE_UNITS } from '../labels'

const devices = ref([])
const stats = ref([])
const latest = ref({})
const loading = ref(true)
let timer = null

async function refresh() {
  try {
    const [devs, st] = await Promise.all([api.getDevices(), api.getStats()])
    devices.value = devs
    stats.value = st
    for (const d of devs) {
      const data = await api.getData({ device_id: d.device_id, limit: 9 })
      latest.value[d.device_id] = data
    }
  } catch (e) {
    console.error(e)
  } finally {
    loading.value = false
  }
}

onMounted(() => {
  refresh()
  timer = setInterval(refresh, 5000)
})
onUnmounted(() => clearInterval(timer))
</script>

<template>
  <div>
    <section class="card">
      <div class="card-head">
        <div>
          <div class="card-title">监测统计概览</div>
          <div class="card-sub">各类型传感器实时统计</div>
        </div>
      </div>
      <div class="card-body">
        <div v-if="loading" class="stat-grid">
          <div v-for="i in 6" :key="i" class="stat-card">
            <div class="skeleton skeleton-line" style="width: 55%"></div>
            <div class="skeleton skeleton-line" style="width: 80%; height: 26px; margin-top: 14px"></div>
            <div class="skeleton skeleton-line" style="width: 70%; margin-top: 14px"></div>
          </div>
        </div>

        <div v-else-if="!stats.length" class="empty">
          <div class="empty-icon">📊</div>
          <div class="empty-title">暂无统计数据</div>
          <div class="empty-hint">节点上报数据后，这里会自动生成统计概览</div>
        </div>

        <div v-else class="stat-grid">
          <div v-for="s in stats" :key="s.type" class="stat-card">
            <div class="stat-label">{{ typeLabel(s.type) }}</div>
            <div class="stat-value">{{ s.avg ?? '-' }}<span class="unit">{{ TYPE_UNITS[s.type] }}</span></div>
            <div class="stat-foot">范围 {{ s.min ?? '-' }} ~ {{ s.max ?? '-' }} · 样本 {{ s.count }}</div>
          </div>
        </div>
      </div>
    </section>

    <section class="card">
      <div class="card-head">
        <div>
          <div class="card-title">监测节点</div>
          <div class="card-sub">共 {{ devices.length }} 个节点</div>
        </div>
      </div>
      <div class="card-body">
        <div v-if="loading" class="device-grid">
          <div v-for="i in 3" :key="i" class="device-card">
            <div class="skeleton skeleton-line" style="width: 50%"></div>
            <div class="skeleton skeleton-line" style="width: 70%; margin-top: 12px"></div>
            <div class="skeleton skeleton-line" style="width: 100%; margin-top: 12px"></div>
            <div class="skeleton skeleton-line" style="width: 100%"></div>
            <div class="skeleton skeleton-line" style="width: 100%"></div>
          </div>
        </div>

        <div v-else-if="!devices.length" class="empty">
          <div class="empty-icon">🛰️</div>
          <div class="empty-title">暂无监测节点</div>
          <div class="empty-hint">节点上报后会自动注册，也可以在「设备管理」手动添加</div>
        </div>

        <div v-else class="device-grid">
          <div v-for="d in devices" :key="d.device_id" class="device-card">
            <div class="device-card-head">
              <span class="name">{{ d.name || d.device_id }}</span>
              <span class="id">{{ d.device_id }}</span>
            </div>
            <div class="device-card-meta">
              <span class="badge" :class="d.online ? 'online' : 'offline'">{{ d.online ? '在线' : '离线' }}</span>{{ d.location || '位置未设置' }} · 最后在线 {{ d.last_seen || '-' }}
            </div>
            <div class="metric-list">
              <template v-if="latest[d.device_id] && latest[d.device_id].length">
                <div v-for="row in latest[d.device_id]" :key="row.id" class="metric-row">
                  <span class="k">{{ typeLabel(row.type) }}</span>
                  <span class="v">
                    {{ row.value }}{{ row.unit }}
                    <span class="badge" :class="row.status">{{ statusLabel(row.status) }}</span>
                  </span>
                </div>
              </template>
              <div v-else class="muted">暂无数据</div>
            </div>
          </div>
        </div>
      </div>
    </section>
  </div>
</template>
