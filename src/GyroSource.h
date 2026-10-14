#pragma once

namespace GyroAim {
    // Raw sensor sample from a gyro source.
    struct GyroSample {
        float x{0.0F};  // angular velocity around device X axis, deg/s
        float y{0.0F};  // angular velocity around device Y axis, deg/s
        float z{0.0F};  // angular velocity around device Z axis, deg/s
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
