#pragma once

#include "GyroSource.h"

namespace GyroAim {
    // Owns the gyro source and turns raw samples into per-frame camera deltas.
    class GyroManager {
    public:
        [[nodiscard]] static GyroManager* GetSingleton();

        void Initialize();
        void Shutdown();

        // Called from the camera-state Update detours on the main thread.
        void OnThirdPersonUpdate(RE::ThirdPersonState* a_state);
        void OnFirstPersonUpdate(RE::FirstPersonState* a_state);
        void OnHorseCameraUpdate(RE::HorseCameraState* a_state);

    private:
        using TimePoint = std::chrono::steady_clock::time_point;

        // Radians; not produced when the frame has no usable gyro motion.
        struct FrameDelta {
            float yaw{0.0F};
            float pitch{0.0F};
        };

        [[nodiscard]] bool IsBowDrawn() const;
        [[nodiscard]] bool IsActive() const;
        [[nodiscard]] float Tick(TimePoint a_now);
        [[nodiscard]] bool ComputeFrameDelta(float a_dt, FrameDelta& a_out);
        void ResetProcessing();
        void LogHeartbeat(RE::TESCameraState* a_state, TimePoint a_now);
        void ApplyToThirdPerson(RE::ThirdPersonState* a_state, const FrameDelta& a_delta);
        void ApplyToFirstPerson(RE::FirstPersonState* a_state, const FrameDelta& a_delta);

        IGyroSource* _source{nullptr};

        TimePoint _lastUpdate{};
        TimePoint _lastHeartbeat{};
        bool _hasTimestamp{false};

        float _smoothYaw{0.0F};
        float _smoothPitch{0.0F};
        float _pitchAccum{0.0F};
    };
}
