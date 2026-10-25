import { store } from './store'

const BASE = '/api'

async function request(method, path, data) {
  const headers = { 'Content-Type': 'application/json' }
  if (store.token) headers['Authorization'] = 'Bearer ' + store.token
  const resp = await fetch(BASE + path, {
    method,
    headers,
    body: data !== undefined ? JSON.stringify(data) : undefined,
  })
  if (resp.status === 401) throw new Error('未登录或登录已过期')
  const json = await resp.json().catch(() => ({}))
  if (!resp.ok) throw new Error(json.detail || '请求失败')
  return json
}

function toQuery(params) {
  const qs = new URLSearchParams()
  Object.keys(params || {}).forEach((k) => {
    const v = params[k]
    if (v !== undefined && v !== null && v !== '') qs.append(k, v)
  })
  const s = qs.toString()
  return s ? '?' + s : ''
}

export const api = {
  login: (username, password) => request('POST', '/auth/login', { username, password }),
  getDevices: () => request('GET', '/devices'),
  addDevice: (d) => request('POST', '/devices', d),
  updateDevice: (id, d) => request('PUT', `/devices/${id}`, d),
  deleteDevice: (id) => request('DELETE', `/devices/${id}`),
  getData: (p) => request('GET', '/data' + toQuery(p)),
  getAlarms: (limit = 200) => request('GET', `/alarms?limit=${limit}`),
  getStats: (p) => request('GET', '/stats' + toQuery(p)),
  getThresholds: () => request('GET', '/thresholds'),
  setThresholds: (t) => request('PUT', '/thresholds', { thresholds: t }),
}
