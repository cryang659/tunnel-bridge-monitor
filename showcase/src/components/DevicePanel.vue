<script setup>
import { ref, onMounted } from 'vue'
import { store } from '../store'
import { api } from '../api'

const devices = ref([])
const msg = ref('')
const msgType = ref('')
const editing = ref(null)
const form = ref({ device_id: '', name: '', location: '', latitude: null, longitude: null })

async function load() {
  try {
    devices.value = await api.getDevices()
  } catch (e) {
    console.error(e)
  }
}

function resetForm() {
  form.value = { device_id: '', name: '', location: '', latitude: null, longitude: null }
  editing.value = null
}

function startEdit(d) {
  editing.value = d.device_id
  form.value = {
    device_id: d.device_id,
    name: d.name || '',
    location: d.location || '',
    latitude: d.latitude,
    longitude: d.longitude,
  }
}

function toNum(v) {
  return (v === '' || v === null || v === undefined) ? null : Number(v)
}

async function save() {
  if (!store.token) { msg.value = '请先登录再操作设备'; msgType.value = 'error'; return }
  if (!editing.value && !form.value.device_id) { msg.value = '请填写设备 ID'; msgType.value = 'error'; return }
  const payload = {
    name: form.value.name,
    location: form.value.location,
    latitude: toNum(form.value.latitude),
    longitude: toNum(form.value.longitude),
  }
  try {
    if (editing.value) {
      await api.updateDevice(editing.value, payload)
      msg.value = '设备已更新'
    } else {
      await api.addDevice({ ...payload, device_id: form.value.device_id })
      msg.value = '设备已添加'
    }
    msgType.value = 'success'
    resetForm()
    load()
  } catch (e) {
    msg.value = e.message
    msgType.value = 'error'
  }
}

async function remove(d) {
  if (!confirm(`确认删除设备 ${d.device_id}？`)) return
  try {
    await api.deleteDevice(d.device_id)
    msg.value = '设备已删除'
    msgType.value = 'success'
    load()
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
        <div class="card-title">设备管理</div>
        <div class="card-sub">管理监测节点的注册信息与经纬度</div>
      </div>
    </div>
    <div class="card-body">
      <div v-if="!store.token" class="tip">
        当前未登录，只能查看。点击右上角「登录」后可增删改设备。
      </div>
      <p v-if="msg" :class="msgType === 'error' ? 'msg-error' : 'msg-success'" style="margin-bottom: 14px">{{ msg }}</p>

      <div v-if="devices.length" class="table-wrap" style="margin-bottom: 20px">
        <table>
          <thead>
            <tr>
              <th>设备 ID</th>
              <th>名称</th>
              <th>位置</th>
              <th>纬度</th>
              <th>经度</th>
              <th>状态</th>
              <th>最后在线</th>
              <th>操作</th>
            </tr>
          </thead>
          <tbody>
            <tr v-for="d in devices" :key="d.device_id">
              <td>{{ d.device_id }}</td>
              <td>{{ d.name || '-' }}</td>
              <td>{{ d.location || '-' }}</td>
              <td>{{ d.latitude ?? '-' }}</td>
              <td>{{ d.longitude ?? '-' }}</td>
              <td><span class="badge" :class="d.online ? 'online' : 'offline'">{{ d.online ? '在线' : '离线' }}</span></td>
              <td class="muted">{{ d.last_seen || '-' }}</td>
              <td>
                <button class="btn btn-text btn-sm" @click="startEdit(d)" :disabled="!store.token">编辑</button>
                <button class="btn btn-text danger btn-sm" @click="remove(d)" :disabled="!store.token">删除</button>
              </td>
            </tr>
          </tbody>
        </table>
      </div>
      <div v-else class="empty">
        <div class="empty-icon">🖥️</div>
        <div class="empty-title">暂无设备</div>
        <div class="empty-hint">节点上报后会自动注册，也可以使用下方表单手动添加</div>
      </div>

      <h4 style="margin: 0 0 14px; font-size: 14px; font-weight: 600">
        {{ editing ? '编辑设备 · ' + editing : '添加设备' }}
      </h4>
      <div class="form-grid" style="margin-bottom: 16px">
        <div class="form-item">
          <label class="field-label">设备 ID</label>
          <input v-model="form.device_id" :disabled="!!editing" class="field" placeholder="如 NODE_003" />
        </div>
        <div class="form-item">
          <label class="field-label">名称</label>
          <input v-model="form.name" class="field" placeholder="节点名称" />
        </div>
        <div class="form-item">
          <label class="field-label">位置描述</label>
          <input v-model="form.location" class="field" placeholder="如：1 号桥墩" />
        </div>
        <div class="form-item">
          <label class="field-label">纬度</label>
          <input v-model="form.latitude" type="number" step="0.0001" class="field" placeholder="26.6500" />
        </div>
        <div class="form-item">
          <label class="field-label">经度</label>
          <input v-model="form.longitude" type="number" step="0.0001" class="field" placeholder="106.6300" />
        </div>
      </div>

      <div style="display: flex; gap: 10px">
        <button @click="save" :disabled="!store.token" class="btn btn-primary">{{ editing ? '保存修改' : '添加设备' }}</button>
        <button v-if="editing" @click="resetForm" class="btn btn-ghost">取消</button>
      </div>
    </div>
  </section>
</template>
