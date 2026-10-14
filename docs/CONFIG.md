# GyroAim 配置与初版说明（Phase 0–1）

对应 `GYROAIM-PLAN.md` 的 Phase 0（工程基线）与 Phase 1（MVP）实现。

## 部署

- `GyroAim.dll` → `<游戏>/Data/SKSE/Plugins/`（设置了 `SKYRIM_FOLDER` / `SKYRIM_MODS_FOLDER` 环境变量时构建会自动拷贝）；
- 配置文件 `GyroAim.ini` 与 DLL 同目录，首次启动自动生成默认配置；
- 日志写入 `Documents/My Games/<游戏目录>/SKSE/GyroAim.log`，每秒输出一条心跳：当前相机状态名、`GetAttackState()` 原始值、陀螺仪是否可用、`freeRotation` 当前值。

## Steam 前置条件（inputSource = steam）

1. Steam 客户端必须运行，且手柄通过 Steam Input 连接（DS4 / DualSense / Switch Pro / Steam Deck 内置陀螺仪）；
2. Steam Input 中该手柄配置的 **Gyro Behavior 设为 None**，避免与本插件叠加出双倍视角移动；
3. 天赋树：游戏自带的 `steam_api64.dll` 来自 2016 年 SDK，不含 ISteamInput。若日志报 `lacks the ISteamInput flat API`，请把任意现代游戏/Steamworks SDK（1.51+）的 `steam_api64.dll` 拷到 `Data/SKSE/Plugins/`，并在 `[Steam] steamApiPath` 填写它（相对路径基于 SKSE/Plugins 目录）。

源初始化失败时插件**明确报错并禁用陀螺仪功能，不做自动回退**（约束 C4）。

## 配置项

```ini
[General]
inputSource = steam        ; 输入源：steam（dsu/hid 为 Phase 3 计划，当前填了会报错并禁用）
injectionMode = freeRotation ; 相机注入模式，见下表
logLevel = info            ; trace | debug | info | warn | error

[Steam]
steamApiPath =             ; 留空 = 标准 DLL 搜索顺序

[Gyro]
sensitivity = 1.0          ; 总灵敏度倍率
yawAxis = y                ; 偏航取原始角速度的哪个轴：x | y | z
pitchAxis = x              ; 俯仰轴：x | y | z
invertYaw = false
invertPitch = false
deadzoneDegPerSec = 1.0    ; 角速度模长低于该值（deg/s）时忽略
smoothing = 0.5            ; 低通平滑：0 = 关闭，越接近 1 平滑越强
pitchClampDeg = 80.0       ; 俯仰钳制（陀螺增量层实现，防相机翻转）

[Activation]
enableOnBowDraw = true     ; true = 仅拉弓瞄准时启用（决策 D8）；false = 常开（调试用）
```

### injectionMode（Phase 1 运行时验证开关）

拉弓态下第三人称相机实际生效的旋转字段尚需实测（plan §8.3 验证点），故提供两种模式：

| 模式 | 写入目标 | 说明 |
|---|---|---|
| `freeRotation` | `freeRotation.x/y` 增量 | 计划书默认（ImprovedCameraSE 实证字段） |
| `yawPitch` | `targetYaw` + `currentYaw` 增量（yaw）、`freeRotation.y`（pitch） | 若 `freeRotation` 模式拉弓时无反应，切到此模式复测 |

两种模式都只做**增量累加**且在原 `Update()` 之后写入（红线 3），右摇杆输入链完全不被触碰（约束 C1）。

## 当前范围与已知限制

- 仅第三人称（MVP，D6：第一人称为 Phase 4）；仅拉弓激活（D8，Phase 4 扩展弩/法术/蹲伏/按键）；
- 陀螺数据用 `GetMotionData` 的 `rotVel`（角速度）；`driftCorrectedQuat` 逐帧差量语义为 Phase 2（D5），届时接入 GamepadMotionHelpers 做标定与 Player-Space；
- 输入源仅 `steam`；`dsu` / `hid` 为 Phase 3；
- 与 SmoothCam/TDM 共存为 Phase 5 测试矩阵项，冲突则按 C2 patch 化。
