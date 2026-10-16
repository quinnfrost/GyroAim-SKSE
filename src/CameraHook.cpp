#include "CameraHook.h"

#include "GyroManager.h"

namespace GyroAim::CameraHook {
    namespace {
        constexpr std::size_t kUpdateVfunc = 0x03;

        using UpdateFn = void (*)(RE::TESCameraState*, RE::BSTSmartPointer<RE::TESCameraState>&);

        UpdateFn _OriginalThirdPersonUpdate = nullptr;
        UpdateFn _OriginalFirstPersonUpdate = nullptr;
        UpdateFn _OriginalHorseCameraUpdate = nullptr;

        using HorseLookFn = void (*)(RE::HorseCameraState*, const RE::NiPoint2&);
        HorseLookFn _OriginalHorseLookInput = nullptr;

        // The game dispatches the mounted look input every frame; rounds 15-16 proved
        // neither the forwarded argument nor PlayerControlsData::lookInputVec is the
        // engine's pitch read source (pure-gyro frames: 0 field movement either way).
        // The mounted-aim pitch carrier is the RIDER pitch (data.angle.x, mapped at
        // -0.1 rad per input unit while aiming -- see OnHorseLookInputPhase), written
        // there at input phase. The argument still carries our units as defense in
        // depth; storage is static so the reference outlives this wrapper's stack.
        RE::NiPoint2 _combinedLookInput{};

        void HorseLookInput(RE::HorseCameraState* a_self, const RE::NiPoint2& a_input) {
            const float gyroY = GyroManager::GetSingleton()->OnHorseLookInputPhase(a_self);
            _combinedLookInput = RE::NiPoint2{a_input.x, a_input.y + gyroY};
            _OriginalHorseLookInput(a_self, _combinedLookInput);
            const auto* player = RE::PlayerCharacter::GetSingleton();
            SKSE::log::debug("horse look-input ({:.5f}, {:.5f}) +({:.5f}, {:.5f}) riderPitch={:.5f}"sv, a_input.x,
                             a_input.y, 0.0F, gyroY, player ? player->data.angle.x : 0.0F);
        }

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

        // HorseCameraState derives ThirdPersonState without overriding Update, so its
        // vtable slot points at ThirdPersonState::Update. GyroManager routes it to the
        // mounted aim injection (horse heading), not the TP fields. The mounted pitch
        // sample is taken in HorseLookInput above (input phase); only the heading steer
        // runs post here.
        void HorseCameraUpdate(RE::TESCameraState* a_self, RE::BSTSmartPointer<RE::TESCameraState>& a_nextState) {
            _OriginalHorseCameraUpdate(a_self, a_nextState);
            GyroManager::GetSingleton()->OnHorseCameraUpdate(static_cast<RE::HorseCameraState*>(a_self));
        }
    }

    void Install() {
        InstallDetour(RE::ThirdPersonState::VTABLE, &ThirdPersonUpdate, _OriginalThirdPersonUpdate);
        InstallDetour(RE::FirstPersonState::VTABLE, &FirstPersonUpdate, _OriginalFirstPersonUpdate);
        InstallDetour(RE::HorseCameraState::VTABLE, &HorseCameraUpdate, _OriginalHorseCameraUpdate);

        REL::Relocation<std::uintptr_t> horseVtable{RE::HorseCameraState::VTABLE[0]};
        _OriginalHorseLookInput = reinterpret_cast<HorseLookFn>(horseVtable.write_vfunc(0x0F, &HorseLookInput));

        SKSE::log::info("camera Update detours installed: third={:#x} first={:#x} horse={:#x} horseLook={:#x}"sv,
                        reinterpret_cast<std::uintptr_t>(_OriginalThirdPersonUpdate),
                        reinterpret_cast<std::uintptr_t>(_OriginalFirstPersonUpdate),
                        reinterpret_cast<std::uintptr_t>(_OriginalHorseCameraUpdate),
                        reinterpret_cast<std::uintptr_t>(_OriginalHorseLookInput));
    }
}
