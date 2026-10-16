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

        const float yawSign = settings.invertYaw ? -1.0F : 1.0F;
        const float pitchSign = settings.invertPitch ? -1.0F : 1.0F;
        const float deltaYaw = _smoothYaw * settings.yawSensitivity * yawSign * kDegToRad;

        float deltaPitch = _smoothPitch * settings.pitchSensitivity * pitchSign * kDegToRad;
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

        // Mounted-aim pitch carrier, proven from the round-15/16 logs: while aiming,
        // stick y maps to the RIDER pitch (data.angle.x) at -0.1 rad per input unit and
        // that is what moves the crosshair; both earlier rider writes (round 7) missed
        // the input-phase consumption this hook runs in. Write it here, at input phase,
        // with the exact expression the verified first-person path uses (up is negative).
        if (auto* player = RE::PlayerCharacter::GetSingleton()) {
            player->SetRotationX(std::clamp(player->data.angle.x - delta.pitch, -kMaxPitch, kMaxPitch));
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
        // 2026-10-14 rollback baseline: camera free-rotation increments only. The
        // targetYaw- and player-heading-based modes were retired after field testing
        // (free-camera artifact / crosshair mismatch). Red line 3: increments only,
        // applied after the original Update ran.
        // 2026-10-15: yaw direction is inverted relative to the player-heading
        // fields (FP subtracts and is correct), so freeRotation.x subtracts too.
        a_state->freeRotation.x -= a_delta.yaw;
        a_state->freeRotation.y += a_delta.pitch;
    }

    void GyroManager::ApplyToFirstPerson(const FrameDelta& a_delta) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            return;
        }

        // FP yaw is the player heading; runtime testing showed the sign is opposite to
        // the ThirdPersonState yaw fields (2026-10-14), so increments are subtracted.
        player->SetRotationZ(player->data.angle.z - a_delta.yaw);

        // FP look pitch lives in data.angle.x (up is negative, verified against
        // ImprovedCameraSE-NG's look-pitch clamps). FirstPersonState's pitch offsets are
        // re-derived from it every update, so writing them does not persist.
        player->SetRotationX(std::clamp(player->data.angle.x - a_delta.pitch, -kMaxPitch, kMaxPitch));
    }

    void GyroManager::ApplyToHorse(RE::HorseCameraState* a_state, const FrameDelta& a_delta) {
        // yaw: steer the horse heading (the mounted aim direction); the camera anchor
        // follows so the view and crosshair turn with it. Applied after the original
        // Update so the horse's own movement processing cannot overwrite it this frame.
        RE::Actor* horse =
            a_state->horseRefHandle ? static_cast<RE::Actor*>(a_state->horseRefHandle.get().get()) : nullptr;
        if (horse) {
            horse->SetRotationZ(horse->data.angle.z - a_delta.yaw);
            a_state->horseCurrentDirection = horse->GetHeading(false);
        }

        // pitch: no direct field write -- the engine consumes and resets freeRotation
        // every frame here (round 11 measured: our fy.y write produced only a small
        // orbit-position shift and was wiped, d_fy = 0/50 frames; fx is the orbit yaw
        // offset, round 10 went horizontal). The stick-validated HandleLookInput path
        // in OnHorseCameraPreUpdate is the pitch channel.
    }
}
