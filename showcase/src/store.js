import { reactive } from 'vue'

// 全局登录状态（简单响应式，不用 Pinia）
export const store = reactive({
  token: localStorage.getItem('token') || '',
  username: localStorage.getItem('username') || '',
})

export function setLogin(token, username) {
  store.token = token
  store.username = username
  localStorage.setItem('token', token)
  localStorage.setItem('username', username)
}

export function clearLogin() {
  store.token = ''
  store.username = ''
  localStorage.removeItem('token')
  localStorage.removeItem('username')
}
