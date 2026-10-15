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
            // Comments must be full comment lines ("; ..."), attached at first creation.
            CSimpleIniW ini;
            ini.SetUnicode();
            ini.SetValue(L"General", nullptr, nullptr, L"; GyroAim config. Delete this file to regenerate defaults.");
            ini.SetValue(L"General", L"inputSource", L"steam", L"; gyro input source: steam (dsu/hid planned)");
            ini.SetValue(L"General", L"logLevel", L"info", L"; trace | debug | info | warn | error");
            ini.SetValue(L"Steam", nullptr, nullptr, L"; settings for inputSource = steam");
            ini.SetValue(L"Steam", L"steamApiPath", L"", L"; steam_api64.dll path; empty = normal DLL search order");
            ini.SetValue(L"Gyro", nullptr, nullptr, L"; sensor data pipeline");
            ini.SetValue(L"Gyro", L"gyroData", L"quat", L"; quat = fused-orientation frame diff (default) | rotVel");
            ini.SetValue(L"Gyro", L"gyroSpace", L"player", L"; projection space: player | world | local");
            ini.SetValue(L"Gyro", L"calibration", L"true", L"; GMH stillness auto-calibration (drift compensation)");
            ini.SetValue(L"Gyro", L"yawRelaxFactor", L"1.41", L"; player-space yaw relax factor (GMH default)");
            ini.SetValue(L"Gyro", L"yawAxis", L"y", L"; local space only: yaw axis (x | y | z)");
            ini.SetValue(L"Gyro", L"pitchAxis", L"x", L"; local space only: pitch axis (x | y | z)");
            ini.SetValue(L"Feel", nullptr, nullptr, L"; per-axis feel tuning (horizontal and vertical are independent)");
            ini.SetValue(L"Feel", L"yawSensitivity", L"0.25", L"; horizontal sensitivity");
            ini.SetValue(L"Feel", L"pitchSensitivity", L"0.1875", L"; vertical sensitivity");
            ini.SetValue(L"Feel", L"invertYaw", L"false", L"; reverse horizontal direction");
            ini.SetValue(L"Feel", L"invertPitch", L"false", L"; reverse vertical direction");
            ini.SetValue(L"Feel", L"yawDeadzoneDegPerSec", L"4.0", L"; horizontal deadzone (deg/s)");
            ini.SetValue(L"Feel", L"pitchDeadzoneDegPerSec", L"4.0", L"; vertical deadzone (deg/s)");
            ini.SetValue(L"Feel", L"smoothing", L"0.7", L"; low-pass smoothing 0..1 (higher = smoother)");
            ini.SetValue(L"Feel", L"pitchClampDeg", L"80.0", L"; accumulated pitch clamp from enable point (anti-flip)");
            ini.SetValue(L"Activation", nullptr, nullptr, L"; when the gyro takes over the camera");
            ini.SetValue(L"Activation", L"enableOnBowDraw", L"true", L"; true = while drawing a bow; false = always on");
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
        _settings.yawAxis = ParseAxis(getString(L"Gyro", L"yawAxis", L"y"));
        _settings.pitchAxis = ParseAxis(getString(L"Gyro", L"pitchAxis", L"x"));

        _settings.yawSensitivity = getFloat(L"Feel", L"yawSensitivity", 0.25F);
        _settings.pitchSensitivity = getFloat(L"Feel", L"pitchSensitivity", 0.1875F);
        _settings.invertYaw = getBool(L"Feel", L"invertYaw", false);
        _settings.invertPitch = getBool(L"Feel", L"invertPitch", false);
        _settings.yawDeadzoneDegPerSec = getFloat(L"Feel", L"yawDeadzoneDegPerSec", 4.0F);
        _settings.pitchDeadzoneDegPerSec = getFloat(L"Feel", L"pitchDeadzoneDegPerSec", 4.0F);
        _settings.smoothing = std::clamp(getFloat(L"Feel", L"smoothing", 0.7F), 0.0F, 1.0F);
        _settings.pitchClampDeg = getFloat(L"Feel", L"pitchClampDeg", 80.0F);

        _settings.enableOnBowDraw = getBool(L"Activation", L"enableOnBowDraw", true);

        ApplyLogLevel(_settings.logLevel);
        if (!existed) {
            WriteDefaults(_filePath);
        }
        return true;
    }
}
