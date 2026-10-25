// 传感器类型 / 状态的统一中文映射（与后端物模型字段对齐）

export const TYPE_LABELS = {
  vibration: '振动',
  stress: '应力',
  temperature: '温度',
  humidity: '湿度',
  // ↓ 以下 4 条是**拆分前的旧类型名**，现已被 _1/_2 取代，不再上报。
  //   保留是为了 data 表里的历史数据——删掉的话老记录会显示成英文 key `deck_temp`、
  //   单位列还会变 undefined。同理，新类型上线后也不要删旧条目。
  water_level: '水位',
  seepage: '渗水',
  deck_temp: '桥面温度',
  pier_temp: '桥墩温度',
  seepage_1: '渗水1',
  seepage_2: '渗水2',
  displacement: '位移',
  load: '荷载',
  cable_force: '索力',
  deck_temp_1: '桥面1',
  deck_temp_2: '桥面2',
  pier_temp_1: '桥墩1',
  pier_temp_2: '桥墩2',
  convergence: '净空收敛',
  // 2026-09-17 隧道板新增
  temperature_1: '温度1',
  temperature_2: '温度2',
  humidity_1: '湿度1',
  humidity_2: '湿度2',
  water_level_1: '水位1',
  water_level_2: '水位2',
  dome_pressure: '穹顶压力',
  // 2026-09-26 隧道板新增：K210 视觉（裂缝为实际实现，车辆/人员为预留）
  crack: '裂缝',
  vehicle: '车辆',
  person: '人员',
  // 2026-09-25 桥板新增：RCWL-1670 超声波（既驱动灯带跟随，也上报）
  position: '物体位置',
}

export const TYPE_UNITS = {
  vibration: 'g',
  stress: 'με',
  temperature: '℃',
  humidity: '%',
  water_level: 'm',
  // 历史数据用的旧类型名（对应上面 TYPE_LABELS 里那几条）
  seepage: '%',
  deck_temp: '℃',
  pier_temp: '℃',
  seepage_1: '%',
  seepage_2: '%',
  displacement: 'mm',
  load: 't',
  cable_force: 'kN',
  deck_temp_1: '℃',
  deck_temp_2: '℃',
  pier_temp_1: '℃',
  pier_temp_2: '℃',
  convergence: 'mm',
  // 2026-09-17 隧道板新增
  temperature_1: '℃',
  temperature_2: '℃',
  humidity_1: '%',
  humidity_2: '%',
  water_level_1: 'm',
  water_level_2: 'm',
  dome_pressure: 'kPa',
  // 2026-09-26 K210 视觉：裂缝为置信度，无量纲
  crack: '',
  vehicle: '辆',
  person: '人',
  // 2026-09-25 桥板超声波：物体位置，单位 cm
  position: 'cm',
}

export const STATUS_LABELS = {
  normal: '正常',
  warning: '预警',
  alarm: '告警',
}

export function typeLabel(t) {
  return TYPE_LABELS[t] || t
}

export function statusLabel(s) {
  return STATUS_LABELS[s] || s
}
