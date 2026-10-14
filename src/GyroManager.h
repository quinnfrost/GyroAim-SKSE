#pragma once

#include "GyroSource.h"

#pragma warning(push, 0)
#include "ThirdParty/GamepadMotionHelpers/GamepadMotion.hpp"
#pragma warning(pop)

namespace GyroAim {
    struct Settings;

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

        [[nodiscard]] bool IsBowAiming() const;
        [[nodiscard]] bool IsActive() const;
        [[nodiscard]] float Tick(TimePoint a_now);
        [[nodiscard]] bool ComputeFrameDelta(float a_dt, FrameDelta& a_out);
        [[nodiscard]] bool OrientationDeltaDegrees(const Settings& a_settings, float& a_pitchDeg, float& a_yawDeg);
        void ConvertSpace(const Settings& a_settings, float a_x, float a_y, float a_z, float& a_pitch, float& a_yaw);
        void ResetProcessing();
        void LogHeartbeat(RE::TESCameraState* a_state, TimePoint a_now);
        void ApplyToThirdPerson(RE::ThirdPersonState* a_state, const FrameDelta& a_delta);
        void ApplyToFirstPerson(const FrameDelta& a_delta);

        IGyroSource* _source{nullptr};

        // Calibration, sensor fusion and space conversion (Phase 2).
        GamepadMotion _motion;

        struct Orientation {
            float w{1.0F};
            float x{0.0F};
            float y{0.0F};
            float z{0.0F};
        };
        Orientation _prevOrientation;
        bool _hasOrientation{false};

        TimePoint _lastUpdate{};
        TimePoint _lastHeartbeat{};
        bool _hasTimestamp{false};

        float _smoothYaw{0.0F};
        float _smoothPitch{0.0F};
        float _pitchAccum{0.0F};

        // Last pipeline outputs, pre-deadzone, for the debug motion log.
        float _lastYawDeg{0.0F};
        float _lastPitchDeg{0.0F};
        float _lastAccelMag{0.0F};
    };
}
