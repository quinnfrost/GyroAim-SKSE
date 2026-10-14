#include "Log.h"

#include <spdlog/sinks/basic_file_sink.h>

namespace GyroAim::Log {
    void Init() {
        const auto directory = SKSE::log::log_directory();
        if (!directory) {
            SKSE::log::error("could not resolve the SKSE log directory"sv);
            return;
        }

        const auto path = *directory / "GyroAim.log"sv;
        auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path.string(), true);
        auto logger = std::make_shared<spdlog::logger>("GyroAim", std::move(sink));
        spdlog::set_default_logger(std::move(logger));
        spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
        spdlog::set_level(spdlog::level::info);
        spdlog::flush_on(spdlog::level::info);
        SKSE::log::info("logging to {}"sv, path.string());
    }
}
