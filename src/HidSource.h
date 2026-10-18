#pragma once

#include "GyroSource.h"

namespace GyroAim {
    // SDL2 sensor source (Phase 3 "hid"): SDL's hidapi backends (DualShock 4, DualSense,
    // Switch Pro, Steam Deck) read the IMU through shared-mode HID, coexisting with
    // Steam Input and DS4Windows-style tools. In SDL2 the motion-sensor API lives on
    // the game-controller interface (the Joystick-level sensor API is SDL3-only).
    // SDL's sensor frame is the PlayStation convention (Y-up, X = pitch, Y = yaw,
    // Z = roll), which is exactly the GamepadMotionHelpers space.
    class HidSource final : public IGyroSource {
    public:
        [[nodiscard]] static HidSource* GetSingleton();

        [[nodiscard]] bool Initialize() override;
        void Shutdown() override;
        void RunFrame() override;
        [[nodiscard]] bool SampleMotion(GyroSample& a_out) override;
        [[nodiscard]] const char* Name() const override { return "hid"; }

    private:
        [[nodiscard]] bool OpenFirstGyroController();
        void CloseController();

        // SDL_GameController*, kept opaque so SDL stays out of the header.
        void* _controller{nullptr};
        // Polls to wait before re-scanning while no gyro controller is attached.
        std::uint32_t _rescanCountdown{0};
    };
}
