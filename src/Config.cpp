#include "Config.h"

#include <SimpleIni.h>
#include <spdlog/spdlog.h>

#include "Util.h"

namespace GyroAim {
    namespace {
        constexpr auto kFileName = L"GyroAim.ini";

        [[nodiscard]] std::string Narrow(const std::wstring& a_wide) {
            if (a_wide.empty()) {
                return {};
            }
            const auto size = WideCharToMultiByte(CP_UTF8, 0, a_wide.data(), static_cast<int>(a_wide.size()), nullptr,
                                                  0, nullptr, nullptr);
            std::string result(static_cast<std::size_t>(size), '\0');
            WideCharToMultiByte(CP_UTF8, 0, a_wide.data(), static_cast<int>(a_wide.size()), result.data(), size,
                                nullptr, nullptr);
            return result;
        }

        void ApplyLogLevel(const std::string& a_level) {
            if (a_level == "trace") {
                spdlog::set_level(spdlog::level::trace);
            } else if (a_level == "debug") {
                spdlog::set_level(spdlog::level::debug);
            } else if (a_level == "warn") {
                spdlog::set_level(spdlog::level::warn);
            } else if (a_level == "error") {
                spdlog::set_level(spdlog::level::err);
            } else {
                spdlog::set_level(spdlog::level::info);
            }
        }

        [[nodiscard]] bool ParseBool(const std::wstring& a_value) {
            return a_value == L"true"sv || a_value == L"1"sv || a_value == L"yes"sv;
        }

        [[nodiscard]] GyroAxis ParseAxis(const std::wstring& a_value) {
            if (a_value == L"y"sv) {
                return GyroAxis::kY;
            }
            if (a_value == L"z"sv) {
                return GyroAxis::kZ;
            }
            return GyroAxis::kX;
        }

        void WriteDefaults(const std::filesystem::path& a_path) {
            CSimpleIniW ini;
            ini.SetUnicode();
            ini.SetValue(L"General", L"inputSource", L"steam");
            ini.SetValue(L"General", L"logLevel", L"info");
            ini.SetValue(L"Steam", L"steamApiPath", L"");
            ini.SetValue(L"Gyro", L"gyroData", L"quat");
            ini.SetValue(L"Gyro", L"gyroSpace", L"player");
            ini.SetValue(L"Gyro", L"calibration", L"true");
            ini.SetValue(L"Gyro", L"yawRelaxFactor", L"1.41");
            ini.SetValue(L"Gyro", L"yawSensitivity", L"0.25");
            ini.SetValue(L"Gyro", L"pitchSensitivity", L"0.25");
            ini.SetValue(L"Gyro", L"yawAxis", L"y");
            ini.SetValue(L"Gyro", L"pitchAxis", L"x");
            ini.SetValue(L"Gyro", L"invertYaw", L"false");
            ini.SetValue(L"Gyro", L"invertPitch", L"false");
            ini.SetValue(L"Gyro", L"yawDeadzoneDegPerSec", L"4.0");
            ini.SetValue(L"Gyro", L"pitchDeadzoneDegPerSec", L"4.0");
            ini.SetValue(L"Gyro", L"smoothing", L"0.7");
            ini.SetValue(L"Gyro", L"pitchClampDeg", L"80.0");
            ini.SetValue(L"Activation", L"enableOnBowDraw", L"true");
            if (ini.SaveFile(a_path.wstring().c_str()) < 0) {
                SKSE::log::warn("could not write default config to {}"sv, a_path.string());
            } else {
                SKSE::log::info("wrote default config to {}"sv, a_path.string());
            }
        }
    }

    Config* Config::GetSingleton() {
        static Config instance;
        return &instance;
    }

    bool Config::Load() {
        _filePath = Util::GetPluginDirectory() / kFileName;

        CSimpleIniW ini;
        ini.SetUnicode();
        const bool existed = std::filesystem::exists(_filePath);
        ini.LoadFile(_filePath.wstring().c_str());

        auto getString = [&ini](const wchar_t* a_section, const wchar_t* a_key, const wchar_t* a_default) {
            const wchar_t* value = ini.GetValue(a_section, a_key, a_default);
            return value ? std::wstring(value) : std::wstring();
        };
        auto getFloat = [&getString](const wchar_t* a_section, const wchar_t* a_key, float a_default) {
            const auto value = getString(a_section, a_key, nullptr);
            return value.empty() ? a_default : static_cast<float>(wcstof(value.c_str(), nullptr));
        };
        auto getBool = [&getString](const wchar_t* a_section, const wchar_t* a_key, bool a_default) {
            const auto value = getString(a_section, a_key, nullptr);
            return value.empty() ? a_default : ParseBool(value);
        };

        const auto source = getString(L"General", L"inputSource", L"steam");
        if (source == L"steam"sv) {
            _settings.inputSource = InputSourceType::kSteam;
        } else if (source == L"dsu"sv) {
            _settings.inputSource = InputSourceType::kDsu;
        } else if (source == L"hid"sv) {
            _settings.inputSource = InputSourceType::kHid;
        } else {
            _settings.inputSource = InputSourceType::kUnknown;
            SKSE::log::error("unknown inputSource '{}'"sv, Narrow(source));
        }

        // injectionMode is retired (2026-10-14 rollback): only the freeRotation
        // increment baseline ships; leftover ini values are ignored.
        _settings.logLevel = Narrow(getString(L"General", L"logLevel", L"info"));

        _settings.steamApiPath = Narrow(getString(L"Steam", L"steamApiPath", L""));

        const auto gyroData = getString(L"Gyro", L"gyroData", L"quat");
        _settings.gyroData = gyroData == L"rotVel"sv ? GyroData::kRotVel : GyroData::kQuat;

        const auto gyroSpace = getString(L"Gyro", L"gyroSpace", L"player");
        if (gyroSpace == L"local"sv) {
            _settings.gyroSpace = GyroSpace::kLocal;
        } else if (gyroSpace == L"world"sv) {
            _settings.gyroSpace = GyroSpace::kWorld;
        } else {
            _settings.gyroSpace = GyroSpace::kPlayer;
        }

        _settings.calibration = getBool(L"Gyro", L"calibration", true);
        _settings.yawRelaxFactor = getFloat(L"Gyro", L"yawRelaxFactor", 1.41F);
        _settings.yawSensitivity = getFloat(L"Gyro", L"yawSensitivity", 0.25F);
        _settings.pitchSensitivity = getFloat(L"Gyro", L"pitchSensitivity", 0.25F);
        _settings.yawAxis = ParseAxis(getString(L"Gyro", L"yawAxis", L"y"));
        _settings.pitchAxis = ParseAxis(getString(L"Gyro", L"pitchAxis", L"x"));
        _settings.invertYaw = getBool(L"Gyro", L"invertYaw", false);
        _settings.invertPitch = getBool(L"Gyro", L"invertPitch", false);
        _settings.yawDeadzoneDegPerSec = getFloat(L"Gyro", L"yawDeadzoneDegPerSec", 4.0F);
        _settings.pitchDeadzoneDegPerSec = getFloat(L"Gyro", L"pitchDeadzoneDegPerSec", 4.0F);
        _settings.smoothing = std::clamp(getFloat(L"Gyro", L"smoothing", 0.7F), 0.0F, 1.0F);
        _settings.pitchClampDeg = getFloat(L"Gyro", L"pitchClampDeg", 80.0F);

        _settings.enableOnBowDraw = getBool(L"Activation", L"enableOnBowDraw", true);

        ApplyLogLevel(_settings.logLevel);
        if (!existed) {
            WriteDefaults(_filePath);
        }
        return true;
    }
}
