#include "GyroManager.h"

#include "Config.h"
#include "SteamInputSource.h"

namespace GyroAim {
    namespace {
        constexpr float kDegToRad = 0.017453292519943295F;
        constexpr float kRadToDeg = 57.29577951308232F;

        [[nodiscard]] float AxisValue(const GyroSample& a_sample, GyroAxis a_axis) {
            switch (a_axis) {
                case GyroAxis::kY:
                    return a_sample.y;
                case GyroAxis::kZ:
                    return a_sample.z;
                case GyroAxis::kX:
                default:
                    return a_sample.x;
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
        const auto inputSource = Config::GetSingleton()->GetSettings().inputSource;
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

    bool GyroManager::IsBowDrawn() const {
        const auto* player = RE::PlayerCharacter::GetSingleton();
        const auto* actorState = player ? player->AsActorState() : nullptr;
        return actorState && actorState->GetAttackState() == RE::ATTACK_STATE_ENUM::kBowDrawn;
    }

    bool GyroManager::IsActive() const {
        const auto& settings = Config::GetSingleton()->GetSettings();
        return settings.enableOnBowDraw ? IsBowDrawn() : true;
    }

    void GyroManager::ResetProcessing() {
        _smoothYaw = 0.0F;
        _smoothPitch = 0.0F;
        _pitchAccum = 0.0F;
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

        SKSE::log::info("heartbeat camera={} attackState={} bowDrawn={} gyro={}"sv, CameraStateName(cameraId),
                        attackState, IsBowDrawn(), _source != nullptr);
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
        if (!_source->SampleMotion(raw) || !IsActive()) {
            ResetProcessing();
            return false;
        }

        const auto& settings = Config::GetSingleton()->GetSettings();

        float yawRate = AxisValue(raw, settings.yawAxis) * (settings.invertYaw ? -1.0F : 1.0F);
        float pitchRate = AxisValue(raw, settings.pitchAxis) * (settings.invertPitch ? -1.0F : 1.0F);
        if (std::hypot(yawRate, pitchRate) < settings.deadzoneDegPerSec) {
            yawRate = 0.0F;
            pitchRate = 0.0F;
        }

        const float alpha = 1.0F - settings.smoothing;
        _smoothYaw += alpha * (yawRate - _smoothYaw);
        _smoothPitch += alpha * (pitchRate - _smoothPitch);

        const float deltaYaw = _smoothYaw * a_dt * settings.sensitivity * kDegToRad;

        float deltaPitch = _smoothPitch * a_dt * settings.sensitivity * kDegToRad;
        const float clampRad = std::fabs(settings.pitchClampDeg) * kDegToRad;
        const float targetPitch = std::clamp(_pitchAccum + deltaPitch, -clampRad, clampRad);
        deltaPitch = targetPitch - _pitchAccum;
        _pitchAccum = targetPitch;

        a_out = FrameDelta{deltaYaw, deltaPitch};
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

    void GyroManager::OnHorseCameraUpdate(RE::HorseCameraState* a_state) {
        // HorseCameraState extends ThirdPersonState; the injection targets are identical.
        OnThirdPersonUpdate(a_state);
    }

    void GyroManager::OnFirstPersonUpdate(RE::FirstPersonState* a_state) {
        const auto now = std::chrono::steady_clock::now();
        LogHeartbeat(a_state, now);

        FrameDelta delta;
        if (!ComputeFrameDelta(Tick(now), delta)) {
            return;
        }
        ApplyToFirstPerson(a_state, delta);
        SKSE::log::debug("fp inject yaw={:.4f} pitch={:.4f} pitchOffsets=({:.2f}, {:.2f})"sv, delta.yaw, delta.pitch,
                         a_state->targetPitchOffset, a_state->currentPitchOffset);
    }

    void GyroManager::ApplyToThirdPerson(RE::ThirdPersonState* a_state, const FrameDelta& a_delta) {
        const auto& settings = Config::GetSingleton()->GetSettings();

        // Red line 3: increments only, applied after the original Update ran.
        switch (settings.injectionMode) {
            case InjectionMode::kYawPitch:
                a_state->targetYaw += a_delta.yaw;
                a_state->currentYaw += a_delta.yaw;
                a_state->freeRotation.y += a_delta.pitch;
                break;
            case InjectionMode::kFreeRotation:
            default:
                a_state->freeRotation.x += a_delta.yaw;
                a_state->freeRotation.y += a_delta.pitch;
                break;
        }
    }

    void GyroManager::ApplyToFirstPerson(RE::FirstPersonState* a_state, const FrameDelta& a_delta) {
        // FP pitch lives in targetPitchOffset/currentPitchOffset ([-100, 100]);
        // runtime validation assumes 1 offset unit == 1 degree.
        const float pitchUnits = a_delta.pitch * kRadToDeg;
        a_state->targetPitchOffset = std::clamp(a_state->targetPitchOffset + pitchUnits, -100.0F, 100.0F);
        a_state->currentPitchOffset = std::clamp(a_state->currentPitchOffset + pitchUnits, -100.0F, 100.0F);

        // FP yaw is the player's heading; add increments so stick input is preserved.
        if (auto* player = RE::PlayerCharacter::GetSingleton()) {
            player->SetRotationZ(player->data.angle.z + a_delta.yaw);
        }
    }
}
