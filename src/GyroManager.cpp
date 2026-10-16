#include "GyroManager.h"

#include "Config.h"
#include "SteamInputSource.h"

namespace GyroAim {
    namespace {
        constexpr float kDegToRad = 0.017453292519943295F;
        constexpr float kRadToDeg = 57.29577951308232F;
        constexpr float kMaxPitch = 1.5533F;  // 89 degrees

        [[nodiscard]] float AxisValue(float a_x, float a_y, float a_z, GyroAxis a_axis) {
            switch (a_axis) {
                case GyroAxis::kY:
                    return a_y;
                case GyroAxis::kZ:
                    return a_z;
                case GyroAxis::kX:
                default:
                    return a_x;
            }
        }

        [[nodiscard]] const char* CameraStateName(std::uint32_t a_id) {
            static constexpr std::array names{"firstPerson"sv, "autoVanity"sv,  "vats"sv,         "free"sv,
                                              "ironSights"sv,  "furniture"sv,   "pcTransition"sv, "tween"sv,
                                              "animated"sv,    "thirdPerson"sv, "mount"sv,        "bleedout"sv,
                                              "dragon"sv};
            return a_id < names.size() ? names[a_id].data() : "unknown";
        }
    }

    GyroManager* GyroManager::GetSingleton() {
        static GyroManager instance;
        return &instance;
    }

    void GyroManager::Initialize() {
        const auto& settings = Config::GetSingleton()->GetSettings();

        // Drift compensation via GMH auto-calibration (stillness + sensor-fusion bias
        // tracking); can be disabled from the config. The Start/Pause*Calibration API is
        // the manual "calibrate now" window: it averages every sample -- including real
        // aiming rotations -- into the bias and disables auto-calibration, so it must not
        // run during gameplay.
        _motion.ResetContinuousCalibration();
        if (settings.calibration) {
            _motion.SetCalibrationMode(GamepadMotionHelpers::CalibrationMode::Stillness |
                                       GamepadMotionHelpers::CalibrationMode::SensorFusion);
        } else {
            _motion.SetCalibrationMode(GamepadMotionHelpers::CalibrationMode::Manual);
        }

        const auto inputSource = settings.inputSource;
        switch (inputSource) {
            case InputSourceType::kSteam:
                _source = SteamInputSource::GetSingleton();
                break;
            case InputSourceType::kDsu:
            case InputSourceType::kHid:
                SKSE::log::error("inputSource '{}' is not implemented yet (Phase 3); gyro disabled"sv,
                                 inputSource == InputSourceType::kDsu ? "dsu" : "hid");
                return;
            case InputSourceType::kUnknown:
            default:
                SKSE::log::error("inputSource is invalid; gyro disabled"sv);
                return;
        }

        if (!_source->Initialize()) {
            SKSE::log::error("gyro source '{}' failed to initialize; gyro disabled (no automatic fallback)"sv,
                             _source->Name());
            _source = nullptr;
        }
    }

    void GyroManager::Shutdown() {
        if (_source) {
            _source->Shutdown();
            _source = nullptr;
        }
    }

    bool GyroManager::IsBowAiming() const {
        const auto* player = RE::PlayerCharacter::GetSingleton();
        const auto* actorState = player ? player->AsActorState() : nullptr;
        if (!actorState) {
            return false;
        }
        // Fires the moment the trigger starts the draw, not only at full draw.
        const auto state = static_cast<std::uint32_t>(actorState->GetAttackState());
        return state >= static_cast<std::uint32_t>(RE::ATTACK_STATE_ENUM::kBowDraw) &&
               state <= static_cast<std::uint32_t>(RE::ATTACK_STATE_ENUM::kBowNextAttack);
    }

    bool GyroManager::IsActive() const {
        const auto& settings = Config::GetSingleton()->GetSettings();
        return settings.enableOnBowDraw ? IsBowAiming() : true;
    }

    void GyroManager::ResetProcessing() {
        _smoothYaw = 0.0F;
        _smoothPitch = 0.0F;
        _pitchAccum = 0.0F;
        _hasOrientation = false;
        _horseRotPitch = 0.0F;  // release the composition offsets when the gyro idles
        _horseYawAcc = 0.0F;
        _tpYawAcc = 0.0F;
        _tpPitchAcc = 0.0F;
        _fpYawAcc = 0.0F;
        _fpPitchAcc = 0.0F;
    }

    void GyroManager::LogHeartbeat(RE::TESCameraState* a_state, TimePoint a_now) {
        if (a_now - _lastHeartbeat < 1s) {
            return;
        }
        _lastHeartbeat = a_now;

        const auto cameraId = static_cast<std::uint32_t>(a_state->id);

        const auto* player = RE::PlayerCharacter::GetSingleton();
        const auto* actorState = player ? player->AsActorState() : nullptr;
        const auto attackState = actorState ? static_cast<std::uint32_t>(actorState->GetAttackState()) : ~0U;

        SKSE::log::info("heartbeat camera={} attackState={} bowAiming={} gyro={}"sv, CameraStateName(cameraId),
                        attackState, IsBowAiming(), _source != nullptr);

        // Compatibility probes: which rig owns the camera this frame. -1 = n/a for
        // this state. A mod-managed aim rig (IC/TDM/SmoothCam) shows up as these flags
        // behaving differently from vanilla expectations while our rider write moves
        // the rider but not the view.
        int freeRO = -1;
        int animCam = -1;
        int applyOff = -1;
        if (cameraId == static_cast<std::uint32_t>(RE::CameraStates::kThirdPerson) ||
            cameraId == static_cast<std::uint32_t>(RE::CameraStates::kMount)) {
            const auto* tp = static_cast<const RE::ThirdPersonState*>(a_state);
            freeRO = tp->freeRotationEnabled ? 1 : 0;
            animCam = tp->toggleAnimCam ? 1 : 0;
            applyOff = tp->applyOffsets ? 1 : 0;
        }
        const auto* camera = RE::PlayerCamera::GetSingleton();
        const int zoom = camera && camera->bowZoomedIn ? 1 : 0;
        SKSE::log::info("rig freeRO={} animCam={} applyOff={} bowZoom={}"sv, freeRO, animCam, applyOff, zoom);

        // Ground truth for drift/axis investigations: after the raw-scale conversion,
        // accelMag should read ~1.0 (g) at rest; grav should be ~ (0,-1,0) at rest on a
        // flat surface. A large accelMag or off-axis grav means the unit conversion or
        // gravity estimation regressed (roll leaking into yaw / sign flips).
        float gravX, gravY, gravZ;
        _motion.GetGravity(gravX, gravY, gravZ);
        SKSE::log::debug("motion accelMag={:.2f} grav=({:.2f}, {:.2f}, {:.2f}) out=({:.3f}, {:.3f}) deg/frame"sv,
                         _lastAccelMag, gravX, gravY, gravZ, _lastYawDeg, _lastPitchDeg);
    }

    float GyroManager::Tick(TimePoint a_now) {
        float dt = 0.0F;
        if (_hasTimestamp) {
            dt = std::clamp(std::chrono::duration<float>(a_now - _lastUpdate).count(), 0.0001F, 0.05F);
        }
        _lastUpdate = a_now;
        _hasTimestamp = true;
        return dt;
    }

    bool GyroManager::ComputeFrameDelta(float a_dt, FrameDelta& a_out) {
        if (!_source) {
            return false;
        }

        _source->RunFrame();
        GyroSample raw;
        if (!_source->SampleMotion(raw)) {
            ResetProcessing();
            return false;
        }

        // Gravity fusion and calibration stay warm even while the gyro is inactive.
        _motion.ProcessMotion(raw.x, raw.y, raw.z, raw.accelX, raw.accelY, raw.accelZ, a_dt);
        _lastAccelMag = std::hypot(raw.accelX, raw.accelY, raw.accelZ);

        if (!IsActive()) {
            ResetProcessing();
            return false;
        }

        const auto& settings = Config::GetSingleton()->GetSettings();

        // Per-frame pitch/yaw in degrees: fused-orientation delta (D5 semantics) or rate * dt.
        float pitchDeg;
        float yawDeg;
        if (settings.gyroData == GyroData::kRotVel) {
            float gyroX, gyroY, gyroZ;
            _motion.GetCalibratedGyro(gyroX, gyroY, gyroZ);
            float pitchRate;
            float yawRate;
            ConvertSpace(settings, gyroX, gyroY, gyroZ, pitchRate, yawRate);
            pitchDeg = pitchRate * a_dt;
            yawDeg = yawRate * a_dt;
        } else if (!OrientationDeltaDegrees(settings, pitchDeg, yawDeg)) {
            return false;  // orientation seed frame: keep state, no delta yet
        }
        _lastYawDeg = yawDeg;
        _lastPitchDeg = pitchDeg;

        // Per-axis deadzone in deg/s; per-frame degrees scale with dt.
        if (std::fabs(yawDeg) < settings.yawDeadzoneDegPerSec * a_dt) {
            yawDeg = 0.0F;
        }
        if (std::fabs(pitchDeg) < settings.pitchDeadzoneDegPerSec * a_dt) {
            pitchDeg = 0.0F;
        }

        const float alpha = 1.0F - settings.smoothing;
        _smoothYaw += alpha * (yawDeg - _smoothYaw);
        _smoothPitch += alpha * (pitchDeg - _smoothPitch);

        // Raw radian delta: per-context sensitivity/invert and injection method are
        // applied by the Apply* functions (per [Gyro]/[Advanced] config).
        const float deltaYaw = _smoothYaw * kDegToRad;

        float deltaPitch = _smoothPitch * kDegToRad;
        const float clampRad = std::fabs(settings.pitchClampDeg) * kDegToRad;
        const float targetPitch = std::clamp(_pitchAccum + deltaPitch, -clampRad, clampRad);
        deltaPitch = targetPitch - _pitchAccum;
        _pitchAccum = targetPitch;

        a_out = FrameDelta{deltaYaw, deltaPitch};
        return true;
    }

    void GyroManager::ConvertSpace(const Settings& a_settings, float a_x, float a_y, float a_z, float& a_pitch,
                                   float& a_yaw) {
        float gravX, gravY, gravZ;
        _motion.GetGravity(gravX, gravY, gravZ);

        switch (a_settings.gyroSpace) {
            case GyroSpace::kPlayer:
                GamepadMotion::CalculatePlayerSpaceGyro(a_pitch, a_yaw, a_x, a_y, a_z, gravX, gravY, gravZ,
                                                        a_settings.yawRelaxFactor);
                break;
            case GyroSpace::kWorld:
                GamepadMotion::CalculateWorldSpaceGyro(a_pitch, a_yaw, a_x, a_y, a_z, gravX, gravY, gravZ);
                break;
            case GyroSpace::kLocal:
            default:
                a_pitch = AxisValue(a_x, a_y, a_z, a_settings.pitchAxis);
                a_yaw = AxisValue(a_x, a_y, a_z, a_settings.yawAxis);
                break;
        }
    }

    bool GyroManager::OrientationDeltaDegrees(const Settings& a_settings, float& a_pitchDeg, float& a_yawDeg) {
        float w, x, y, z;
        _motion.GetOrientation(w, x, y, z);

        // q_delta = conj(previous) * current: this frame's rotation in device axes.
        const Orientation previous = _prevOrientation;
        _prevOrientation = Orientation{w, x, y, z};
        if (!_hasOrientation) {
            _hasOrientation = true;
            a_pitchDeg = 0.0F;
            a_yawDeg = 0.0F;
            return false;
        }

        float qw = previous.w * w + previous.x * x + previous.y * y + previous.z * z;
        float qx = previous.w * x - previous.x * w - previous.y * z + previous.z * y;
        float qy = previous.w * y + previous.x * z - previous.y * w - previous.z * x;
        float qz = previous.w * z - previous.x * y + previous.y * x - previous.z * w;
        if (qw < 0.0F) {  // shortest arc
            qw = -qw;
            qx = -qx;
            qy = -qy;
            qz = -qz;
        }

        const float sinHalf = std::sqrt(qx * qx + qy * qy + qz * qz);
        if (sinHalf < 1e-8F) {
            a_pitchDeg = 0.0F;
            a_yawDeg = 0.0F;
            return true;
        }

        // Rotation vector in degrees on the device axes, then space conversion.
        const float scale = 2.0F * std::atan2(sinHalf, qw) * kRadToDeg / sinHalf;
        ConvertSpace(a_settings, qx * scale, qy * scale, qz * scale, a_pitchDeg, a_yawDeg);
        return true;
    }

    void GyroManager::OnThirdPersonUpdate(RE::ThirdPersonState* a_state) {
        const auto now = std::chrono::steady_clock::now();
        LogHeartbeat(a_state, now);

        FrameDelta delta;
        if (!ComputeFrameDelta(Tick(now), delta)) {
            return;
        }
        ApplyToThirdPerson(a_state, delta);
        SKSE::log::debug("tp inject yaw={:.4f} pitch={:.4f} freeRot=({:.3f}, {:.3f})"sv, delta.yaw, delta.pitch,
                         a_state->freeRotation.x, a_state->freeRotation.y);
    }

    float GyroManager::OnHorseLookInputPhase(RE::HorseCameraState* a_state) {
        const auto now = std::chrono::steady_clock::now();
        LogHeartbeat(a_state, now);

        FrameDelta delta;
        if (!ComputeFrameDelta(Tick(now), delta)) {
            _hasHorseDelta = false;
            return 0.0F;
        }
        _horseDelta = delta;
        _hasHorseDelta = true;

        // Mounted-aim pitch carrier, proven from the round-15/16/18 logs: while aiming
        // the engine maintains a (rider pitch, freeRotation.y = -pitch) MIRROR PAIR as
        // the aim state -- stick frames update both together (d_fy == -d_pp exactly),
        // while our rider-only writes left the mirror stale: the character animated but
        // the crosshair (which reads the mirrored state) never moved, and a stick nudge
        // re-synced the pair from the engine's own accumulator (the "snap back level").
        // Write BOTH at input phase, same moment and same relation as the engine
        // (up is negative, identical to the verified first-person expression).
        if (auto* player = RE::PlayerCharacter::GetSingleton()) {
            const auto& s = Config::GetSingleton()->GetSettings();
            const float riderPitch = delta.pitch * s.mountedPitchSensitivity * (s.mountedInvertPitch ? -1.0F : 1.0F);
            player->SetRotationX(std::clamp(player->data.angle.x - riderPitch, -kMaxPitch, kMaxPitch));
            a_state->freeRotation.y += riderPitch;  // mirror: d_fy == -d_pp
        }

        // Stick-magnitude contribution for the forwarded argument (redundant; the
        // engine's canonical read ignored it in round 15-16, kept as defense in depth).
        return delta.pitch * Config::GetSingleton()->GetSettings().horseLookScale;
    }

    void GyroManager::OnHorseCameraUpdate(RE::HorseCameraState* a_state) {
        // Pre/post snapshot of every candidate field: if the pre values of the next
        // frame do not carry this frame's post values, another mod is overwriting them.
        const float preFreeX = a_state->freeRotation.x;
        const float preFreeY = a_state->freeRotation.y;
        const auto* player = RE::PlayerCharacter::GetSingleton();
        const float prePlayerPitch = player ? player->data.angle.x : 0.0F;
        const auto* camera = RE::PlayerCamera::GetSingleton();
        const float preRotInputY = camera ? camera->rotationInput.y : 0.0F;

        if (_hasHorseDelta) {
            ApplyToHorse(a_state, _horseDelta);
        }
        SKSE::log::debug(
            "horse inject yaw={:.4f} pitch={:.4f} freeRot=({:.4f},{:.4f})->({:.4f},{:.4f}) playerPitch={:.4f}->{:.4f} rotInY={:.4f}->{:.4f}"sv,
            _horseDelta.yaw, _horseDelta.pitch, preFreeX, preFreeY, a_state->freeRotation.x, a_state->freeRotation.y,
            prePlayerPitch, player ? player->data.angle.x : 0.0F, preRotInputY,
            camera ? camera->rotationInput.y : 0.0F);
    }

    void GyroManager::OnFirstPersonUpdate(RE::FirstPersonState* a_state) {
        const auto now = std::chrono::steady_clock::now();
        LogHeartbeat(a_state, now);

        FrameDelta delta;
        if (!ComputeFrameDelta(Tick(now), delta)) {
            return;
        }
        ApplyToFirstPerson(delta);
        const auto* player = RE::PlayerCharacter::GetSingleton();
        SKSE::log::debug("fp inject yaw={:.4f} pitch={:.4f} playerAngle=({:.3f}, {:.3f})"sv, delta.yaw, delta.pitch,
                         player ? player->data.angle.x : 0.0F, player ? player->data.angle.z : 0.0F);
    }

    void GyroManager::ApplyToThirdPerson(RE::ThirdPersonState* a_state, const FrameDelta& a_delta) {
        // Red line 3: increments only, applied after the original Update ran. Per-axis
        // method from [Advanced] injectThirdPerson*: field = the verified freeRotation
        // increments; compose = feed the accumulator consumed by the GetRotation hook
        // (the mod-breakage fallback, round-22 path).
        const auto& s = Config::GetSingleton()->GetSettings();
        const float yaw = a_delta.yaw * s.thirdYawSensitivity * (s.thirdInvertYaw ? -1.0F : 1.0F);
        const float pitch = a_delta.pitch * s.thirdPitchSensitivity * (s.thirdInvertPitch ? -1.0F : 1.0F);

        if (s.injectThirdPersonYaw == InjectionMethod::kCompose) {
            _tpYawAcc += yaw;
        } else {
            a_state->freeRotation.x -= yaw;
        }
        if (s.injectThirdPersonPitch == InjectionMethod::kCompose) {
            const float clampRad = std::fabs(s.pitchClampDeg) * kDegToRad;
            _tpPitchAcc = std::clamp(_tpPitchAcc + pitch, -clampRad, clampRad);
        } else {
            a_state->freeRotation.y += pitch;
        }
    }

    void GyroManager::ApplyToFirstPerson(const FrameDelta& a_delta) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            return;
        }

        // Per-axis method from [Advanced] injectFirstPerson*: field = the verified
        // player-heading writes (yaw subtracts, pitch into angle.x, up is negative);
        // compose = GetRotation-hook accumulators.
        const auto& s = Config::GetSingleton()->GetSettings();
        const float yaw = a_delta.yaw * s.firstYawSensitivity * (s.firstInvertYaw ? -1.0F : 1.0F);
        const float pitch = a_delta.pitch * s.firstPitchSensitivity * (s.firstInvertPitch ? -1.0F : 1.0F);

        if (s.injectFirstPersonYaw == InjectionMethod::kCompose) {
            _fpYawAcc += yaw;
        } else {
            player->SetRotationZ(player->data.angle.z - yaw);
        }
        if (s.injectFirstPersonPitch == InjectionMethod::kCompose) {
            const float clampRad = std::fabs(s.pitchClampDeg) * kDegToRad;
            _fpPitchAcc = std::clamp(_fpPitchAcc + pitch, -clampRad, clampRad);
        } else {
            player->SetRotationX(std::clamp(player->data.angle.x - pitch, -kMaxPitch, kMaxPitch));
        }
    }

    namespace {
        // Left-multiply q about a (unit-normalized) axis by angle, then renormalize.
        void ComposeAxis(RE::NiQuaternion& a_q, float a_ux, float a_uy, float a_uz, float a_angle) {
            const float len = std::sqrt(a_ux * a_ux + a_uy * a_uy + a_uz * a_uz);
            if (len <= 1e-6F || std::fabs(a_angle) < 1e-7F) {
                return;
            }
            const float half = a_angle * 0.5F;
            const float s = std::sin(half) / len;
            const float c = std::cos(half);
            const float ux = a_ux * s;
            const float uy = a_uy * s;
            const float uz = a_uz * s;
            const float px = c * a_q.w - ux * a_q.x - uy * a_q.y - uz * a_q.z;
            const float py = c * a_q.x + ux * a_q.w + uy * a_q.z - uz * a_q.y;
            const float pz = c * a_q.y - ux * a_q.z + uy * a_q.w + uz * a_q.x;
            const float pw = c * a_q.z + ux * a_q.y - uy * a_q.x + uz * a_q.w;
            const float n = std::sqrt(px * px + py * py + pz * pz + pw * pw);
            // px/py/pz/pw hold the product's w/x/y/z parts respectively.
            a_q.w = px / n;
            a_q.x = py / n;
            a_q.y = pz / n;
            a_q.z = pw / n;
        }

        // Compose accumulated yaw/pitch onto an orientation. Axis convention verified
        // by the round-24 composeFrame probe (717 frames): the state quaternion lives
        // in Skyrim's right/forward/up basis -- column0 = camera right (pitch axis,
        // round-22 verified), column1 = camera FORWARD (using it for yaw rotated about
        // the view axis = the observed roll bug), column2 = camera up (the yaw axis).
        // Yaw keeps the negated sign to match the field convention (freeRotation.x -=
        // yaw, SetRotationZ(angle - yaw)); flip with the per-context invert keys.
        void ComposeYawPitch(RE::NiQuaternion& a_q, float a_yaw, float a_pitch) {
            if (std::fabs(a_pitch) >= 1e-7F) {
                const float rx = 1.0F - 2.0F * (a_q.y * a_q.y + a_q.z * a_q.z);
                const float ry = 2.0F * (a_q.x * a_q.y + a_q.w * a_q.z);
                const float rz = 2.0F * (a_q.x * a_q.z - a_q.w * a_q.y);
                ComposeAxis(a_q, rx, ry, rz, a_pitch);
            }
            if (std::fabs(a_yaw) >= 1e-7F) {
                const float ux = 2.0F * (a_q.x * a_q.y + a_q.w * a_q.z);
                const float uy = 2.0F * (a_q.y * a_q.z - a_q.w * a_q.x);
                const float uz = 1.0F - 2.0F * (a_q.x * a_q.x + a_q.y * a_q.y);
                // Sign fixed by the round-25 runtime test (negated was reversed).
                ComposeAxis(a_q, ux, uy, uz, a_yaw);
            }
        }
    }

    void GyroManager::ComposeMountedRotation(RE::NiQuaternion& a_rotation) {
        ComposeYawPitch(a_rotation, _horseYawAcc, _horseRotPitch);
    }

    void GyroManager::ComposeThirdPerson(RE::NiQuaternion& a_rotation) {
        ComposeYawPitch(a_rotation, _tpYawAcc, _tpPitchAcc);
    }

    void GyroManager::ComposeFirstPerson(RE::NiQuaternion& a_rotation) {
        ComposeYawPitch(a_rotation, _fpYawAcc, _fpPitchAcc);
    }

    void GyroManager::ApplyToHorse(RE::HorseCameraState* a_state, const FrameDelta& a_delta) {
        const auto& s = Config::GetSingleton()->GetSettings();
        const float yaw = a_delta.yaw * s.mountedYawSensitivity * (s.mountedInvertYaw ? -1.0F : 1.0F);
        const float pitch = a_delta.pitch * s.mountedPitchSensitivity * (s.mountedInvertPitch ? -1.0F : 1.0F);

        // yaw: heading = steer the horse (the mounted aim direction, verified; the
        // camera anchor follows so the view and crosshair turn with it, applied post
        // Update so movement processing cannot overwrite it); rider = rotate only the
        // rider (FP-style SetRotationZ, horse keeps its course); compose = camera-only
        // yaw through the GetRotation hook.
        if (s.injectMountedYaw == InjectionMethod::kCompose) {
            _horseYawAcc += yaw;
        } else if (s.injectMountedYaw == InjectionMethod::kRider) {
            if (auto* player = RE::PlayerCharacter::GetSingleton()) {
                player->SetRotationZ(player->data.angle.z - yaw);
            }
        } else if (RE::Actor* horse = a_state->horseRefHandle
                                          ? static_cast<RE::Actor*>(a_state->horseRefHandle.get().get())
                                          : nullptr) {
            horse->SetRotationZ(horse->data.angle.z - yaw);
            a_state->horseCurrentDirection = horse->GetHeading(false);
        }

        // pitch: compose = round-22 verified GetRotation consumption path (default);
        // field = legacy freeRotation.y increment kept as a switchable fallback.
        if (s.injectMountedPitch == InjectionMethod::kCompose) {
            const float clampRad = std::fabs(s.pitchClampDeg) * kDegToRad;
            _horseRotPitch = std::clamp(_horseRotPitch + pitch, -clampRad, clampRad);
        } else {
            a_state->freeRotation.y += pitch;
        }
    }
}
