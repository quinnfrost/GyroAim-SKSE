#include "CameraHook.h"

#include "GyroManager.h"

namespace GyroAim::CameraHook {
    namespace {
        constexpr std::size_t kUpdateVfunc = 0x03;

        using UpdateFn = void (*)(RE::TESCameraState*, RE::BSTSmartPointer<RE::TESCameraState>&);

        UpdateFn _OriginalThirdPersonUpdate = nullptr;
        UpdateFn _OriginalFirstPersonUpdate = nullptr;
        UpdateFn _OriginalHorseCameraUpdate = nullptr;

        template <class F>
        void InstallDetour(const std::array<REL::VariantID, 2>& a_vtable, F a_detour, UpdateFn& a_original) {
            REL::Relocation<std::uintptr_t> vtable{a_vtable[0]};
            a_original = reinterpret_cast<UpdateFn>(vtable.write_vfunc(kUpdateVfunc, a_detour));
        }

        void ThirdPersonUpdate(RE::TESCameraState* a_self, RE::BSTSmartPointer<RE::TESCameraState>& a_nextState) {
            _OriginalThirdPersonUpdate(a_self, a_nextState);
            GyroManager::GetSingleton()->OnThirdPersonUpdate(static_cast<RE::ThirdPersonState*>(a_self));
        }

        void FirstPersonUpdate(RE::TESCameraState* a_self, RE::BSTSmartPointer<RE::TESCameraState>& a_nextState) {
            _OriginalFirstPersonUpdate(a_self, a_nextState);
            GyroManager::GetSingleton()->OnFirstPersonUpdate(static_cast<RE::FirstPersonState*>(a_self));
        }

        // HorseCameraState derives ThirdPersonState without overriding Update, so its vtable
        // slot points at ThirdPersonState::Update and the third-person field logic applies.
        void HorseCameraUpdate(RE::TESCameraState* a_self, RE::BSTSmartPointer<RE::TESCameraState>& a_nextState) {
            _OriginalHorseCameraUpdate(a_self, a_nextState);
            GyroManager::GetSingleton()->OnHorseCameraUpdate(static_cast<RE::HorseCameraState*>(a_self));
        }
    }

    void Install() {
        InstallDetour(RE::ThirdPersonState::VTABLE, &ThirdPersonUpdate, _OriginalThirdPersonUpdate);
        InstallDetour(RE::FirstPersonState::VTABLE, &FirstPersonUpdate, _OriginalFirstPersonUpdate);
        InstallDetour(RE::HorseCameraState::VTABLE, &HorseCameraUpdate, _OriginalHorseCameraUpdate);
        SKSE::log::info("camera Update detours installed: third={:#x} first={:#x} horse={:#x}"sv,
                        reinterpret_cast<std::uintptr_t>(_OriginalThirdPersonUpdate),
                        reinterpret_cast<std::uintptr_t>(_OriginalFirstPersonUpdate),
                        reinterpret_cast<std::uintptr_t>(_OriginalHorseCameraUpdate));
    }
}
