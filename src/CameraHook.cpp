#include "CameraHook.h"

#include "GyroManager.h"

namespace GyroAim::CameraHook {
    namespace {
        constexpr std::size_t kUpdateVfunc = 0x03;

        using UpdateFn = void (*)(RE::ThirdPersonState*, RE::BSTSmartPointer<RE::TESCameraState>&);
        UpdateFn _OriginalUpdate = nullptr;

        void Update(RE::ThirdPersonState* a_this, RE::BSTSmartPointer<RE::TESCameraState>& a_nextState) {
            _OriginalUpdate(a_this, a_nextState);
            GyroManager::GetSingleton()->OnThirdPersonUpdate(a_this);
        }
    }

    void Install() {
        REL::Relocation<std::uintptr_t> vtable{RE::ThirdPersonState::VTABLE[0]};
        _OriginalUpdate = reinterpret_cast<UpdateFn>(vtable.write_vfunc(kUpdateVfunc, &Update));
        SKSE::log::info("ThirdPersonState::Update detour installed ({:#x} -> {:#x})"sv,
                        static_cast<std::uintptr_t>(kUpdateVfunc), reinterpret_cast<std::uintptr_t>(_OriginalUpdate));
    }
}
