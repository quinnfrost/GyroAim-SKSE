#pragma once

#include "GyroSource.h"
#include "Util.h"

namespace GyroAim {
    // ISteamInput-based source. steam_api64.dll is loaded at runtime (D4) so
    // GOG / no-Steam installs never reference a missing DLL.
    class SteamInputSource final : public IGyroSource {
    public:
        [[nodiscard]] static SteamInputSource* GetSingleton();

        [[nodiscard]] bool Initialize() override;
        void Shutdown() override;
        void RunFrame() override;
        [[nodiscard]] bool SampleMotion(GyroSample& a_out) override;
        [[nodiscard]] const char* Name() const override { return "steam"; }

    private:
        // Matches Steamworks InputMotionData_t (stable since SDK 1.44). The flat API
        // returns it by value, which on x64 compiles to a hidden sret pointer - the
        // signature must mirror that exactly.
        struct InputMotionData {
            float rotQuat[4];   // X, Y, Z, W
            float posAccel[3];  // m/s^2
            float rotVel[3];    // deg/s
        };

        using SteamInputFn = void* (*)();
        using InitFn = bool (*)(void*, bool);
        using RunFrameFn = bool (*)(void*, bool);
        using GetConnectedControllersFn = std::int32_t (*)(void*, std::uint64_t*);
        using GetMotionDataFn = InputMotionData (*)(void*, std::uint64_t);

        [[nodiscard]] void* Resolve(const char* a_symbol) const;

        void* _self{nullptr};
        HMODULE _module{nullptr};
        RunFrameFn _runFrame{nullptr};
        GetConnectedControllersFn _getConnectedControllers{nullptr};
        GetMotionDataFn _getMotionData{nullptr};
        std::uint64_t _handle{0};
    };
}
