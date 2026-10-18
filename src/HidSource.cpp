#include "HidSource.h"

#define SDL_MAIN_HANDLED
#include <SDL.h>

#include "Config.h"

namespace GyroAim {
    namespace {
        constexpr float kRadToDeg = 57.29577951308232F;
        constexpr float kStandardGravity = 9.80665F;
        // While no gyro controller is attached, re-scan every N polls (~2 s at 60 fps).
        constexpr std::uint32_t kRescanInterval = 120;
    }

    HidSource* HidSource::GetSingleton() {
        static HidSource instance;
        return &instance;
    }

    bool HidSource::Initialize() {
        // Joystick + gamecontroller subsystems only: manual polling, no event pump, no
        // threads, no window/haptic initialization. SDL2 exposes controller motion
        // sensors through the gamecontroller API.
        SDL_SetMainReady();
        if (SDL_InitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER) != 0) {
            SKSE::log::error("hid source unavailable: SDL_InitSubSystem failed: {}"sv, SDL_GetError());
            return false;
        }
        SDL_JoystickEventState(SDL_DISABLE);

        if (!OpenFirstGyroController()) {
            SDL_QuitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER);
            return false;
        }
        return true;
    }

    void HidSource::Shutdown() {
        CloseController();
        SDL_QuitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER);
    }

    bool HidSource::OpenFirstGyroController() {
        const int count = SDL_NumJoysticks();
        for (int i = 0; i < count; ++i) {
            if (!SDL_IsGameController(i)) {
                continue;
            }
            SDL_GameController* controller = SDL_GameControllerOpen(i);
            if (!controller) {
                continue;
            }
            if (SDL_GameControllerHasSensor(controller, SDL_SENSOR_GYRO) == SDL_TRUE &&
                SDL_GameControllerHasSensor(controller, SDL_SENSOR_ACCEL) == SDL_TRUE &&
                SDL_GameControllerSetSensorEnabled(controller, SDL_SENSOR_GYRO, SDL_TRUE) == 0 &&
                SDL_GameControllerSetSensorEnabled(controller, SDL_SENSOR_ACCEL, SDL_TRUE) == 0) {
                _controller = controller;
                SKSE::log::info("hid source: using '{}' (vid={:#06x} pid={:#06x} gyroRate={:.1f}Hz)"sv,
                                SDL_GameControllerName(controller) ? SDL_GameControllerName(controller) : "unknown",
                                SDL_GameControllerGetVendor(controller), SDL_GameControllerGetProduct(controller),
                                static_cast<double>(SDL_GameControllerGetSensorDataRate(controller, SDL_SENSOR_GYRO)));
                return true;
            }
            SDL_GameControllerClose(controller);
        }

        // Startup validation: fail loudly; the manager falls back to the next source.
        SKSE::log::error(
            "hid source unavailable: no gyro-capable game controller among {} enumerated device(s); "
            "supported: DualShock 4, DualSense, Switch Pro, Steam Deck"sv,
            count);
        return false;
    }

    void HidSource::CloseController() {
        if (_controller) {
            SDL_GameControllerClose(static_cast<SDL_GameController*>(_controller));
            _controller = nullptr;
        }
    }

    void HidSource::RunFrame() {
        if (!_controller) {
            if (_rescanCountdown > 0) {
                --_rescanCountdown;
                return;
            }
            if (OpenFirstGyroController()) {
                SKSE::log::info("hid source: controller attached"sv);
            } else {
                _rescanCountdown = kRescanInterval;
            }
            return;
        }

        SDL_GameControllerUpdate();
        if (SDL_GameControllerGetAttached(static_cast<SDL_GameController*>(_controller)) != SDL_TRUE) {
            SKSE::log::warn("hid source: controller detached; rescanning every {} polls"sv, kRescanInterval);
            CloseController();
            _rescanCountdown = kRescanInterval;
        }
    }

    bool HidSource::SampleMotion(GyroSample& a_out) {
        auto* controller = static_cast<SDL_GameController*>(_controller);
        if (!controller) {
            return false;
        }

        float gyro[3]{};
        float accel[3]{};
        if (SDL_GameControllerGetSensorData(controller, SDL_SENSOR_GYRO, gyro, 3) != 0 ||
            SDL_GameControllerGetSensorData(controller, SDL_SENSOR_ACCEL, accel, 3) != 0) {
            return false;
        }

        // SDL reports the PlayStation sensor frame (Y-up; X = pitch axis, Y = yaw axis,
        // Z = roll axis) -- identical to GamepadMotionHelpers space, so no basis change.
        // Units: gyro in rad/s, accel in m/s^2 (specific force, +Y at rest face up);
        // GMH wants deg/s and g.
        a_out = GyroSample{gyro[0] * kRadToDeg,
                           gyro[1] * kRadToDeg,
                           gyro[2] * kRadToDeg,
                           accel[0] / kStandardGravity,
                           accel[1] / kStandardGravity,
                           accel[2] / kStandardGravity,
                           1.0F,
                           0.0F,
                           0.0F,
                           0.0F,
                           false};
        return true;
    }
}
