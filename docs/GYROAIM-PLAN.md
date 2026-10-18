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
- **C4 输入源配置化**：优先 Steam API，由配置文件显式选择输入 API。（2026-10-18 修订：启用 `steam → hid` 回退链，见 Phase 3——取代原"选定源失败即禁用"。）

### 明确排除
- VR 版（输入设备枚举不同、收益低）；
- 右摇杆映射/鼠标模拟路线（手感差，Steam 原生已可实现）；
- xinput.dll 代理、XInput 轮询 hook（dualpad 已验证是高冲突路线）。

---

## 1. 总体架构

```
┌─ GyroSource 层（配置优先 + steam→hid 回退链）──────────┐
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
- **六轮复测通过 + 七轮微调**（2026-10-15）：真机确认静止无漂移、水平/垂直正常、灵敏度合理。遗留三项已修：(a) 垂直灵敏度默认 0.25 → **0.1875**（`[Feel]` 分轴独立调节，水平/垂直灵敏度、死区、反向均可分开设置）；(b) 第三人称水平反向——`freeRotation.x` 增量改减（FP 的 `SetRotationZ` 约定为正解）；(c) 骑马"只转视角不转准星、垂直不动"——TDM `HorseCameraState::UpdateRotation` 源码实证骑马相机为绕马轨道（yaw = 马头朝向 + `freeRotation.x`、pitch = `-freeRotation.y`），`freeRotation` 写入只动轨道视角；改为**瞄准注入**：yaw → `horse->SetRotationZ`（马头，同步 `horseCurrentDirection` 使镜头随动），pitch → `player->data.angle.x`（骑手瞄准俯仰，同 FP）。配置文件同步重组（`[General]/[Steam]/[Gyro]/[Feel]/[Activation]` + 每键一行注释，不兼容旧布局，测试时删 ini 重生成）。待复测：骑马准星是否随陀螺转动、马行进中马头写入是否被运动系统覆盖（若被覆盖则降级为 `horseCurrentDirection` 驱动或 patch 化）；
- **八轮：骑马俯仰改走原生视角通道**（2026-10-15）：七轮 yaw（马头）实测通过、俯仰仍无效——双写 `freeRotation.y` + `data.angle.x` 后骑手有上下动作但准星/镜头不动（产物校验排除陈旧构建）。判定：骑马瞄准态的相机俯仰不派生自这两个字段（`freeRotation` 疑被门控/重置，`data.angle.x` 仅驱动骑手动画）。俯仰改为调用 `HorseCameraState::HandleLookInput`（TDM hook 的同一入口 = 右摇杆俯仰原生通道），新增 `[Feel] horseLookScale` 校准输入单位（负值反向）；debug 日志输出各字段 pre/post 快照——若 pre ≠ 上帧 post，即为外部 mod 覆盖写入，按 C2 转 patch 路线；
- **九轮：骑马俯仰单位标定定案**（2026-10-15，日志实证）：诊断日志一次性定案——`HandleLookInput` 通道正常（输入 → `freeRotation.y` 线性，`playerPitch = -freeRot.y` 由引擎镜像，解释了直写字段无效），但输入是**摇杆量纲**：1 输入单位 ≈ 0.1 弧度/帧（摇杆 0.17136/0.23821/0.24834 → Δ 0.0172/0.0238/0.0248），此前弧度增量直当输入小 300~800 倍 → "完全无反应"。修复：`horseLookScale` 默认 10.0。待复测：骑马俯仰速度/方向。
- **十轮：骑马俯仰改写 `freeRotation.x`（IC 口径，已被十一轮实测回退）**（2026-10-15）：九轮定标后输入已全额到达引擎（60 个强输入帧：`HandleLookInput` 以精确 0.1 rad/单位处理，`freeRotation.y` 与骑手 `data.angle.x` 均 51/60 响应，`freeRotation.x` **0/60 从未被触碰**），但画面仍不动——当时判定 `.y`/`angle.x` 为骑手马背前倾动画镜像而非视图俯仰，依据 IC `Horse.cpp`（上马 `freeRotation.x = player->data.angle.x`、FP 骑马上下限钳 `freeRotation.x`）改写 fx；移除 HandleLookInput 合成（`horseLookScale` 退役保留兼容），诊断行增列 `rotationInput.y`。**实测回退**：垂直动作变横摆 → fx 是轨道 yaw 偏移（见十一轮）。
- **十二轮：恢复摇杆同款 HandleLookInput 合成（scale 生效为准）**（2026-10-15，十一轮实测+摇杆对照日志）：十一轮 fy 直写实测"镜头位置小幅平移、准星不动"，且日志量化确认——引擎把 fy.y 当轨道位置俯仰消费后**每帧重置**（陀螺帧 d_fy = 0/50），fx 是轨道 yaw 偏移（十轮横摆）；`rotationInput.y` 全程为 0 非通道。决定性对照：**摇杆俯仰（总是有效）** 的引擎写入 = `input.y → freeRotation.x（大头 ~−0.5×y）+ fy/rider 镜像（0.1×y）`——即摇杆走的就是 `HandleLookInput` 这一个函数，引擎内部把输入组合成有效瞄准俯仰（内部映射组合无法从外部复刻，C1 排除合成摇杆事件）。**结论：复刻摇杆=调用同一函数、同一相位、同一量纲**。九轮败因勘误：部署构建日志 y≈pitch×1（`horseLookScale` 乘法未生效），欠功率 10 倍而非通道错误。修复：恢复 pre 相位合成 `HandleLookInput({0, pitch×horseLookScale})`（成员保活防引用存储），移除无效 fy 直写（副作用仅为轨道位置平移）。**复测验证点**：日志中合成 y 应 ≈ pitch×10（scale 生效证明）+ 准星随陀螺上下。
- **十三轮：合成调用移至 `PlayerCamera::Update` pre 阶段（相位修正）**（2026-10-15，十二轮日志定案）：十二轮日志显示 scale 已生效（y/pitch 中位数 9.99）但 **153 个强合成输入的引擎字段响应全部精确为 0**，同量级摇杆 308 次（含 219 次纯 x=0）响应正常（fx −0.126×y、fy/pp ±0.076×y）——**引擎完全忽略合成调用**。勘误贯穿九轮的误判：早期日志按"x=0"归因的"合成响应"实为摇杆（摇杆 71% 为纯垂直 x=0），合成从未被处理过；根因是调用发生在 `state->Update` 虚函数体内（相机更新已开始），摇杆则发生在 PlayerControls 输入相位。修复：新增 `PlayerCamera::Update`（vtable 0x02）detour，其 pre 阶段（输入相位之后、state Update 之前）执行马背心跳/取样/合成 HandleLookInput；状态级 horse detour 只保留 post 马头转向与日志。待复测：合成行字段响应应与摇杆同量级（fx/fy/pp 比例非零）+ 准星随陀螺上下。
- **十四轮：改用游戏原生输入分发的 0x0F 骑乘点（PlayerCamera 虚表 detour 无效）**（2026-10-15，十三轮日志）：十三轮 PlayerCamera vtable 0x02 detour **从未执行**（mount 心跳归零、`OnHorseCameraPreUpdate` 零调用；state 更新走的不是该虚表槽）——且水平（马头）也一并失效（取样被挪进死代码）。同时日志显示**游戏每帧都从输入相位调用 `HorseCameraState::HandleLookInput`**（1407/1407 帧含 (0,0) 帧）——这正是引擎处理 look 输入的原生上下文。修复：删除 PlayerCamera detour；在既有 0x0F 包装器内做心跳/取样/标定，并把俯仰按摇杆量纲**叠加进游戏原生调用的入参**（`{x, y + pitch×horseLookScale}`）——同函数、同相位、同调用栈上下文，与摇杆输入不可区分（诊断行改记原值 + 叠加值）。待复测：mount 心跳恢复、`+ (0, gyroY)` 非零、字段响应与摇杆同量级、准星随陀螺上下。
- **十六轮：原函数不消费入参——改写规范源 `lookInputVec`**（2026-10-15，十五轮日志三桶对照定案）：`aim+stick` 133/144 动、`pure-gyro` 0/256 动，且 `d_pp` 精确 = **−0.1×rawY 而非 −0.1×(rawY+gyroY)**——**原 `HandleLookInput` 忽略我们叠加的入参**，读取的是引擎自己的规范源（能解释 8-15 轮全部"传参无效"：摇杆有效是因为其值同时存在于规范源）。规范源锁定 `PlayerControls::data.lookInputVec`（NiPoint2@08，整形链产物）。修复：0x0F 包装器内分发前 `lookInputVec.y += gyroY`（仅 y 分量=垂直、输入相位当帧 poll 之后、无需回滚——下帧 poll 自然覆盖；入参仍同步叠加作为冗余）。**自验证判据**：下轮 `d_pp ≈ −0.1×(rawY+gyroY)` 即命中规范源。
- **十七轮：输入相位写骑手俯仰（瞄准态俯仰载波）**（2026-10-16，十六轮日志）：`lookInputVec.y` 写入生效（vecY==gyroY）但字段仍 0/116 → **规范源假设也排除**；结合十五轮分桶拼图完成——**瞄准态**摇杆 y → `d_pp = −0.1×y` 大头（非瞄准态才是 fx 0.39×y）→ **瞄准俯仰的载波是骑手 `data.angle.x`**，摇杆"总是有效"即此路径。七轮 angle.x 写入失效系**相位错误**（post 更新，吃不到当帧输入相位的瞄准消费；且七轮为独立写入实验，与本路线的入参/vec 前置轮不同）。修复：在 0x0F 输入相位钩内 `SetRotationX(angle.x − delta.pitch)`——与已验证的 FP 路径**同一表达式同一符号**，±89° 钳制；移除已证无效的 lookInputVec 改写（入参叠加保留作冗余）。待复测：`riderPitch=` 逐帧递增 + 准星随陀螺上下；方向反 → 只翻该处符号。
- **十八/十九轮：镜像对模型定案与输入相位双写**（2026-10-16，探针实验+逐帧三桶对照）：探针（陀螺推位移→摇杆垂直一碰→角色弹回水平、准星全程不动）定案——引擎维护 `(riderPitch, freeRotation.y = -riderPitch)` **镜像对**为瞄准俯仰状态：摇杆帧 `d_fy == -d_pp` 精确同步，单写 pp 帧 `d_fy = 0/768` 不镜像，静止零变化（写入持久）。准星消费镜像对而非孤立 `angle.x` ⇒ 角色动/准星不动/摇杆弹回三现象全部吻合。修复（十九轮）：0x0F 输入相位双写 `angle.x -= pitch`（同 FP）+ `freeRotation.y += pitch`（镜像关系、与引擎同相位）。同轮新增兼容性诊断：非系统模块清单（SmoothCam/TDM/IC 嫌疑标记）、detour 原函数归属、rig 标志位心跳。待复测：准星随陀螺上下；若仍不动则消费点为未插桩字段，依模块清单+钩子链归属排查 mod 接管。
- **二十/二十一轮：TDM 占链确证 + rotation 四元数直写路径（用户批准的兜底）**（2026-10-16）：二十轮四探针定案——`rotation composer owners: horse(0x0E)=TrueDirectionalMovement.dll`、`hook chain owners: horseLook=TrueDirectionalMovement.dll`（TDM 先占 0x0F/0x0E，我们挂其后）、`third(0x0E)=SkyrimSE.exe`。但随后用户 A/B：完整移除 TDM（含 360 Horse Archery 附带、动画重建）后马背现象不变 → **TDM 占链属实但非唯一根因：马背瞄准俯仰的消费点对 vanilla 字段写入整体不可见**（7-20 轮全路径穷尽：入参/vec/pp/fy 镜像对均无效）；A/B 另发现地面 TP 在三 mod 全关时也异常（罗盘随水平、镜头死）、仅 TDM 开+SC/IC 关时正常 → 地面链路受 mod 组合影响，待独立处理。二十一轮（经用户同意）实施直写路径：post 阶段把**累计俯仰角**（自维护 `_horseRotPitch`，pitchClamp 钳制、失活归零）以相机右轴四元数左乘合成进 `ThirdPersonState::rotation`（每次基于合成器当帧新输出重新奠基、持久性在我方累加器，TDM/vanilla 谁合成均最后覆盖）；仅 pitch，yaw 维持马头转向。待复测：马背准星/视角随陀螺上下；方向反 → 翻该处符号；若 rotation 成员不被消费 → 下一步 cameraRoot 矩阵直写（SmoothCam 完整机制）。
- **二十二轮：改挂 GetRotation 消费点（0x04）**（2026-10-16）：二十一轮 rotation 成员 post 直写实测无效（马背 Update 之后仍有环节改写或根本不读该成员）。合成点后移到 `HorseCameraState::GetRotation`（vtable 0x04）：原函数产出朝向后、相机应用前叠加累计俯仰（`ComposeMountedPitch`，右轴四元数左乘；成员保持未合成避免双叠；失活归零复位）；钩子链探针扩展 `horseGetRotation=` 显示 0x04 槽原主。附带日志（22:27 run）确认该轮 TDM 仍在链上（horseLook/horse 0x0E 均为其 DLL），TDM 开关不影响本方案——合成挂在取用函数上，与合成器是谁无关。待复测：马背垂直；反向则翻符号；若 GetRotation 亦非消费点 → 最终格 cameraRoot 矩阵直写。
- **二十二轮验收通过：马背垂直俯仰修复（2026-10-16 22:47 用户确认）**。日志复核：`horseGetRotation=SkyrimSE.exe`（TDM 未占 0x04），`rotation composer` 仍为 TDM——**根因定案与复盘**：马背瞄准的朝向只在 `GetRotation` 取用点被消费，TDM 的 UpdateRotation 合成与其间的任何字段/输入链写入（入参、lookInputVec、angle.x、freeRotation 镜像对、rotation 成员）对最终朝向全部不可见；挂消费点后与合成器身份无关（TDM 开关均有效）。**经验法则（供后续阶段沿用）：注入必须落在"消费函数"上，而不是"状态字段"上——字段可能被旁路、被镜像、被专用合成器每帧重建；钩子链探针（module 归属四行）应作为兼容性排查第一步。** TDM 占 0x0E/0x0F 与本 bug 并存但非其成因，仍记入 Phase 5 兼容矩阵（SmoothCam/IC/TDM 组合对地面 TP 的影响见二十轮 A/B，为独立待办）。
- **二十三轮：配置三段重构 + 每上下文/每轴注入方式选项**（2026-10-16）：ini 重组为 `General`（logLevel 默认 info + enableOnBowDraw）/ `Gyro`（用户手感：第三/第一/骑马 × 水平/垂直 灵敏度与反转、死区、平滑、钳制）/ `Advanced`（底层：inputSource、steamApiPath、gyroData/Space、calibration、axis、scale 等），当前行为全部作为默认值；Advanced 新增六键 `inject{ThirdPerson,FirstPerson,Mounted}{Yaw,Pitch} = field|compose|heading`——依据二十轮 A/B（三 mod 全关时步行第三人称同样失效），为步行上下文提供 GetRotation 消费点 compose 作为 mod 兼容逃生舱；compose 路径已实现为第三/第一人称 vtable 0x04 getter hook + 每上下文独立累加器（失活归零），骑马默认维持已验证的 heading yaw + compose pitch。旧分区（Steam/Feel/Activation、全局 sensitivity/invert）移除，需删 ini 重新生成。
- **二十四轮：compose yaw 横滚 bug 定案与修复（2026-10-17）**：`composeFrame` 探针 717 帧实测——`cameraRoot->world.rotate` 基向量布局为 **(right, forward, up)**（Skyrim Z-up 约定），状态四元数同系：col0=right（二十二轮 pitch 合成因此有效）、**col1=forward、col2=up**。原 ComposeYawPitch 误用 col1 作"up"→ yaw 合成绕**视轴**旋转 = 观察到的横滚。修复：yaw 轴改 col2（公式 `2(xy+wz), 2(yz−wx), 1−2(x²+y²)`），符号沿用 field 惯例（负向，可由各上下文 invert 翻转）；行走/马背 compose 共用同一函数一并修正。触发条件 = 任意上下文 `inject*yaw=compose`（当时用户手改 mounted yaw=compose；默认仍 heading/field 不受影响）。待复测：roll 消失、水平方向正确（反了调 `mountedInvertYaw`）。
- **二十五轮：compose yaw 符号修正 + 新增 rider 注入方式（2026-10-17）**：二十四轮修复后 roll 消失、但 compose yaw 方向反（实测）→ 符号翻为正向（`ComposeYawPitch` 注释留档）。用户反馈 compose 手感=纯相机自转、heading=连人带马一起转，追问只转角色 → 新增 `injectMountedYaw = rider`：`player->SetRotationZ(angle.z − yaw)`（FP 同款表达式），马头与行进方向不受影响。**未验证风险（复测点）**：骑乘系统可能每帧把骑手朝向重同步回马头（写入被覆盖→无效果或滞后）；瞄准 yaw 载波此前实证为马头（heading 模式准星跟随 ✓），rider-only 可能出现"角色转但准星不跟"（同十七轮俯仰镜像）——若如此，heading 仍是唯一瞄准耦合 yaw，再议取舍。
- **二十六轮：新增 orbit 注入方式并设为骑马 yaw 默认（2026-10-18，调研驱动）**：日志实证（68 个瞄准态纯水平摇杆帧，d_fx 68/68 响应）+ TDM/IC 源码交叉验证 → 摇杆水平走的就是 `freeRotation.x`（轨道偏航：准星/相机绕马、马不变向、骑手无重同步），非 mod 行为而是 vanilla 语义。实现 `injectMountedYaw = orbit`：post 阶段 `freeRotation.x -= yaw`（与 TP field 同符号约定，反向可调 `mountedInvertYaw`），设为默认；`heading`（转马）/`rider`（已证伪：骑乘每帧重同步骑手朝向）/`compose`（纯相机兜底）保留可选。
- **二十七轮：orbit 水平验收通过 + 垂直 2× 落点修复（2026-10-18）**：orbit 水平实测通过。垂直无"orbit 类比"（fx 是唯一轨道偏移字段；fy 的轨道对应十一/十八轮实测仅位置平移不带视图俯仰，死路），垂直维持 compose。用户反馈"落点 ≈ 2× 准星位移"——根因：十九轮引入的输入相位骑手俯仰随行写入（`angle.x -= pitch` + fy 镜像）在 compose 激活时**双计数进瞄准**（compose 转相机 Δ + 瞄准 rig 追骑手 Δ + 该写入再叠 Δ → 箭矢方向 2Δ、准星 Δ）。修复：移除该随行写入（其历史使命"骑手动画跟随"已被瞄准 rig 的自然跟随取代），垂直单一事实源 = GetRotation compose。待复测：落点/准星 1:1；若仍 2× 则箭矢方向另有独立载波，转调研弹道级方案（SmoothCam arrow_fixes 模式）。
- **二十八轮：orbit+2×修复真机验收通过；hid 输入源落地（协作 session）；版本提交**（2026-10-18）：用户确认 orbit 水平 + compose 垂直 + 2×修复为当前最佳手感，默认值即为该组合（`injectMountedYaw=orbit`、`injectMountedPitch=compose`、其余 field/heading 语义见 CONFIG.md）。协作 session 落地 **Phase 3 `hid` 源**（SDL2 gamecontroller 传感器直读：PS 坐标系=GMH 空间免基变换、rad/s→deg/s 与 m/s²→g 换算、断连重扫、无手柄时明确报错）；**输入源策略更新（修订 C4）**：配置源优先，初始化失败时按固定序 `steam → hid` 受控回退（warn 提示，双失败才禁用）——原"绝不回退"放宽为"显式可诊断的回退"，`dsu` 保留占位。双预设（含 sdl2 依赖）零警告构建通过。
- **调研记录：相机 mod 源码与 API（2026-10-18 只读调研，IC-NG@2e441c1 / SmoothCam@e0afd09 / TDM@57b913a）**：
  - **马背偏航机制**：TDM `src/Hooks.cpp:812-841` 生产实证相机 yaw = `马头朝向 + freeRotation.x`，其 UpdateRotation 替身做"马转时 fx += horseCurrentDirection − heading 保持世界瞄准"；SmoothCam `thirdperson.cpp:771-797` "lie-trick"（强制 freeRotationEnabled 调 UpdateRotation 取游戏计算的旋转）+ `:685-722` 直写 cameraRoot/tps->rotation，`GetAimRotation :647-681` 马背瞄准 ±90° faceforward 钳制，`arrow_fixes.cpp:88-196` 弹道级覆写 linearVelocity；IC-NG `cameras/Horse.cpp:64-66,110-130` 仅播种/钳制 fx + 变焦，三库全库 0 命中 SetRotationZ/SetHeading/RotateNode → **没有任何 mod 靠转角色实现瞄准**。
  - **公开 API**：SmoothCam 有（`SmoothCamAPI.h`，SKSE 消息头 0x9007CA50，sender "SmoothCam"，kPostLoad 注册 loader + kPostPostLoad 请求；IVSmoothCam1 相机/准星/潜行表控制权、IVSmoothCam2 仅位置查询无旋转查询、IVSmoothCam3 `EnableUnlockedHorseAim` 解锁马背 ±90° 钳制；集成范例见 IC-NG `ImprovedCameraSE.cpp:775-786`）；**IC-NG 无公开 API**（include/api 仅 SKSE 样板，SmoothCamAPI.h/TDM API 头均为其消费方 vendored）。
  - **备用技术栈（按适配度）**：① orbit（本轮已实现，默认）；② SmoothCam lie-trick + cameraRoot 直写（需配 `IsCameraTaken()` 竞态防护）；③ 弹道级解耦（±90° 钳制覆写箭矢速度，配合 SmoothCam V3 免费解锁）。
  - **遗留分歧标注**：fx/fy 语义各源标注不一（TDM：x=yaw/y=pitch；IC：x="Yaw"/y="Roll"；SmoothCam 调试图把 fy 混入 yaw 序列）——**以 TDM 生产数学为准**；SmoothCam 在马背 vtable "index 4" 挂 ProcessButton 属计数惯例差异，与我们已验证的 0x03/0x04/0x0F 不冲突。
- **十一轮：回退 fx，俯仰单写 `freeRotation.y`**（2026-10-15，十轮实测触发回退条件）：十轮 fx 写入实测"垂直动作→横摆"，确认 mounted 轨道 yaw = 马头 + `freeRotation.x`（TDM 口径成立、IC 钳制为其 FP 特例），fx 俯仰写入废弃。复核 git 历史：七轮提交代码实为 `data.angle.x` 单写（骑手动画动、镜头不动 → 骑手俯仰非视图俯仰），**`freeRotation.y` 的 post 直写从未真正执行过**（计划"双写"记录与提交不符）；八/九轮 HandleLookInput 通道喂的正是 `fy.y` 且引擎全额处理，但部署构建量纲疑似 ≈1（日志 y≈pitch×1）导致 10 倍欠功率而非通道失效。方案：horse 俯仰与地面 TP 完全同路径同符号 `freeRotation.y += pitch`（`HorseCameraState` 继承同一 `UpdateRotation`，TDM 合成式亦为 `pitch = −freeRotation.y`；地面已验证该写法），单写、无双写干扰；若方向反 → 仅翻 ApplyToHorse 符号。待复测：骑马俯仰生效/方向；若仍无反应 → 转 `rotation` 四元数 post 直写（SmoothCam 机制，观测点 `rotInY` 已就位）。

### Phase 4 — 功能扩展（工作量：M~L）
- 激活条件扩展：弩、法术蓄力、蹲伏、按键 Toggle/Hold（配置化）；
- **验收**：各激活条件下启用/停用切换无跳变、无累积漂移。

### Phase 5 — 兼容性与发布（工作量：M）
- 兼容测试矩阵（§5）；
- 按 D2 结论落实 patch 形态；
- 文档：安装、Steam 配置要求（**Gyro Behavior 设为 None**，避免与插件叠加出双倍视角）、配置项说明、已知不兼容列表；
- 运行时覆盖：SE 1.5.97 + AE 1.6.x + GOG（CommonLibSSE NG `RELOCATION_ID(SE,AE)` 双值 + REL 抽象），VR 明确不支持；
- **验收**：干净 MO2 环境 + 典型整合包环境各跑通一遍。

### Phase 3 — 多输入源：**hid 已实现；dsu 不在计划内（2026-10-18 定案）**
- **`steam`**（默认）：官方 Steam Input 传感器通道；
- **`hid`**（2026-10-17 实现）：SDL2 GameController 传感器 API（固定 baseline 为 2.22 时代、无 sdl3 端口；Joystick 级 sensor API 为 SDL3 专属），hidapi 共享模式直读 DS4/DualSense/Switch Pro；轴空间与 GMH 同为 PS Y-up 约定、rad/s 与 m/s² 源内换算；热插拔 ~2s 重扫；启动校验失败日志明确报错；
- **回退策略**（2026-10-18，取代 C4"无自动回退"）：配置源优先尝试，不可用（初始化失败/枚举不到陀螺仪手柄）时按 `steam → hid` 固定顺序回退，全部失败才禁用；失败源先自报原因、回退链再报 `trying next fallback`；
- **`dsu`**（Cemuhook UDP，覆盖 DS4Windows/HidHide 场景）：**暂不实现**——工程复杂度考虑（2026-10-18 决定）；DualSense 蓝牙在 Windows 的系统级枚举兼容性问题同样**不深究**，记录为已知限制（见 CONFIG.md"输入源已知限制"）；
- 待评估（低优先）：启动全部失败后进入热插拔重扫循环而非永久禁用。

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

### C4 输入源配置化（2026-10-18 修订：启用回退链）
- `inputSource` 显式配置（默认 `steam`），配置源优先尝试；不可用时按固定顺序 `steam → hid` 回退，全部失败才报错禁用（取代原"无自动回退"策略，起因：蓝牙手柄启动瞬间可能不在线）；
- 每个源的前置条件写进文档（steam 需 Steam 运行 + Gyro Behavior=None；hid 直读共享 HID、无需 Steam）。

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
| DS4Windows（HidHide） | ⚠️ HidHide 隐藏物理设备后 steam/hid 均看不到 | 需 DSU 源（暂不实现，2026-10-18） |
| GOG 版 / 无 Steam | ✅ 走 hid 源 | steam 源失败自动回退，不崩溃 |

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
4. 之后按 §2 顺序走 Phase 2 → 2.1 → 4 → 5，每阶段过完验收再进下一阶段；Phase 3：hid 已实现（steam/hid 回退链），dsu 不在计划内（2026-10-18 定案）。
