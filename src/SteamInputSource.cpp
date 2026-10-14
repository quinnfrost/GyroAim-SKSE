#include "SteamInputSource.h"

#include "Config.h"
#include "Util.h"

namespace GyroAim {
    namespace {
        constexpr std::uint32_t kMaxControllers = 16;
    }

    SteamInputSource* SteamInputSource::GetSingleton() {
        static SteamInputSource instance;
        return &instance;
    }

    void* SteamInputSource::Resolve(const char* a_symbol) const {
        return _module ? reinterpret_cast<void*>(GetProcAddress(_module, a_symbol)) : nullptr;
    }

    bool SteamInputSource::Initialize() {
        const auto& settings = Config::GetSingleton()->GetSettings();

        std::filesystem::path dllPath;
        if (!settings.steamApiPath.empty()) {
            dllPath = std::filesystem::path(settings.steamApiPath);
            if (dllPath.is_relative()) {
                dllPath = Util::GetPluginDirectory() / dllPath;
            }
        } else {
            dllPath = L"steam_api64.dll";
        }

        _module = LoadLibraryW(dllPath.c_str());
        if (!_module) {
            SKSE::log::error("steam source unavailable: LoadLibraryW('{}') failed (WinError {})"sv, dllPath.string(),
                             GetLastError());
            return false;
        }

        wchar_t loadedPath[MAX_PATH]{};
        GetModuleFileNameW(_module, loadedPath, static_cast<DWORD>(std::size(loadedPath)));
        SKSE::log::info("loaded steam_api64 from {}"sv, std::filesystem::path(loadedPath).string());

        SteamInputFn steamInput{};
        for (auto version = 7; version >= 4 && !steamInput; --version) {
            const auto symbol = fmt::format("SteamAPI_SteamInput_v{:03}"sv, version);
            steamInput = reinterpret_cast<SteamInputFn>(Resolve(symbol.c_str()));
        }
        auto* init = reinterpret_cast<InitFn>(Resolve("SteamAPI_ISteamInput_Init"));
        _runFrame = reinterpret_cast<RunFrameFn>(Resolve("SteamAPI_ISteamInput_RunFrame"));
        _getConnectedControllers =
            reinterpret_cast<GetConnectedControllersFn>(Resolve("SteamAPI_ISteamInput_GetConnectedControllers"));
        _getMotionData = reinterpret_cast<GetMotionDataFn>(Resolve("SteamAPI_ISteamInput_GetMotionData"));

        if (!steamInput || !init || !_runFrame || !_getConnectedControllers || !_getMotionData) {
            SKSE::log::error(
                "steam source unavailable: steam_api64.dll lacks the ISteamInput flat API "
                "(SteamAPI_SteamInput_v00x + Init/RunFrame/GetConnectedControllers/GetMotionData); "
                "set [Steam] steamApiPath to a redistributable with SDK 1.51+"sv);
            return false;
        }

        _self = steamInput();
        if (!_self || !init(_self, true)) {
            SKSE::log::error(
                "steam source unavailable: ISteamInput::Init() failed (is Steam running with a gyro-capable controller?)"sv);
            return false;
        }

        SKSE::log::info("steam source initialized"sv);
        return true;
    }

    void SteamInputSource::Shutdown() {
        if (_module) {
            FreeLibrary(_module);
            _module = nullptr;
        }
        _self = nullptr;
        _handle = 0;
    }

    void SteamInputSource::RunFrame() {
        if (!_self) {
            return;
        }
        _runFrame(_self, false);

        std::uint64_t handles[kMaxControllers]{};
        _handle = _getConnectedControllers(_self, handles) > 0 ? handles[0] : 0;
    }

    bool SteamInputSource::SampleMotion(GyroSample& a_out) {
        if (!_self || _handle == 0) {
            return false;
        }

        InputMotionData data{};
        _getMotionData(_self, _handle, &data);
        a_out = GyroSample{data.rotVel[0], data.rotVel[1], data.rotVel[2]};
        return true;
    }
}
