# GyroAim-SKSE 开发计划方案

日期：2026-10-14
状态：**v1.0 已定案**（D1–D8 全部定稿，可直接交实现 agent 执行）
前置调研（补充材料，存于会话工作区 files/）：`gyro-mod-survey.md`（同类 mod 调研）、`gamepad-support-survey.md`（手柄生态调研）——本规格已内含实现所需的关键结论，不依赖调研原文即可开工。

---

## 0. 需求与约束回顾

### 核心需求
1. 手柄陀螺仪瞄准：游戏特定状态（如拉弓瞄准）自动启用；
2. DLL 原生实现：陀螺仪相对位移直接旋转游戏相机/准星，**不是**映射到右摇杆/鼠标；
3. 兼容性：与 Steam Input 及现有手柄支持插件共存。

### 本轮新增约束
- **C1 叠加共存**：陀螺仪视角移动必须与右摇杆视角叠加同时工作（不接管、不屏蔽右摇杆）；
- **C2 兼容降级**：若某兼容目标无法低成本实现广泛兼容，则改为 **patch 形式、由用户选择安装**，不硬做万能兼容；
- **C3 增量交付**：从基本功能验证开始，逐阶段加入完整功能，每阶段有可玩验收；
- **C4 输入源配置化**：优先 Steam API；**暂不做自动回退**，由配置文件显式选择输入 API，选定源失败即明确报错并禁用功能。

### 明确排除
- VR 版（输入设备枚举不同、收益低）；
- 右摇杆映射/鼠标模拟路线（手感差，Steam 原生已可实现）；
- xinput.dll 代理、XInput 轮询 hook（dualpad 已验证是高冲突路线）。

---

## 1. 总体架构

```
┌─ GyroSource 层（配置选择，无自动回退）─────────────────┐
│  SteamInputSource   ISteamInput::GetMotionData        │
│  DsuSource          UDP 127.0.0.1:26760 (Cemuhook)   │
│  HidSource          SDL3 sensor / hidapi 直读          │
└──────────────┬────────────────────────────────────────┘
               ↓  统一采样格式 GyroSample { dt, angularVelocity, deltaQuat }
┌─ Processing 层 ───────────────────────────────────────┐
│  GamepadMotionHelpers：标定、传感器融合、Player-Space   │
│  灵敏度 / 死区 / 反转 / 低通平滑 / 俯仰钳制             │
└──────────────┬────────────────────────────────────────┘
               ↓  yaw/pitch 增量（弧度，逐帧）
┌─ Activation 层 ───────────────────────────────────────┐
│  拉弓检测：ActorState::GetAttackState() == kBowDrawn   │
│  可扩展：弩 / 法术蓄力 / 按键 Toggle                   │
└──────────────┬────────────────────────────────────────┘
               ↓  enabled ? 增量 : 0
┌─ Injection 层（相机，主线程）──────────────────────────┐
│  hook RE::TESCameraState::Update（SmoothCam 模式）      │
│  第三人称：ThirdPersonState::freeRotation / targetYaw   │
│  第一人称：FirstPersonState pitch + 玩家 yaw（1.1）     │
│  骑马：HorseCameraState 增量注入（1.1）                 │
│  与右摇杆输入叠加（C1），不触碰 ignoreKeyboardMouse     │
└──────────────┬────────────────────────────────────────┘
               ↓
┌─ Config / Compat 层 ──────────────────────────────────┐
│  INI 配置（inputSource、灵敏度、激活条件、hook 策略）    │
│  兼容 patch（可选安装）：针对特定 mod 的适配 DLL/策略    │
└────────────────────────────────────────────────────────┘
```

### 分层原则
- GyroSource 与 Injection 完全解耦：输入源只产数据，注入层只消费增量；
- Processing 与 Injection 之间只传"本帧视角增量"，天然支持与右摇杆叠加（右摇杆走原生链路，各自贡献、互不覆盖）；
- 兼容性问题按层隔离：与相机 mod 的冲突只影响 Injection 层（→ patch 化，C2），与手柄 mod 的冲突只影响输入语义（→ 保持 kGamepad 语义规避）。

---

## 2. 分阶段里程碑

### Phase 0 — 工程基线（工作量：S）
- 项目命名：改 `CMakeLists.txt` 的 `[BEGIN set_project_name]` 块 → `GyroAim`（DLL 随之改名）；
- SKSE 日志落地（spdlog，写 `Documents/My Games/.../SKSE/GyroAim.log`）；
- 配置文件读取骨架（格式见决策点 D3）；
- 帧驱动：`kDataLoaded` 后注册更新钩子/事件 sink；
- **验收**：`/W4` 零警告编译通过；游戏内日志每秒输出帧心跳、当前相机状态名、`GetAttackState()` 值。

### Phase 1 — 基本功能验证 / MVP（工作量：M，核心风险在此阶段暴露）
- **SteamInputSource**：初始化 ISteamInput（接入方式见 D4）、`GetConnectedControllers` + `GetMotionData`；
- **相机注入**：hook `RE::TESCameraState::Update`，把固定测试增量写入 `ThirdPersonState::freeRotation.x/y`（先硬编码小角度/帧，验证写入通路）；
- **拉弓触发**：`GetAttackState() == kBowDrawn` 时启用陀螺增量；
- **叠加验证（C1）**：拉弓时右摇杆继续可用，两者视角叠加；
- **验收**：拉弓 + 手腕转动 → 相机平滑转动；松开弓恢复常态；右摇杆与陀螺仪同时有效、互不吞噬。
- **风险预案**：若 `GetMotionData` 因 Steam 配置不出数，先用固定测试增量证明注入通路，再回头修数据源（分层设计允许两线独立排障）。

### Phase 1.1 — 功能拓展：第一人称与骑马相机（工作量：M，含原 R3 风险）
- 第一人称支持：yaw 走玩家朝向联动（`Actor::SetRotationZ`）；pitch 初版写 `FirstPersonState::targetPitchOffset`/`currentPitchOffset`（**Phase 2.1 实测修正**：offset 为每帧派生态不持久，真值是 `PlayerCharacter::data.angle.x`，上为负）；
- 骑马相机：`HorseCameraState`（`HandleLookInput` 已在 CommonLibSSE NG 确认存在）复用增量 post 注入；
- 激活条件沿用 D8（拉弓 / 配置常开）；按当前相机状态（第一/第三人称/骑马）自动分流注入目标；
- **验收**：第一人称、第三人称、骑马三种状态陀螺均可转视角，右摇杆叠加同时有效，启停切换无跳变、无累积漂移。

### Phase 2 — 手感完善（工作量：M）
- 接入 GamepadMotionHelpers（标定/融合/Player-Space）；
- 数据语义：`driftCorrectedQuat` 逐帧差量为主（Steam 文档明确推荐相机用法），`rotVel` 备选（见 D5）；
  - 实现注记（2026-10-14）：Steam V2 `driftCorrectedQuat` 截至 SDK 1.58a 无公开接口，落地时以 GMH 融合姿态 + 连续标定的逐帧差量等价实现 D5 语义；
- 灵敏度、反转轴、死区、低通平滑、俯仰钳制（防翻转）；
- 全部参数进配置文件；
- **验收**：长时间瞄准无明显漂移；手感可调至接近 Switch 版体感水准（主观基准）。

### Phase 2.1 — 手感问题修复（工作量：S，2026-10-14 实测反馈新增，含二轮修正）
- 灵敏度/抖动/静态漂移（一轮 + 二轮）：默认 `sensitivity` 逐轮下调 1.0 → 0.5 → **0.25**，`deadzoneDegPerSec` 1.0 → 2.5 → **4.0**，`smoothing` 0.5 → 0.65 → **0.7**；相关可调参数全部进配置（含 GMH 自动标定开关 `calibration`）；
- 视角反向排查（一轮）：根因为 Steam 运动数据为 Z-up（X = pitch, Y = roll, Z = yaw），与 GamepadMotionHelpers 的 Y-up（X = pitch, Y = yaw, Z = roll）约定不一致——平视时重力投影恰好自洽，俯仰接近垂直时玩家空间 yaw 的符号翻转导致左右反向；修复为在 Steam 源输出端做轴空间转换；
- 激活时机（一轮）：拉弓判定从 `== kBowDrawn` 改为 `kBowDraw..kBowNextAttack` 范围（扣下扳机即触发，与 IC/CMC 验证过的 OAR-proof 判定一致）；
- 第一人称修正（二轮）：yaw 实测与第三人称**反号**（增量改减）；pitch 写入点从 `targetPitchOffset`/`currentPitchOffset` 改为 **`PlayerCharacter::data.angle.x`**——offset 是引擎每帧从玩家俯仰派生的中间态（写入不持久，实测无效），`data.angle.x` 为第一人称俯仰真值且**上为负**（由 ImprovedCameraSE-NG 的 look-pitch 钳制源码实证）；
- 轴使用确认（二轮）：全程仅 **yaw + pitch 两个轴**作用于相机——任何注入路径都不写 roll；roll 分量只在 GMH 玩家空间投影内部作为 yaw 的几何输入参与（倾斜持握所需），不产生相机横滚；
- 第三人称 yaw 注入目标切换（三轮）：SmoothCam 源码实证 `UpdateRotation()` 按 `freeRotationEnabled` 门控——`freeRotation.x` 仅自由视角模式生效，武器拔出/瞄准态被丢弃或重置，统一解释"下视反向/快速旋转回拉/大俯仰受限"三症状；默认 `injectionMode` 切为 **`yawPitch`**（`targetYaw`/`currentYaw` 无条件被消费），`freeRotation` 模式保留可选；
- 分轴配置（三轮）：灵敏度与死区各自拆分为水平/垂直两项（`yawSensitivity`/`pitchSensitivity`、`yawDeadzoneDegPerSec`/`pitchDeadzoneDegPerSec`）；
- 第三人称准星修复（四轮）：`yawPitch`（写 `targetYaw`/`currentYaw`）实测为自由镜头——瞄准态准星跟随**玩家朝向**，只转相机角色不转。默认注入改为 `injectionMode = player`（yaw → `Actor::SetRotationZ` 转玩家本体、与 FP 同符号，pitch → `freeRotation.y`）；`freeRotation` 模式仅留档；旧值 `yawPitch` 自动映射为 `player`；
- 运动学可观测日志（四轮）：`logLevel = debug` 每秒输出 `motion accelMag/grav/out`，用于实测定案 posAccel 单位（9.8 = m/s²，1.0 = g）与重力估计质量（静止应 ≈ (0,-1,0)），以及死区前原始输出的漂移/滚转串扰；
- **回滚**（五轮，2026-10-14）：四轮 `player` 与三轮 `yawPitch` 注入均未通过实测（自由镜头/准星不随动/滚转驱动），整体**回滚至严重问题前的二轮基线**——第三人称恢复 `freeRotation` 增量，退役 `injectionMode`（遗留 ini 值忽略）；保留分轴配置与 debug 日志（行为无关）；"水平由滚转驱动"为回滚后仍开放的排查项，依赖 debug 运动学日志定案；
- **开放项根因定案**（六轮，2026-10-15，代码审查 + Steamworks SDK 注释实证）：两个开放项同源于**数据单位与标定 API 误用**——(a) `InputMotionData_t` 为 ±SHRT_MAX 原始量程（`posAccel`↔±2G、`rotVel`↔±2000°/s，SDK 注释明确），此前按 m/s²÷9.8、°/s 直通 → GMH 重力向量长度 ≈1670 → `CalculatePlayerSpaceGyro` 的 `min(|worldYaw|·relax, |gyroYZ|)` 永久饱和为 `|gyroYZ|`，滚转以满幅混入水平轴、符号由投影点积决定、俯仰过零（越过水平面）时滚转分量符号翻转——精确复现"滚转驱动水平 + 越过水平面反向"；(b) `StartContinuousCalibration()` 是 GMH 的手动标定窗口（把所有真实转动累进偏置均值并禁用静止/传感器融合自动标定），误当"持续漂移补偿"常开 → 静止漂移。修复：源内换算为 deg/s + g（`2000/32767`、`2/32767`），标定改用 `SetCalibrationMode(Stillness | SensorFusion)`；轴空间转换（Z-up→Y-up）经推导确认正确、保留不动；
- **验收**：回滚后行为与二轮一致（第一人称垂直/水平可用、第三人称基本手感）；六轮修复后待真机复测：静止无漂移、水平由偏航驱动、越过水平面不反向（复测时 `accelMag≈1.0`、`grav≈(0,-1,0)`）；
- **六轮复测通过 + 七轮微调**（2026-10-15）：真机确认静止无漂移、水平/垂直正常、灵敏度合理。遗留三项已修：(a) 垂直灵敏度默认 0.25 → **0.1875**（`[Feel]` 分轴独立调节，水平/垂直灵敏度、死区、反向均可分开设置）；(b) 第三人称水平反向——`freeRotation.x` 增量改减（FP 的 `SetRotationZ` 约定为正解）；(c) 骑马"只转视角不转准星、垂直不动"——TDM `HorseCameraState::UpdateRotation` 源码实证骑马相机为绕马轨道（yaw = 马头朝向 + `freeRotation.x`、pitch = `-freeRotation.y`），`freeRotation` 写入只动轨道视角；改为**瞄准注入**：yaw → `horse->SetRotationZ`（马头，同步 `horseCurrentDirection` 使镜头随动），pitch → `player->data.angle.x`（骑手瞄准俯仰，同 FP）。配置文件同步重组（`[General]/[Steam]/[Gyro]/[Feel]/[Activation]` + 每键一行注释，不兼容旧布局，测试时删 ini 重生成）。待复测：骑马准星是否随陀螺转动、马行进中马头写入是否被运动系统覆盖（若被覆盖则降级为 `horseCurrentDirection` 驱动或 patch 化）。

### Phase 4 — 功能扩展（工作量：M~L）
- 激活条件扩展：弩、法术蓄力、蹲伏、按键 Toggle/Hold（配置化）；
- **验收**：各激活条件下启用/停用切换无跳变、无累积漂移。

### Phase 5 — 兼容性与发布（工作量：M）
- 兼容测试矩阵（§5）；
- 按 D2 结论落实 patch 形态；
- 文档：安装、Steam 配置要求（**Gyro Behavior 设为 None**，避免与插件叠加出双倍视角）、配置项说明、已知不兼容列表；
- 运行时覆盖：SE 1.5.97 + AE 1.6.x + GOG（CommonLibSSE NG `RELOCATION_ID(SE,AE)` 双值 + REL 抽象），VR 明确不支持；
- **验收**：干净 MO2 环境 + 典型整合包环境各跑通一遍。

### Phase 3 — 多输入源（工作量：M，受 C4 约束）——**调整至末位，暂不计划实现（2026-10-14）**
- `DsuSource`：Cemuhook UDP（覆盖 DS4Windows 用户、Steam Deck + SteamDeckGyroDSU）；
- `HidSource`：SDL3 sensor API 或 hidapi 直读（覆盖无 Steam/裸设备）；
- 配置项 `inputSource = steam | dsu | hid`；**启动时校验选定源，失败 → 日志明确报错 + 功能禁用，不回退**；
- **验收**：三个源各自可用；切换配置重启生效；选错源时日志可诊断。

---

## 3. 新增约束的设计响应

### C1 右摇杆叠加（注入层选型结论：方案 1 相机层，post-注入）
- 陀螺增量**只加不减**：每帧把 `gyroDelta` 叠加到相机目标旋转，右摇杆原生输入链完全不动 → 摇杆 100% 遵循原有设置（死区/灵敏度/加速/平滑全部保留）；
- 陀螺在整形链**之后**注入（相机层字段），不经过任何摇杆整形 → 陀螺手感完全由本插件配置决定；
- **注入时机**：在相机状态 `Update()` detour 的 post 阶段（原函数执行完后）累加增量。这样无论 `HandleLookInput` 内部是累加还是覆盖式写 `targetYaw`，陀螺增量都是最后加上去的，不会被摇杆当帧写入吞噬；
- **已排除**：(a) 合成 `ThumbstickEvent` 并入输入链（兼容性差、经过摇杆整形）；(b) 写 `lookInputVec`（位于整形链中段，陀螺会继承手柄视角速度/加速等设置；逆向补偿脆弱：灵敏度设置一改就失真，加速曲线不可线性逆）；
- **fallback（仅在方案 1 找不到干净写入点时启用）**：自接管摇杆整形——hook `ProcessInput(device, rawX, rawY, deadzoneMin, deadzoneMax, outX, outY)` call-site（TDM `GamepadHook` 同款，`RELOCATION_ID(67497, 68808)` + 0x13F）或 `BSWin32GamepadDevice::Poll` 内 XInput 调用点（dualpad 路线），自行复刻死区/灵敏度/加速后与陀螺合成一个 look 向量。可行（双先例证明 hook 位置正确），复杂度 M~L：难点在整形复刻保真（死区参数 call-site 可得，但视角速度/加速/平滑分散在 `HandleLookInput`/相机状态/用户设置），复刻不完美即改变摇杆手感；且与 TDM/dualpad 类 mod hook 同一位置，冲突概率显著上升 → 大概率落回 C2 patch 化。不进 MVP；
- 需实测确认的边界：TDM/SmoothCam 也在视角链上 hook 时的叠加顺序（放进 Phase 5 测试矩阵，若冲突 → patch 化，C2）；拉弓瞄准状态下第三人称的生效字段是 `targetYaw/currentYaw` 还是 `freeRotation.x/y`（Phase 1 RE 验证点）。

### C2 兼容 patch 形式
- 架构上预留 hook 策略可插拔：注入层抽象为 `InjectionStrategy`（默认策略 = hook `TESCameraState::Update`）；
- 若与 SmoothCam / Improved Camera / TDM 的共存验证成本过高，则产出**针对性兼容 patch**（独立可选 DLL 或配置切换的备用策略），由用户按需安装，核心 DLL 不背万能兼容包袱；
- 优先级：只对"装机率高且确证冲突"的目标做 patch（预计候选：SmoothCam、TDM），其余写入已知不兼容列表。

### C3 增量交付
- 每 Phase 有独立可玩验收（§2），Phase 1 即可产出"能玩的拉弓陀螺瞄准"原型；
- 风险最高的两件事（相机写入通路、Steam Input 取数）都压在 Phase 1 尽早暴露。

### C4 输入源配置化
- `inputSource` 显式配置，无自动探测回退；启动校验失败即禁用 + 明确日志（"选定 steam 源不可用：SteamInput()->Init() 失败"）；
- 每个源的前置条件写进文档（steam 需 Steam 运行 + 配置支持开启；dsu 需服务端在 26760 端口；hid 需设备未被独占）。

---

## 4. 风险清单

| # | 风险 | 阶段 | 等级 | 缓解 |
|---|------|------|------|------|
| R1 | 与 SmoothCam/TDM 相机 hook 链叠加导致旋转被覆盖/打架 | P5 | 高 | hook `Update` 而非静态 patch；冲突即 patch 化（C2） |
| R2 | `GetMotionData` 在部分 Steam 配置下不出数（Gyro=None 时行为未验证） | P1 | 高 | Phase 1 先验证；若不成立，文档要求特定 Steam 配置或改用 rotVel/DSU 通道 |
| R3 | 第一人称相机旋转路径未逆向验证 | P1.1 | 中 | 专项 RE + `injectionMode` 式开关分流验证 |
| R4 | 陀螺漂移（quat heading 漂移、温度漂移） | P2 | 中 | driftCorrectedQuat 差量 + GamepadMotionHelpers 校准 + 按需重校准快捷键 |
| R5 | AE 1.6+ 虚表偏移（+2 vtblShift） | P1 | 低 | CommonLibSSE NG REL/RELOCATION_ID 双值抽象 |
| R6 | 帧率与手柄采样率不匹配抖动 | P2 | 低 | 累积均值 + 时间戳插值（JoyShockLibrary 模式） |
| R7 | 右摇杆叠加后俯仰钳制/俯仰死区冲突 | P2 | 低 | 增量式写入 + 钳制在增量层做 |
| R8 | Nexus 长尾存在未知同类 mod 的可能性 | — | 低 | 发布前人工检索一次 Nexus（gyro/motion control/Steam Deck） |

---

## 5. 兼容性测试矩阵（Phase 5）

| 对象 | 预期 | 验证点 |
|---|---|---|
| Steam Input（Gyro=None） | ✅ 必须 | 右摇杆/按键正常、gyro 数据可达 |
| Auto Input Switch | ✅ 必须 | 不触发键鼠/手柄提示切换 |
| SmoothCam | ⚠️ 可能 patch | 相机旋转不被覆盖、顺序正确 |
| True Directional Movement | ⚠️ 可能 patch | 目标锁定时陀螺行为合理（锁定时禁用？→ 配置项） |
| Improved Camera SE | ⚠️ 观察 | 第一人称阶段处理 |
| Gamepad++ / Button Freer | ✅ 预期无冲突 | 不依赖 ControlMap 语义即可规避 |
| SkyUI | ✅ 无冲突 | 纯 UI 层 |
| DS4Windows（HidHide） | ✅ 走 dsu 源 | UDP motion 可达 |
| GOG 版 / 无 Steam | ✅ 走 hid/dsu 源 | steam 源明确报错不崩溃 |

---

## 6. 决策记录（已定案 2026-10-14）

| # | 决策点 | 结论 |
|---|--------|------|
| D1 | 叠加注入位置 | **相机层 post-注入**（§3-C1）：hook `TESCameraState::Update` 并在原函数执行完后增量累加。摇杆零触碰（遵循全部原设置）、陀螺零整形（仅本插件配置）。已排除合成 `ThumbstickEvent`（兼容性差）与写 `lookInputVec`（陀螺会继承摇杆整形）；自接管摇杆整形仅作 fallback（复杂度 M~L，复刻保真难、与 TDM/dualpad 同点 hook 冲突面大），不进 MVP |
| D2 | 兼容 patch 形态 | **独立可选 DLL**（MO2 按需勾选）+ 配置级备用 hook 策略（`InjectionStrategy` 抽象内置）；只对"确证冲突且装机率高"的目标出 patch（候选：SmoothCam、TDM），其余写入已知不兼容列表 |
| D3 | 配置文件 | **INI**（SimpleIni，Skyrim 生态惯例）；MCM 调参留到发布后 |
| D4 | Steam API 接入 | **运行时动态加载 `steam_api64.dll`**（LoadLibrary/GetProcAddress），不静态链接；GOG/无 Steam 环境不因缺 DLL 崩溃，与 C4 显式选择哲学一致 |
| D5 | 陀螺数据语义 | **`driftCorrectedQuat` 逐帧差量**为主（Steam 文档明确推荐相机用法），`rotVel` 角速度备选 |
| D6 | MVP 范围 | **不含第一人称/骑马**（R3 未逆向验证，移至 Phase 1.1 专项处理） |
| D7 | 目标运行时 | **SE 1.5.97 + AE 1.6.x + GOG**（CommonLibSSE NG REL/`RELOCATION_ID(SE,AE)` 一次抽象全覆盖）；**VR 不支持** |
| D8 | 默认激活条件 | **MVP 仅拉弓**（`GetAttackState() == kBowDrawn`）；Phase 4 配置化扩展（弩/法术蓄力/蹲伏/按键 Toggle）；Phase 2.1 起改为 `kBowDraw..kBowNextAttack` 范围（扣下扳机即触发） |

## 7. 调研遗留未验证项（排期进对应 Phase）
- `InputMotionDataV2_t` 访问器确切名称（P1 开工时核对 Steamworks SDK 头）；
- Steam 配置 Gyro=None 时 `GetMotionData` 是否仍出数（P1，R2）；
- `BSPCGamepadDeviceHandler::InitializeDelegate` 是否绑定 Steam 后端（P1 观察）；
- Proton/Steam Deck Windows 环境下 hidapi 行为（P3）。

---

## 8. 实现交接说明（给实现 agent）

### 8.1 仓库现状
- 仓库是 SkyrimDev 的 CommonLibSSE NG "Hello World" 模板，全部插件代码在 `plugin.cpp`（现为打印 Hello World 的示例）；
- 项目改名：编辑 `CMakeLists.txt` 的 `[BEGIN set_project_name]` … `[END set_project_name]` 块（`HelloWorld` → `GyroAim`），DLL 名随之改变；其余构建区段不要动；
- 新增源文件加入 `add_commonlibsse_plugin(${PROJECT_NAME} SOURCES ...)` 的 SOURCES 列表；PCH（`PCH.h`）已接入，CommonLibSSE 头无需手动 include。

### 8.2 构建与验证循环（无测试套件、无 CI）
```sh
cmake --preset release        # 配置 + vcpkg 依赖安装（commonlibsse-ng 走 vcpkg-configuration.json 的固定 registry）
cmake --build build/release
cmake --preset debug && cmake --build build/debug
```
- **验收即干净编译**：`/W4` 全开，新警告视为失败；MSVC 专用 flags（`/permissive- /Zc:preprocessor`），不支持其他编译器；
- 格式化用 clang-format（仓库 `.clang-format`：Google 基底、4 空格、120 列）；
- 运行时验证用 `.vscode/launch.json` 的 **attach** 配置挂到已运行的 Skyrim；
- 设置 `SKYRIM_FOLDER` / `SKYRIM_MODS_FOLDER` 环境变量后构建产物会自动部署到游戏/mod 目录。

### 8.3 关键代码锚点（已在本地 CommonLibSSE NG 头文件核实）
| 用途 | 符号 / 位置 |
|---|---|
| 拉弓检测 | `RE::ActorState::GetAttackState()`、`WEAPON_STATE_ENUM::kBowDrawn = 10`（`RE/A/ActorState.h`）；辅以 `IsWeaponDrawn()` |
| 第三人称注入目标 | `RE::ThirdPersonState`（`RE/T/ThirdPersonState.h`）：`HandleLookInput(const NiPoint2&)`、`UpdateRotation()`、`targetYaw`/`currentYaw`（弧度）、`freeRotation.x/y`（NiPoint2，疑似 yaw/pitch）、`rotation`（NiQuaternion）；拉弓态下哪组字段生效是 Phase 1 RE 验证点 |
| 第一人称（Phase 1.1，2.1 修正） | yaw：`Actor::SetRotationZ`（与第三人称反号）；pitch：`PlayerCharacter::data.angle.x`（上为负，±89° 钳制；`targetPitchOffset` 为派生态已弃用） |
| 骑马（Phase 1.1） | `RE::HorseCameraState::HandleLookInput`（存在，已确认） |
| 帧驱动 / 相机 hook | 虚表 `RE::VTABLE_TESCameraState` 写 `Update`（detour 前后调用原函数，**增量注入放 post 阶段**）；或 SKSE 事件 sink 轮询（MVP 可先用 sink，稳定后换 detour） |
| 输入层参考 hook（fallback 用） | TDM `Hooks.h`：`LookHandler` vfunc 0x2 `ProcessThumbstick`/0x3 `ProcessMouseMove`（AE 1.6+ 需 +2 vtblShift）；摇杆整形 call-site `RELOCATION_ID(67497, 68808)` + 0x13F 处 `write_call`，签名 `ProcessInput(BSWin32GamepadDevice*, rawX, rawY, deadzoneMin, deadzoneMax, outX, outY)` |
| 输入设备管理 | `RE::BSInputDeviceManager`（`RE/B/BSInputDeviceManager.h`，`GetGamepad()/IsGamepadConnected()`）；`RE::PlayerControlsData::lookInputVec`（**只读参考，不注入**） |
| 相机旋转写法参考 | SmoothCam `SmoothCam/source/thirdperson.cpp` `SetCameraRotation`（写 `cameraRoot` NiMatrix3 + `ThirdPersonState::rotation`）；ImprovedCameraSE-NG `ImprovedCamera/source/cameras/ThirdPerson.cpp`（`freeRotation.x/y` 钳制实证） |
| 陀螺处理算法 | `JibbSmart/GamepadMotionHelpers`（MIT、header-only）：标定/传感器融合/Player-Space |
| Steam 数据 | `ISteamInput::GetMotionData`（`InputMotionData_t`：`rotQuat*`/`posAccel*`/`rotVel*`；新 SDK `InputMotionDataV2_t`：**相机用 `driftCorrectedQuat*` 逐帧差量**）；`RunFrame()` 由 `SteamAPI_RunCallbacks` 驱动，低延迟可每帧显式调 |
| 其他游戏同类实现 | `lud-berthe/Returnal-Native-Gyro`（MIT，原生相机注入 + Aim 激活）；`NaokoAF/HastyControls`（`GyroDelta` 叠加 `lookInput` 的完整范例） |

### 8.4 实现红线（违反即破坏兼容性）
1. **不碰** `ControlMap::ignoreKeyboardMouse`、不合成鼠标事件（会破坏手柄模式与 UI 提示）；
2. **不代理** `xinput.dll`、不 hook XInput 轮询主链（高冲突，dualpad 已淘汰该路线；仅 fallback 的 call-site hook 除外且需 patch 化评估）；
3. 陀螺增量一律**增量累加**、在原函数 **post 阶段**写入，绝不做绝对值覆盖（会吞噬右摇杆当帧贡献）；
4. 全程保持 `INPUT_DEVICE::kGamepad` 语义；
5. HID/Steam 读取在非主线程时，**不得在该线程碰游戏对象**，采样经队列回主线程消费（dualpad 约束）；
6. 俯仰钳制（防相机翻转）在陀螺增量层自行实现，勿依赖摇杆路径的钳制。

### 8.5 建议实现顺序（对应 §2 里程碑）
1. Phase 0：改名 `GyroAim`、日志、INI 读取骨架、帧心跳（验收：`/W4` 零警告 + 游戏内日志输出 `GetAttackState()`）；
2. Phase 1：SteamInputSource 取数 + 相机层固定测试增量注入 + 拉弓触发 + 右摇杆叠加验证（验收：拉弓手腕转动能转视角，摇杆同时可用）；
3. Phase 1.1：第一人称与骑马相机注入分流（验收：三种相机状态均可转视角、启停无跳变）；
4. 之后按 §2 顺序走 Phase 2 → 2.1 → 4 → 5，每阶段过完验收再进下一阶段；Phase 3（多输入源）已移至末位，暂不计划实现。
