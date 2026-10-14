#pragma once

#include <Windows.h>

namespace GyroAim::Util {
    // Address anchor used to resolve our own module handle without a DllMain.
    inline char dummy = 0;

    [[nodiscard]] inline std::filesystem::path GetPluginDirectory() {
        HMODULE module{};
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                reinterpret_cast<LPCWSTR>(&dummy), &module)) {
            return {};
        }

        wchar_t buffer[MAX_PATH]{};
        GetModuleFileNameW(module, buffer, static_cast<DWORD>(std::size(buffer)));
        return std::filesystem::path(buffer).parent_path();
    }
}
