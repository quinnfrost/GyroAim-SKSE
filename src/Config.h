#pragma once

namespace GyroAim {
    enum class InputSourceType : std::uint32_t { kSteam, kDsu, kHid, kUnknown };

    enum class GyroAxis : std::uint32_t { kX, kY, kZ };

    // D5: orientation frame-diff primary, angular velocity fallback.
    enum class GyroData : std::uint32_t { kQuat, kRotVel };

    enum class GyroSpace : std::uint32_t { kPlayer, kLocal, kWorld };

    struct Settings {
        InputSourceType inputSource{InputSourceType::kUnknown};
        std::string logLevel{"info"};

        std::string steamApiPath;

        GyroData gyroData{GyroData::kQuat};
        GyroSpace gyroSpace{GyroSpace::kPlayer};
        bool calibration{true};
        float yawRelaxFactor{1.41F};

        float yawSensitivity{0.25F};
        float pitchSensitivity{0.1875F};
        GyroAxis yawAxis{GyroAxis::kY};
        GyroAxis pitchAxis{GyroAxis::kX};
        bool invertYaw{false};
        bool invertPitch{false};
        float yawDeadzoneDegPerSec{4.0F};
        float pitchDeadzoneDegPerSec{4.0F};
        float smoothing{0.7F};
        float pitchClampDeg{80.0F};

        bool enableOnBowDraw{true};
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
