#pragma once

namespace GyroAim {
    // Raw sensor sample in the GamepadMotionHelpers space (Y-up: X = pitch,
    // Y = yaw, Z = roll). Sources translate from their native convention.
    // Units: angular velocity in deg/s, acceleration in g-force.
    struct GyroSample {
        float x{0.0F};       // angular velocity around device X (pitch), deg/s
        float y{0.0F};       // angular velocity around device Y (yaw), deg/s
        float z{0.0F};       // angular velocity around device Z (roll), deg/s
        float accelX{0.0F};  // acceleration including gravity, g
        float accelY{0.0F};
        float accelZ{0.0F};
        float quatW{1.0F};  // device orientation (W, X, Y, Z)
        float quatX{0.0F};
        float quatY{0.0F};
        float quatZ{0.0F};
        bool hasOrientation{false};
    };

    // Produces gyro samples; knows nothing about the game.
    class IGyroSource {
    public:
        virtual ~IGyroSource() = default;

        [[nodiscard]] virtual bool Initialize() = 0;
        virtual void Shutdown() = 0;
        virtual void RunFrame() = 0;
        [[nodiscard]] virtual bool SampleMotion(GyroSample& a_out) = 0;
        [[nodiscard]] virtual const char* Name() const = 0;
    };
}
