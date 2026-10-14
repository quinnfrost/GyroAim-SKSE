#include "GyroManager.h"

#include "Config.h"
#include "SteamInputSource.h"

namespace GyroAim {
    namespace {
        constexpr float kDegToRad = 0.017453292519943295F;

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

    void GyroManager::LogHeartbeat(RE::ThirdPersonState* a_state, TimePoint a_now) {
        if (a_now - _lastHeartbeat < 1s) {
            return;
        }
        _lastHeartbeat = a_now;

        const auto* camera = RE::PlayerCamera::GetSingleton();
        const auto cameraId =
            camera && camera->currentState ? static_cast<std::uint32_t>(camera->currentState->id) : ~0U;

        const auto* player = RE::PlayerCharacter::GetSingleton();
        const auto* actorState = player ? player->AsActorState() : nullptr;
        const auto attackState = actorState ? static_cast<std::uint32_t>(actorState->GetAttackState()) : ~0U;

        SKSE::log::info("heartbeat camera={} attackState={} bowDrawn={} gyro={} freeRot=({:.3f}, {:.3f})"sv,
                        CameraStateName(cameraId), attackState, IsBowDrawn(), _source != nullptr,
                        a_state->freeRotation.x, a_state->freeRotation.y);
    }

    void GyroManager::OnThirdPersonUpdate(RE::ThirdPersonState* a_state) {
        const auto now = std::chrono::steady_clock::now();
        LogHeartbeat(a_state, now);

        float dt = 0.0F;
        if (_hasTimestamp) {
            dt = std::clamp(std::chrono::duration<float>(now - _lastUpdate).count(), 0.0001F, 0.05F);
        }
        _lastUpdate = now;
        _hasTimestamp = true;

        if (!_source) {
            return;
        }

        _source->RunFrame();
        GyroSample raw;
        if (!_source->SampleMotion(raw) || !IsActive()) {
            ResetProcessing();
            return;
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

        const float deltaYaw = _smoothYaw * dt * settings.sensitivity * kDegToRad;

        float deltaPitch = _smoothPitch * dt * settings.sensitivity * kDegToRad;
        const float clampRad = std::fabs(settings.pitchClampDeg) * kDegToRad;
        const float targetPitch = std::clamp(_pitchAccum + deltaPitch, -clampRad, clampRad);
        deltaPitch = targetPitch - _pitchAccum;
        _pitchAccum = targetPitch;

        // Red line 3: increments only, applied after the original Update ran.
        switch (settings.injectionMode) {
            case InjectionMode::kYawPitch:
                a_state->targetYaw += deltaYaw;
                a_state->currentYaw += deltaYaw;
                a_state->freeRotation.y += deltaPitch;
                break;
            case InjectionMode::kFreeRotation:
            default:
                a_state->freeRotation.x += deltaYaw;
                a_state->freeRotation.y += deltaPitch;
                break;
        }
    }
}
