#pragma once

#include "GyroSource.h"

namespace GyroAim {
    // Owns the gyro source and turns raw samples into per-frame camera deltas.
    class GyroManager {
    public:
        [[nodiscard]] static GyroManager* GetSingleton();

        void Initialize();
        void Shutdown();

        // Called from the ThirdPersonState::Update detour on the main thread.
        void OnThirdPersonUpdate(RE::ThirdPersonState* a_state);

    private:
        using TimePoint = std::chrono::steady_clock::time_point;

        [[nodiscard]] bool IsBowDrawn() const;
        [[nodiscard]] bool IsActive() const;
        void ResetProcessing();
        void LogHeartbeat(RE::ThirdPersonState* a_state, TimePoint a_now);

        IGyroSource* _source{nullptr};

        TimePoint _lastUpdate{};
        TimePoint _lastHeartbeat{};
        bool _hasTimestamp{false};

        float _smoothYaw{0.0F};
        float _smoothPitch{0.0F};
        float _pitchAccum{0.0F};
    };
}
