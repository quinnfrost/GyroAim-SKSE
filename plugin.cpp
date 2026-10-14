#include "src/CameraHook.h"
#include "src/Config.h"
#include "src/GyroManager.h"
#include "src/Log.h"

SKSEPluginLoad(const SKSE::LoadInterface* skse) {
    SKSE::Init(skse);

    GyroAim::Log::Init();
    SKSE::log::info("GyroAim v{} loading"sv, "0.1.0");

    if (!GyroAim::Config::GetSingleton()->Load()) {
        SKSE::log::error("failed to load GyroAim.ini; gyro disabled"sv);
        return true;
    }

    GyroAim::GyroManager::GetSingleton()->Initialize();

    SKSE::GetMessagingInterface()->RegisterListener([](SKSE::MessagingInterface::Message* message) {
        if (message->type == SKSE::MessagingInterface::kDataLoaded) {
            GyroAim::CameraHook::Install();
        }
    });

    return true;
}