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

        [[nodiscard]] InjectionMethod ParseMethod(const std::wstring& a_value) {
            if (a_value == L"compose"sv) {
                return InjectionMethod::kCompose;
            }
            if (a_value == L"heading"sv) {
                return InjectionMethod::kHeading;
            }
            if (a_value == L"rider"sv) {
                return InjectionMethod::kRider;
            }
            return InjectionMethod::kField;
        }

        void WriteDefaults(const std::filesystem::path& a_path) {
            // Comments must be full comment lines ("; ..."), attached at first creation.
            CSimpleIniW ini;
            ini.SetUnicode();
            ini.SetValue(L"General", nullptr, nullptr, L"; GyroAim config. Delete this file to regenerate defaults.");
            ini.SetValue(L"General", L"logLevel", L"info", L"; trace | debug | info | warn | error");
            ini.SetValue(L"General", L"enableOnBowDraw", L"true",
                         L"; true = gyro active while drawing a bow; false = always on");
            ini.SetValue(L"Gyro", nullptr, nullptr,
                         L"; feel tuning you may want to change (per context: third/first = walking, mounted = horse)");
            ini.SetValue(L"Gyro", L"thirdYawSensitivity", L"0.25", L"; third person walking: horizontal sensitivity");
            ini.SetValue(L"Gyro", L"thirdPitchSensitivity", L"0.1875", L"; third person walking: vertical sensitivity");
            ini.SetValue(L"Gyro", L"firstYawSensitivity", L"0.25", L"; first person: horizontal sensitivity");
            ini.SetValue(L"Gyro", L"firstPitchSensitivity", L"0.1875", L"; first person: vertical sensitivity");
            ini.SetValue(L"Gyro", L"mountedYawSensitivity", L"0.25", L"; horse: horizontal sensitivity");
            ini.SetValue(L"Gyro", L"mountedPitchSensitivity", L"0.1875", L"; horse: vertical sensitivity");
            ini.SetValue(L"Gyro", L"thirdInvertYaw", L"false", L"; third person: reverse horizontal");
            ini.SetValue(L"Gyro", L"thirdInvertPitch", L"false", L"; third person: reverse vertical");
            ini.SetValue(L"Gyro", L"firstInvertYaw", L"false", L"; first person: reverse horizontal");
            ini.SetValue(L"Gyro", L"firstInvertPitch", L"false", L"; first person: reverse vertical");
            ini.SetValue(L"Gyro", L"mountedInvertYaw", L"false", L"; horse: reverse horizontal");
            ini.SetValue(L"Gyro", L"mountedInvertPitch", L"false", L"; horse: reverse vertical");
            ini.SetValue(L"Gyro", L"yawDeadzoneDegPerSec", L"4.0", L"; horizontal deadzone (deg/s)");
            ini.SetValue(L"Gyro", L"pitchDeadzoneDegPerSec", L"4.0", L"; vertical deadzone (deg/s)");
            ini.SetValue(L"Gyro", L"smoothing", L"0.7", L"; low-pass smoothing 0..1 (higher = smoother)");
            ini.SetValue(L"Gyro", L"pitchClampDeg", L"80.0",
                         L"; accumulated pitch clamp from enable point (anti-flip)");
            ini.SetValue(L"Advanced", nullptr, nullptr,
                         L"; low level -- normally leave alone; delete file to regenerate");
            ini.SetValue(L"Advanced", L"inputSource", L"steam", L"; gyro input source: steam (dsu/hid planned)");
            ini.SetValue(L"Advanced", L"steamApiPath", L"", L"; steam_api64.dll path; empty = normal DLL search");
            ini.SetValue(L"Advanced", L"gyroData", L"quat", L"; quat = fused-orientation frame diff | rotVel");
            ini.SetValue(L"Advanced", L"gyroSpace", L"player", L"; projection space: player | world | local");
            ini.SetValue(L"Advanced", L"calibration", L"true", L"; GMH stillness auto-calibration");
            ini.SetValue(L"Advanced", L"yawRelaxFactor", L"1.41", L"; player-space yaw relax factor");
            ini.SetValue(L"Advanced", L"horseLookScale", L"10.0", L"; mounted stick-unit scale for look input");
            ini.SetValue(L"Advanced", L"yawAxis", L"y", L"; local space only: yaw axis (x | y | z)");
            ini.SetValue(L"Advanced", L"pitchAxis", L"x", L"; local space only: pitch axis (x | y | z)");
            ini.SetValue(L"Advanced", L"injectThirdPersonYaw", L"field",
                         L"; field | compose (switch to compose if a mod breaks walking 3rd person yaw)");
            ini.SetValue(L"Advanced", L"injectThirdPersonPitch", L"field",
                         L"; field | compose (switch to compose if a mod breaks walking 3rd person pitch)");
            ini.SetValue(L"Advanced", L"injectFirstPersonYaw", L"field", L"; field | compose");
            ini.SetValue(L"Advanced", L"injectFirstPersonPitch", L"field", L"; field | compose");
            ini.SetValue(L"Advanced", L"injectMountedYaw", L"heading",
                         L"; heading | rider | compose (turn horse+rider / rider only / camera-only)");
            ini.SetValue(L"Advanced", L"injectMountedPitch", L"compose",
                         L"; compose | field (field = legacy freeRotation write)");
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

        const auto source = getString(L"Advanced", L"inputSource", L"steam");
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

        _settings.logLevel = Narrow(getString(L"General", L"logLevel", L"info"));
        _settings.enableOnBowDraw = getBool(L"General", L"enableOnBowDraw", true);

        // [Gyro] feel
        _settings.thirdYawSensitivity = getFloat(L"Gyro", L"thirdYawSensitivity", 0.25F);
        _settings.thirdPitchSensitivity = getFloat(L"Gyro", L"thirdPitchSensitivity", 0.1875F);
        _settings.firstYawSensitivity = getFloat(L"Gyro", L"firstYawSensitivity", 0.25F);
        _settings.firstPitchSensitivity = getFloat(L"Gyro", L"firstPitchSensitivity", 0.1875F);
        _settings.mountedYawSensitivity = getFloat(L"Gyro", L"mountedYawSensitivity", 0.25F);
        _settings.mountedPitchSensitivity = getFloat(L"Gyro", L"mountedPitchSensitivity", 0.1875F);
        _settings.thirdInvertYaw = getBool(L"Gyro", L"thirdInvertYaw", false);
        _settings.thirdInvertPitch = getBool(L"Gyro", L"thirdInvertPitch", false);
        _settings.firstInvertYaw = getBool(L"Gyro", L"firstInvertYaw", false);
        _settings.firstInvertPitch = getBool(L"Gyro", L"firstInvertPitch", false);
        _settings.mountedInvertYaw = getBool(L"Gyro", L"mountedInvertYaw", false);
        _settings.mountedInvertPitch = getBool(L"Gyro", L"mountedInvertPitch", false);
        _settings.yawDeadzoneDegPerSec = getFloat(L"Gyro", L"yawDeadzoneDegPerSec", 4.0F);
        _settings.pitchDeadzoneDegPerSec = getFloat(L"Gyro", L"pitchDeadzoneDegPerSec", 4.0F);
        _settings.smoothing = std::clamp(getFloat(L"Gyro", L"smoothing", 0.7F), 0.0F, 1.0F);
        _settings.pitchClampDeg = getFloat(L"Gyro", L"pitchClampDeg", 80.0F);

        // [Advanced] pipeline + injection methods
        _settings.steamApiPath = Narrow(getString(L"Advanced", L"steamApiPath", L""));

        const auto gyroData = getString(L"Advanced", L"gyroData", L"quat");
        _settings.gyroData = gyroData == L"rotVel"sv ? GyroData::kRotVel : GyroData::kQuat;

        const auto gyroSpace = getString(L"Advanced", L"gyroSpace", L"player");
        if (gyroSpace == L"local"sv) {
            _settings.gyroSpace = GyroSpace::kLocal;
        } else if (gyroSpace == L"world"sv) {
            _settings.gyroSpace = GyroSpace::kWorld;
        } else {
            _settings.gyroSpace = GyroSpace::kPlayer;
        }

        _settings.calibration = getBool(L"Advanced", L"calibration", true);
        _settings.yawRelaxFactor = getFloat(L"Advanced", L"yawRelaxFactor", 1.41F);
        _settings.horseLookScale = getFloat(L"Advanced", L"horseLookScale", 10.0F);
        _settings.yawAxis = ParseAxis(getString(L"Advanced", L"yawAxis", L"y"));
        _settings.pitchAxis = ParseAxis(getString(L"Advanced", L"pitchAxis", L"x"));

        _settings.injectThirdPersonYaw = ParseMethod(getString(L"Advanced", L"injectThirdPersonYaw", L"field"));
        _settings.injectThirdPersonPitch = ParseMethod(getString(L"Advanced", L"injectThirdPersonPitch", L"field"));
        _settings.injectFirstPersonYaw = ParseMethod(getString(L"Advanced", L"injectFirstPersonYaw", L"field"));
        _settings.injectFirstPersonPitch = ParseMethod(getString(L"Advanced", L"injectFirstPersonPitch", L"field"));
        _settings.injectMountedYaw = ParseMethod(getString(L"Advanced", L"injectMountedYaw", L"heading"));
        _settings.injectMountedPitch = ParseMethod(getString(L"Advanced", L"injectMountedPitch", L"compose"));

        ApplyLogLevel(_settings.logLevel);
        if (!existed) {
            WriteDefaults(_filePath);
        }
        return true;
    }
}
