<script setup>
import { ref, onMounted } from 'vue'
import { store } from '../store'
import { api } from '../api'
import { typeLabel, TYPE_UNITS } from '../labels'

const thresholds = ref({})
const edited = ref({})
const msg = ref('')
const msgType = ref('')

async function load() {
  try {
    thresholds.value = await api.getThresholds()
    edited.value = { ...thresholds.value }
  } catch (e) {
    console.error(e)
  }
}

async function save() {
  if (!store.token) {
    msg.value = '请先登录再修改阈值'
    msgType.value = 'error'
    return
  }
  try {
    await api.setThresholds(edited.value)
    thresholds.value = { ...edited.value }
    msg.value = '阈值已保存'
    msgType.value = 'success'
  } catch (e) {
    msg.value = e.message
    msgType.value = 'error'
  }
}

onMounted(load)
</script>

<template>
  <section class="card">
    <div class="card-head">
      <div>
        <div class="card-title">告警阈值配置</div>
        <div class="card-sub">越限即触发告警，预警线为阈值的 80%</div>
      </div>
    </div>
    <div class="card-body">
      <div v-if="!store.token" class="tip">
        当前未登录，只能查看。点击右上角「登录」后可修改阈值。
      </div>
      <p v-if="msg" :class="msgType === 'error' ? 'msg-error' : 'msg-success'" style="margin-bottom: 14px">{{ msg }}</p>

      <div class="table-wrap" style="margin-bottom: 16px">
        <table>
          <thead>
            <tr>
              <th>传感器类型</th>
              <th>告警阈值</th>
              <th>说明</th>
            </tr>
          </thead>
          <tbody>
            <tr v-for="(v, type) in edited" :key="type">
              <td>{{ typeLabel(type) }} <span class="muted">({{ TYPE_UNITS[type] }})</span></td>
              <td>
                <input v-model.number="edited[type]" type="number" step="0.1" class="field" style="width: 140px" />
              </td>
              <td class="muted">预警 = 阈值 × 0.8</td>
            </tr>
          </tbody>
        </table>
      </div>

      <button @click="save" :disabled="!store.token" class="btn btn-primary">保存阈值</button>
    </div>
  </section>
</template>
