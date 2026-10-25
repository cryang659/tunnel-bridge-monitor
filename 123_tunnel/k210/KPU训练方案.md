# K210 裂缝识别 · KPU 模型训练方案

> ## ⚠️ 状态：**备用路线，当前暂停**（2026-09-26）
>
> **暂停原因：没有 TF 卡。** 实测确认：
> - KPU 硬件/固件 ✅ 好的（`from maix import KPU` 可导入，函数齐全）
> - Flash 内置模型 ❌ **没有**（`0x300000` 处是空的，精简固件）
> - 从 Flash 加载 ❌ **不支持**（固件**没有 `load()` 函数**，只有 `load_kmodel` 读文件）
> - TF 卡 ❌ **当前没有**
>
> → **三条加载途径全断，以现在的硬件条件跑不了 KPU。**
> → 当前用的是**传统 CV 方案**（见 `README.md` 第三节「分块最暗值对比」）。
> → 要转 KPU：**先买一张 TF 卡**（microSD，8G 够，FAT32），再按本文档走。
>
> ---
>
> **为什么值得做**：传统 CV 只能靠"比周围暗"这个粗糙特征，光照一复杂就误报。
> KPU 是神经网络硬件加速，能学到真正的外观特征，效果上限高得多。
> 但**前提是链路先通**——所以第 ① 步（验证）不能跳。

---

## 一、整体分两步走

| 阶段 | 内容 | 耗时 | 依赖 |
|---|---|---|---|
| **① 验证链路** | 找个**现成模型**跑通「摄像头→KPU→检测→LCD→串口」 | 30 分钟 | **TF 卡** + 从 MaixHub 下载一个现成 kmodel |
| **② 训练裂缝模型** | 采图 → 标注 → 训练 → 部署 | 半天~一天 | 需要你**拍裂缝照片** |

**先做①的理由**：训练要花半天到一天。如果先训练完才发现板子跑不了 KPU，
那半天就白费了。先用现成模型把链路验证掉。

---

## 二、阶段①：验证 KPU 链路

### 2.1 准备

1. **买一张 microSD 卡**（8G 足够），格式化成 **FAT32**
2. **从 MaixHub 模型库下载一个现成模型**：<https://maixhub.com/model/zoo>
   挑一个 K210 平台的 `.kmodel`（人脸检测 / 物体检测都行）
3. 把 `.kmodel` 拷到 TF 卡**根目录**
4. 插卡，重启 K210

### 2.2 验证

用 CanMV IDE 跑一段最小代码：

```python
import sensor, image, lcd
from maix import KPU as kpu

sensor.reset()
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.QVGA)
lcd.init()

# 模型路径换成你拷进去的文件名
task = kpu.load_kmodel("/sd/你的模型.kmodel")
print("模型加载 OK")

# anchor 必须用**下载模型时附带的那组**，不能照抄别人的
ANCHOR = (1.889, 2.5245, 2.9465, 3.94056, 3.99987,
          5.3658, 5.155437, 6.92275, 6.718375, 9.01025)
kpu.init_yolo2(task, 0.5, 0.3, 5, ANCHOR)
print("init_yolo2 OK")

while True:
    img = sensor.snapshot()
    objs = kpu.run_yolo2(task, img)
    if objs:
        for o in objs:
            img.draw_rectangle(o.rect(), color=(255, 0, 0), thickness=2)
            print("检测到: 置信=%.3f" % o.value())
    lcd.display(img)
```

| 结果 | 说明 |
|---|---|
| 打印 `模型加载 OK` + `init_yolo2 OK` | ✅ KPU 链路通 |
| 画面上出现红框 | ✅ **整条链路 OK**，可以进入阶段② |
| `load_kmodel` 报 `path error` | 卡没插好 / 文件名不对 / 不在根目录 |

### 2.3 参考：本固件的 KPU 实测情况

```
from maix import KPU      → ✅ 可用（import KPU 不行）
可用函数：
  load_kmodel, init_yolo2, run_with_output, regionlayer_yolo2,
  deinit, feature_compare, lp_recog, lp_recog_load_weight_data,
  sigmoid, softmax
⚠️ 没有 load() —— 只能从**文件**加载，不能从 Flash 地址加载
```

---

## 三、阶段②：训练裂缝模型

### 3.1 采图（**这步最关键，决定模型上限**）

**用 K210 自己拍**，不要用手机拍——镜头视角、分辨率、光照必须和实际部署一致。

要拍的内容：

| 类别 | 张数 | 说明 |
|---|---|---|
| **有裂缝** | 150~300 张 | 不同粗细、不同光照、不同角度、不同距离 |
| **无裂缝（背景）** | 50~100 张 | 干净的墙面/地面，**必须包含容易误报的场景** |
| 难例 | 尽量收 | 阴影、接缝、电线、污渍 —— 这些最容易被误判成裂缝 |

> ⚠️ **「无裂缝」的图必须拍够**，尤其要包含**光照不均、有阴影、有杂物**的场景。
> 模型最容易在这些地方误报，负样本能显著压低误报率。

> ⚠️ 每张图都要**尽量贴近真实使用场景**：如果实际是拍隧道壁，就别只拍桌上的纸。

### 3.2 标注（用 MaixHub 在线标注）

登录 <https://maixhub.com> → 模型训练 → 新建训练

| 步骤 | 操作 |
|---|---|
| 1. 创建项目 | 模型类型选 **图像检测**（不是分类！分类只能判断"有没有"，检测才能框出**在哪**） |
| 2. 创建数据集 | 数据类型=图像，标注类型=**检测** |
| 3. 上传图片 | 训练集 / 验证集分开上传 |
| 4. 标注 | 逐张框选裂缝，标签统一叫 `crack` |

**标注要点**：
- 框要**贴紧**裂缝，别带太多背景
- 同一类裂缝的框选标准要一致（别有的框整条缝、有的只框一段）
- 模糊、看不出是不是裂缝的图，**直接不放进训练集**（宁缺毋滥）

**验证集**：每类至少 5 张，且**不能和训练集重复**（否则评估结果虚高）。

### 3.3 训练

在 MaixHub 上点「创建训练任务」：

| 参数 | 建议 |
|---|---|
| 部署平台 | 选 **K210** |
| 模型网络 | 用页面**默认推荐**的（新手别改） |
| 图像增强 | 先保持默认 |
| 数据均衡 | 如果"有裂缝/无裂缝"数量差很多，**开启** |
| 负样本 | 如果误报多，添加 |

训练完在「训练记录」里看**验证集抽样结果**——如果框得不准，说明数据量不够或标注有问题，回去补。

### 3.4 部署

训练完下载模型包，里面含 `.kmodel` + 示例 `main.py` + **该模型的 anchor 参数**。

两种部署方式：

| 方式 | 做法 | 适用 |
|---|---|---|
| **放 SD 卡**（推荐） | 把 `.kmodel` 拷到 TF 卡根目录，代码里 `kpu.load("/sd/crack.kmodel")` | 简单，不用烧录 |
| 烧到 Flash | 用 kflash_gui 烧到 `0x300000` | 不用插卡，但要会烧录 |

### 3.5 接进正式程序

拿到模型后，改动**只有三处**（`main.py` 里已经预留好接口）：

```python
# 1) 顶部：换成你的模型路径 + 训练时给的 anchor
MODEL_PATH = "/sd/crack.kmodel"
ANCHOR = (1.889, 2.5245, ...)      # ← 训练输出里给的，必须照抄

# 2) 初始化时加载
task = kpu.load(MODEL_PATH)
kpu.init_yolo2(task, 0.5, 0.3, 5, ANCHOR)

# 3) detect_crack() 换成 KPU 推理
def detect_crack(img):
    objs = kpu.run_yolo2(task, img)
    if not objs:
        return (0, 0.0)
    best = max(o.value() for o in objs)     # 最高置信度
    return (int(best * 100), float(best))
```

**其余代码（串口协议、STM32 通信、显示）一行都不用动**——
协议里 `crack` 字段本来就是 0~100 的整数置信度。

---

## 四、为什么放弃传统 CV

一路实测下来，传统 CV 在这块板子 + 这个场景上走不通，记录如下（避免以后重走）：

| 方案 | 结果 | 原因 |
|---|---|---|
| Canny 边缘 → 数边缘像素占比 | ❌ | 裂缝是**细长线**，像素占比天然只有百分之几，永远够不到阈值 |
| LSD 找线段 → 量最长线段 | ❌ | ① K210 上极慢 ② **每帧返回几百个线段对象**，GC 追不上 → **跑一会儿内存耗尽卡死** |
| 全图统计量（mean/min/std） | ❌ | 白墙有**光照渐变**（中心亮四周暗），`min` 很低、`std` 很大，和真裂缝的数字撞车。**全图统计量看不见"局部"对比** |
| 黑帽变换（形态学） | ❌ | 实测本固件 **`MORPH_*` 常量全部不存在**，`img.morph()` 直接 `TypeError` |
| `get_statistics(thresholds=)` | ❌ | 返回语义与文档不符（白墙和裂缝返回同一个值），**不能靠猜 API 语义** |

**根本原因**：裂缝的特征是**局部**的（一小块区域比周围暗），而全图统计量是**全局**的，
天生看不见局部对比。要做局部对比就得用形态学或卷积——而这正是**神经网络擅长的事**。

**K210 的 KPU 是硬件加速器**，跑卷积几乎不耗 CPU，正好补上这块短板。

---

## 五、实测记录（供复现参考）

固件：`MicroPython v1.0.4-22-g0f1e00b-dirty` / `CanMV_Board with kendryte-k210`

| API | 可用性 |
|---|---|
| `machine.UART` + `fpioa_manager.fm` | ✅ |
| `sensor.set_auto_exposure/gain/whitebal(False)` | ✅ |
| `img.get_statistics(roi=...)` | ✅（`l_mean`/`l_min`/`l_stdev` 可用） |
| `img.get_statistics(thresholds=...)` | ⚠️ 能调用但**语义不明** |
| `img.binary([(lo,hi)])` | ✅ |
| `img.copy(x_scale=, y_scale=)` | ✅ 但**每帧分配**，慎用 |
| `image.Image((w,h), fmt)` | ✅ **第一参数必须是元组** |
| `img.draw_image(...)` | ✅ 但**没有 `roi` 参数**（那是 OpenMV 的） |
| `img.find_line_segments()` | ⚠️ 能用但**每帧分配几百对象 → 卡死** |
| `img.morph(...)` / `MORPH_*` | ❌ **全部不存在** |
| `machine.FPIOA` | ❌ 不存在（那是 K230 的，K210 用 `fpioa_manager`） |
| `from maix import KPU` | ✅ 可用（`import KPU` 不行） |
| `kpu.load()` | ❌ **不存在**——只能 `load_kmodel` 读文件 |
| `kpu.load_kmodel(path)` | ✅ 可用（需 TF 卡上有 .kmodel） |
| `kpu.init_yolo2 / run_yolo2` | ✅ 可用 |
| Flash `0x300000` 内置模型 | ❌ 空的（精简固件没烧模型） |

### 串口（与 STM32 通信）

```
请求  STM32 → K210 :  $Q#
应答  K210 → STM32 :  $K,<status>,<crack>,<vehicle>,<person>#
```
STM32 侧超时 **80ms**，所以 K210 必须用缓存的上次结果**立即应答**，不能等推理完成。
