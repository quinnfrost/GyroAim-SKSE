# GyroAim 配置与初版说明（Phase 0–2.1）

对应 `GYROAIM-PLAN.md` 的 Phase 0（工程基线）、Phase 1（MVP）、Phase 1.1（第一人称/骑马）、Phase 2（手感完善）与 Phase 2.1（手感问题修复）实现。

## 部署

- `GyroAim.dll` → `<游戏>/Data/SKSE/Plugins/`（设置了 `SKYRIM_FOLDER` / `SKYRIM_MODS_FOLDER` 环境变量时构建会自动拷贝）；
- 配置文件 `GyroAim.ini` 与 DLL 同目录，首次启动自动生成默认配置；
- 日志写入 `Documents/My Games/<游戏目录>/SKSE/GyroAim.log`，每秒输出一条心跳：相机状态名、`attackState` 原始值、`bowAiming`、陀螺仪是否可用。
- **升级注意**：默认值只在 `GyroAim.ini` 首次生成时写入。从旧版本升级请删除旧 ini 让它重新生成，或手动对照下表更新数值。

## Steam 前置条件（inputSource = steam）

1. Steam 客户端必须运行，且手柄通过 Steam Input 连接（DS4 / DualSense / Switch Pro / Steam Deck 内置陀螺仪）；
2. Steam Input 中该手柄配置的 **Gyro Behavior 设为 None**，避免与本插件叠加出双倍视角移动；
3. 天赋树：游戏自带的 `steam_api64.dll` 来自 2016 年 SDK，不含 ISteamInput。若日志报 `lacks the ISteamInput flat API`，请把任意现代游戏/Steamworks SDK（1.51+）的 `steam_api64.dll` 拷到 `Data/SKSE/Plugins/`，并在 `[Steam] steamApiPath` 填写它（相对路径基于 SKSE/Plugins 目录）。

源初始化失败时插件**明确报错并禁用陀螺仪功能，不做自动回退**（约束 C4）。

## 配置项

```ini
[General]
inputSource = steam        ; 输入源：steam（dsu/hid 为 Phase 3 计划，当前填了会报错并禁用）
logLevel = info            ; trace | debug | info | warn | error（复现水平滚转问题时用 debug，见 Phase 2.1）

[Steam]
steamApiPath =             ; 留空 = 标准 DLL 搜索顺序

[Gyro]
gyroData = quat            ; quat = 融合姿态逐帧差量（D5 语义，默认）；rotVel = 角速度路径
gyroSpace = player         ; player | local | world（GamepadMotionHelpers 空间转换）
calibration = true         ; GMH 静止自动标定（漂移补偿）；false = 关闭
yawRelaxFactor = 1.41      ; player 空间的 yaw 放松系数（GMH 默认）
yawSensitivity = 0.25      ; 水平灵敏度（三轮起与垂直分开设置）
pitchSensitivity = 0.25    ; 垂直灵敏度
yawAxis = y                ; 仅 local 空间生效：偏航取哪个轴，x | y | z（GMH Y-up 约定 Y = yaw）
pitchAxis = x              ; 仅 local 空间生效：俯仰轴，x | y | z
invertYaw = false
invertPitch = false
yawDeadzoneDegPerSec = 4.0     ; 水平死区（deg/s，三轮起分开设置；漂移主要靠它截断）
pitchDeadzoneDegPerSec = 4.0   ; 垂直死区（deg/s）
smoothing = 0.7            ; 低通平滑：0 = 关闭，越接近 1 平滑越强
pitchClampDeg = 80.0       ; 俯仰钳制（陀螺增量层实现，防相机翻转）

[Activation]
enableOnBowDraw = true     ; true = 拉弓瞄准时启用，扣下扳机（kBowDraw 起）即触发；false = 常开（调试用）
```

### 第三人称注入（2026-10-14 回滚后基线）

当前固定为 **`freeRotation` 增量基线**：yaw/pitch 增量写 `ThirdPersonState::freeRotation.x/y`（原 `Update()` 后累加，红线 3）。`injectionMode` 配置键已退役——ini 里遗留的 `player` / `yawPitch` / `freeRotation` 值一律被忽略，不产生行为差异。

历史记录（均已退役）：`yawPitch`（写 `targetYaw`/`currentYaw`）实测为自由镜头、准星不随动；`player`（写 `Actor::SetRotationZ` 转角色）在四轮测试中未通过，连同其默认值一起回滚。

## Phase 1.1：第一人称与骑马相机

按当前相机状态自动分流注入目标（`injectionMode` 只作用于第三人称/骑马）：

| 相机状态 | yaw 写入 | pitch 写入 |
|---|---|---|
| 第三人称 | `freeRotation.x` 或 `targetYaw`/`currentYaw` 增量 | `freeRotation.y` 增量 |
| 骑马（kMount） | 同第三人称 | 同第三人称 |
| 第一人称 | `Actor::SetRotationZ` 增量，**与第三人称反号**（2.1 实测修正） | `PlayerCharacter::data.angle.x` 增量（FP 俯仰真值，**上为负**，±89° 钳制；2.1 实测修正，原 `targetPitchOffset` 为派生态已弃用） |

运行时验证点（对应 plan R3）：

- 第一人称 pitch 若方向相反，调 `invertPitch`（FP 与 TP 共用该开关，注意会同时翻转第三人称）；
- 骑马相机沿用第三人称 `player` 模式（写玩家朝向）；马匹是否随动待 Phase 1.1 验收复核，无反应可切 `injectionMode = freeRotation` 对比。

## Phase 2：手感完善（GamepadMotionHelpers）

- 接入 vendored 的 [GamepadMotionHelpers](../src/ThirdParty/GamepadMotionHelpers/GamepadMotion.hpp)（MIT，header-only）：
  - **标定**：`StartContinuousCalibration()` 静止漂移自动补偿，陀螺静止时持续收敛；
  - **传感器融合**：`ProcessMotion()` 每帧喂入角速度 + 加速度（g），输出重力方向与融合姿态；
  - **空间转换**：`gyroSpace = player`（默认，重力辅助 yaw，防漂移）/ `world` / `local`（轴映射仅 local 模式生效）。
- 数据语义（D5）：`gyroData = quat` 用融合姿态逐帧差量（conj(q_prev)·q_cur 旋转矢量，deg）作为视角增量，天然帧率无关；`gyroData = rotVel` 走校准后角速度 × dt。
- **D5 偏差说明**：Steam 新 SDK 的 `InputMotionDataV2_t::driftCorrectedQuat` 截至 SDK 1.58a **无公开接口可取**（`ISteamInput::GetMotionData` 只返回 V1 结构）；GMH 融合姿态 + 连续标定在本地实现了等价的"漂移校正姿态差量"，验收目标（长时间无明显漂移）不变。
- 单位约定：Steam `rotVel` 按 deg/s 使用、`posAccel` 为 m/s²（源内换算为 g）；Steam 原生轴为 Z-up（X = pitch、Y = roll、Z = yaw），**在 Steam 源输出端转换为 GMH 的 Y-up 约定（X = pitch、Y = yaw、Z = roll）**，处理管线全程使用 GMH 空间。

## Phase 2.1：手感问题修复（2026-10-14 实测反馈，含二轮修正）

- **灵敏度/抖动/漂移**（一轮 + 二轮）：默认 `sensitivity` 1.0 → 0.5 → **0.25**、`deadzoneDegPerSec` 1.0 → 2.5 → **4.0**、`smoothing` 0.5 → 0.65 → **0.7**；新增 `[Gyro] calibration` 开关 GMH 静止自动标定。所有旋钮均可在 ini 调整（注意旧 ini 不会自动更新，见部署一节）。
- **上下视左右反向**（一轮）：根因是把 Steam 的 Z-up 运动数据直接喂给了按 Y-up 约定设计的 GMH 玩家空间投影——平视时重力落在 Z 轴上恰好自洽，俯仰接近垂直时重力转到 Y 轴、投影符号翻转，表现为左右反向。已在 Steam 源输出端做基变换 `(x, y, z) → (x, z, -y)`（角速度/加速度/四元数同步转换），三轴几何一致后各俯仰角符号连续。
- **触发时机**（一轮）：激活判定由 `== kBowDrawn`（完全拉满才触发）改为 `kBowDraw..kBowNextAttack` 范围——扣下扳机开始拉弓即触发；心跳日志字段更名 `bowAiming`。
- **第一人称**（二轮）：
  - 水平反向 → yaw 增量改减（`data.angle.z - yaw`，与第三人称字段约定相反，实测定案）；
  - 上下不动 → pitch 写入点改为 `PlayerCharacter::data.angle.x`（IC 源码实证这是 FP 俯仰真值、上为负）；原 `targetPitchOffset`/`currentPitchOffset` 是引擎每帧从玩家俯仰派生的中间态，post 写入下帧即被重算，实测无效，已弃用。
- **轴使用确认**（二轮）：所有注入路径只消费 yaw 与 pitch 两个轴，**不写入任何 roll/横滚**；roll 分量仅在 GMH 玩家空间投影内部作为求取水平转速的几何输入（倾斜持握所需），不会让相机产生横滚。
- **第三人称 yaw 注入目标切换**（三轮）：SmoothCam 源码实证 `ThirdPersonState::UpdateRotation()` 按 `freeRotationEnabled` 门控合成相机旋转——`freeRotation.x` 仅在自由视角模式（武器收起）生效，武器拔出/瞄准态会被门控丢弃或重置。该机制统一解释三轮反馈的三个症状（以下视为准：下视反向、快速旋转后回拉、大俯仰受限均为 free-mode 门控副作用）。默认 `injectionMode` 切换为 **`yawPitch`**（写 `targetYaw`/`currentYaw`，无条件被消费的规范偏航状态）；`freeRotation` 模式保留可选。
- **分轴配置**（三轮）：`sensitivity` 拆分为 `yawSensitivity` / `pitchSensitivity`，`deadzoneDegPerSec` 拆分为 `yawDeadzoneDegPerSec` / `pitchDeadzoneDegPerSec`（漂移截断主要依赖水平死区）。
- **第三人称准星修复**（四轮）：三轮的 `yawPitch` 模式（写 `targetYaw`/`currentYaw`）实测把陀螺变成了"自由镜头"——瞄准态**准星跟随玩家朝向**而非相机字段，只转相机不转角色准星就不动。四轮起默认 `injectionMode = player`：yaw 写 `Actor::SetRotationZ`（转玩家本体，与 FP 同符号），pitch 仍写 `freeRotation.y`；`freeRotation` 模式仅留档。
- **运动学可观测日志**（四轮）：`logLevel = debug` 时每秒输出 `motion accelMag=… grav=… out=…`——`accelMag≈9.8` 说明 Steam `posAccel` 为 m/s²（当前换算正确），`≈1.0` 则说明已是 g（需去除换算）；静止平放时 `grav≈(0,-1,0)`，偏离即重力估计异常（滚转混入 yaw / 方向反向的嫌疑源）；`out` 为死区前的管线原始 yaw/pitch 输出（deg/帧），可直接看漂移与滚转串扰。
- **回滚至严重问题前版本**（五轮，2026-10-14）：第三人称注入回滚为 `freeRotation` 增量基线，退役 `injectionMode`（`yawPitch`/`player` 及其默认值全部移除，遗留 ini 值忽略）。**保留**两项与行为无关的四轮产物：分轴配置（`yawSensitivity`/`pitchSensitivity`、`yawDeadzoneDegPerSec`/`pitchDeadzoneDegPerSec`，默认值与回滚目标一致）与 debug 运动学日志（用于继续定案"水平由滚转驱动"问题）。第一人称（`data.angle.x` pitch + 反号 yaw）与回滚目标版本相同，无需变更。

## 当前范围与已知限制

- 第一人称、第三人称、骑马三种相机状态均已支持（Phase 1.1）；拉弓扣扳机即激活（Phase 2.1，D8 扩展于 Phase 4）；
- 陀螺数据默认走 GMH 融合姿态差量（`gyroData = quat`）；`rotVel` 为备选路径。Steam V2 `driftCorrectedQuat` 无公开接口，等价能力由 GMH 本地实现（见 Phase 2 一节）；
- 输入源仅 `steam`；`dsu` / `hid` 为 Phase 3；
- 与 SmoothCam/TDM 共存为 Phase 5 测试矩阵项，冲突则按 C2 patch 化。
