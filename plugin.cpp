#include "src/CameraHook.h"
#include "src/Config.h"
#include "src/GyroManager.h"
#include "src/Log.h"

SKSEPluginLoad(const SKSE::LoadInterface* skse) {
    SKSE::Init(skse);

    GyroAim::Log::Init();
    SKSE::log::info("GyroAim v{} loading"sv, "0.1.0");

    try {
        if (!GyroAim::Config::GetSingleton()->Load()) {
            SKSE::log::error("failed to load GyroAim.ini; gyro disabled"sv);
            return true;
        }
        SKSE::log::info("config loaded: {}"sv, GyroAim::Config::GetSingleton()->GetFilePath().string());
    } catch (const std::exception& e) {
        SKSE::log::error("config load threw ({}); gyro disabled"sv, e.what());
        return true;
    }

    // Game-facing work (Steam API, camera hooks) belongs in kDataLoaded: the engine
    // and SteamAPI are not fully initialized while SKSEPluginLoad runs.
    SKSE::GetMessagingInterface()->RegisterListener([](SKSE::MessagingInterface::Message* message) {
        if (message->type == SKSE::MessagingInterface::kDataLoaded) {
            try {
                GyroAim::GyroManager::GetSingleton()->Initialize();
                GyroAim::CameraHook::Install();
            } catch (const std::exception& e) {
                SKSE::log::error("initialization threw ({}); gyro disabled"sv, e.what());
            }
        }
    });

    return true;
}