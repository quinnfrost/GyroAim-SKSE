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
        const auto& s = GyroAim::Config::GetSingleton()->GetSettings();
        const auto methodName = [](GyroAim::InjectionMethod a_method) {
            switch (a_method) {
                case GyroAim::InjectionMethod::kCompose:
                    return "compose";
                case GyroAim::InjectionMethod::kHeading:
                    return "heading";
                case GyroAim::InjectionMethod::kRider:
                    return "rider";
                case GyroAim::InjectionMethod::kOrbit:
                    return "orbit";
                case GyroAim::InjectionMethod::kField:
                default:
                    return "field";
            }
        };
        SKSE::log::info("config inject: TP(yaw={} pitch={}) FP(yaw={} pitch={}) mounted(yaw={} pitch={})"sv,
                        methodName(s.injectThirdPersonYaw), methodName(s.injectThirdPersonPitch),
                        methodName(s.injectFirstPersonYaw), methodName(s.injectFirstPersonPitch),
                        methodName(s.injectMountedYaw), methodName(s.injectMountedPitch));
        SKSE::log::info("config feel: sens TP={:.3f}/{:.3f} FP={:.3f}/{:.3f} mounted={:.3f}/{:.3f} (yaw/pitch)"sv,
                        s.thirdYawSensitivity, s.thirdPitchSensitivity, s.firstYawSensitivity, s.firstPitchSensitivity,
                        s.mountedYawSensitivity, s.mountedPitchSensitivity);
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