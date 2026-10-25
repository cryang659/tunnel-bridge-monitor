<script setup>
import { ref, onMounted, onUnmounted } from 'vue'
import { api } from '../api'
import { typeLabel, statusLabel } from '../labels'

const alarms = ref([])
let timer = null

async function refresh() {
  try {
    alarms.value = await api.getAlarms(200)
  } catch (e) {
    console.error(e)
  }
}

onMounted(() => {
  refresh()
  timer = setInterval(refresh, 5000)
})
onUnmounted(() => clearInterval(timer))
</script>

<template>
  <section class="card">
    <div class="card-head">
      <div>
        <div class="card-title">告警记录</div>
        <div class="card-sub">最近 200 条越限告警</div>
      </div>
    </div>
    <div class="card-body card-body--flush">
      <div v-if="alarms.length" class="table-wrap">
        <table>
          <thead>
            <tr>
              <th>时间</th>
              <th>设备</th>
              <th>类型</th>
              <th>值</th>
              <th>阈值</th>
              <th>级别</th>
            </tr>
          </thead>
          <tbody>
            <tr v-for="a in alarms" :key="a.id">
              <td class="muted">{{ a.timestamp }}</td>
              <td>{{ a.device_id }}</td>
              <td>{{ typeLabel(a.type) }}</td>
              <td>{{ a.value }}</td>
              <td>{{ a.threshold }}</td>
              <td><span class="badge" :class="a.level">{{ statusLabel(a.level) }}</span></td>
            </tr>
          </tbody>
        </table>
      </div>

      <div v-else class="empty">
        <div class="empty-icon">✅</div>
        <div class="empty-title">暂无告警</div>
        <div class="empty-hint">所有监测项均在正常范围内</div>
      </div>
    </div>
  </section>
</template>
