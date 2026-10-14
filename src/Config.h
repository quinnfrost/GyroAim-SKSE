#pragma once

namespace GyroAim {
    enum class InputSourceType : std::uint32_t { kSteam, kDsu, kHid, kUnknown };

    enum class InjectionMode : std::uint32_t { kFreeRotation, kYawPitch };

    enum class GyroAxis : std::uint32_t { kX, kY, kZ };

    struct Settings {
        InputSourceType inputSource{InputSourceType::kUnknown};
        InjectionMode injectionMode{InjectionMode::kFreeRotation};
        std::string logLevel{"info"};

        std::string steamApiPath;

        float sensitivity{1.0F};
        GyroAxis yawAxis{GyroAxis::kY};
        GyroAxis pitchAxis{GyroAxis::kX};
        bool invertYaw{false};
        bool invertPitch{false};
        float deadzoneDegPerSec{1.0F};
        float smoothing{0.5F};
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
