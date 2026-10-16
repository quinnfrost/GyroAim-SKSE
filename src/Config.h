#pragma once

namespace GyroAim {
    enum class InputSourceType : std::uint32_t { kSteam, kDsu, kHid, kUnknown };

    enum class GyroAxis : std::uint32_t { kX, kY, kZ };

    // D5: orientation frame-diff primary, angular velocity fallback.
    enum class GyroData : std::uint32_t { kQuat, kRotVel };

    enum class GyroSpace : std::uint32_t { kPlayer, kLocal, kWorld };

    // Injection method per context/axis ([Advanced] inject* keys).
    //   field   -- write the context's state fields post-Update (walk defaults)
    //   compose -- accumulate and compose into the orientation returned by the
    //              GetRotation consumption hook (mounted pitch default; also the
    //              fallback for walk contexts when a mod combination breaks fields)
    //   heading -- steer the actor heading (mounted yaw default; horse+rider turn
    //              together, crosshair follows -- verified)
    //   rider   -- rotate only the rider via SetRotationZ (FP-style; horse keeps its
    //              course; crosshair coupling is runtime-unverified)
    enum class InjectionMethod : std::uint32_t { kField, kCompose, kHeading, kRider };

    struct Settings {
        // [General]
        std::string logLevel{"info"};
        bool enableOnBowDraw{true};

        // [Gyro] -- feel tuning users are expected to change
        float thirdYawSensitivity{0.25F};
        float thirdPitchSensitivity{0.1875F};
        float firstYawSensitivity{0.25F};
        float firstPitchSensitivity{0.1875F};
        float mountedYawSensitivity{0.25F};
        float mountedPitchSensitivity{0.1875F};
        bool thirdInvertYaw{false};
        bool thirdInvertPitch{false};
        bool firstInvertYaw{false};
        bool firstInvertPitch{false};
        bool mountedInvertYaw{false};
        bool mountedInvertPitch{false};
        float yawDeadzoneDegPerSec{4.0F};
        float pitchDeadzoneDegPerSec{4.0F};
        float smoothing{0.7F};
        float pitchClampDeg{80.0F};

        // [Advanced] -- low level; defaults reproduce the currently verified behavior
        InputSourceType inputSource{InputSourceType::kUnknown};
        std::string steamApiPath;
        GyroData gyroData{GyroData::kQuat};
        GyroSpace gyroSpace{GyroSpace::kPlayer};
        bool calibration{true};
        float yawRelaxFactor{1.41F};
        float horseLookScale{10.0F};
        GyroAxis yawAxis{GyroAxis::kY};
        GyroAxis pitchAxis{GyroAxis::kX};
        InjectionMethod injectThirdPersonYaw{InjectionMethod::kField};
        InjectionMethod injectThirdPersonPitch{InjectionMethod::kField};
        InjectionMethod injectFirstPersonYaw{InjectionMethod::kField};
        InjectionMethod injectFirstPersonPitch{InjectionMethod::kField};
        InjectionMethod injectMountedYaw{InjectionMethod::kHeading};
        InjectionMethod injectMountedPitch{InjectionMethod::kCompose};
    };

    class Config {
    public:
        [[nodiscard]] static Config* GetSingleton();

        bool Load();

        [[nodiscard]] const Settings& GetSettings() const { return _settings; }
        [[nodiscard]] const std::filesystem::path& GetFilePath() const { return _filePath; }

    private:
        Settings _settings;
        std::filesystem::path _filePath;
    };
}
