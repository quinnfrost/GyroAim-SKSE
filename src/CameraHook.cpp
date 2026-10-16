#include "CameraHook.h"

#include <Windows.h>
#include <tlhelp32.h>

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

        using GetRotationFn = void (*)(RE::TESCameraState*, RE::NiQuaternion&);
        GetRotationFn _OriginalHorseGetRotation = nullptr;

        // Round 22: compose the accumulated gyro pitch at the CONSUMPTION point --
        // GetRotation is where the camera pulls the state's orientation, after Update
        // (where both TDM and vanilla compose from their own state) and before the
        // camera applies it. Writing the rotation member in the Update post-stage
        // proved insufficient (round 21); whatever re-derives it after us cannot beat
        // a hook on the getter itself.
        void HorseGetRotation(RE::TESCameraState* a_self, RE::NiQuaternion& a_rotation) {
            _OriginalHorseGetRotation(a_self, a_rotation);
            GyroManager::GetSingleton()->ComposeMountedPitch(a_rotation);
        }

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

        // Diagnostic: which module owns this address (vanilla exe vs another mod DLL).
        [[nodiscard]] std::string ModuleOf(std::uintptr_t a_address) {
            HMODULE module{};
            if (!GetModuleHandleExW(
                    GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                    reinterpret_cast<LPCWSTR>(a_address), &module) ||
                !module) {
                return "unknown";
            }
            wchar_t buffer[MAX_PATH]{};
            GetModuleFileNameW(module, buffer, static_cast<DWORD>(std::size(buffer)));
            return std::filesystem::path(buffer).filename().string();
        }

        // Diagnostic: list every non-system module in the process so compatibility
        // suspects (SmoothCam / TDM / ImprovedCamera / ...) show up by name.
        void LogLoadedModules() {
            const auto snapshot =
                CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetCurrentProcessId());
            if (snapshot == INVALID_HANDLE_VALUE) {
                SKSE::log::warn("module snapshot failed (WinError {})"sv, GetLastError());
                return;
            }
            MODULEENTRY32W entry{};
            entry.dwSize = sizeof(entry);
            std::string list;
            if (Module32FirstW(snapshot, &entry)) {
                do {
                    const std::filesystem::path path(entry.szExePath);
                    const auto parent = path.parent_path().wstring();
                    if (parent.starts_with(L"C:\\Windows") || parent.starts_with(L"C:/Windows")) {
                        continue;
                    }
                    auto name = path.filename().string();
                    std::transform(name.begin(), name.end(), name.begin(),
                                   [](unsigned char c) { return std::tolower(c); });
                    const bool suspect =
                        name.find("smoothcam") != std::string::npos || name.find("directional") != std::string::npos ||
                        name.find("improvedcamera") != std::string::npos || name.find("gyroaim") != std::string::npos;
                    list += (suspect ? " [*] " : " ") + path.filename().string();
                } while (Module32NextW(snapshot, &entry));
            }
            CloseHandle(snapshot);
            SKSE::log::info("loaded non-system modules ([*] = camera/gyro suspect):{}"sv, list);
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
        LogLoadedModules();

        InstallDetour(RE::ThirdPersonState::VTABLE, &ThirdPersonUpdate, _OriginalThirdPersonUpdate);
        InstallDetour(RE::FirstPersonState::VTABLE, &FirstPersonUpdate, _OriginalFirstPersonUpdate);
        InstallDetour(RE::HorseCameraState::VTABLE, &HorseCameraUpdate, _OriginalHorseCameraUpdate);

        REL::Relocation<std::uintptr_t> horseVtable{RE::HorseCameraState::VTABLE[0]};
        const auto previousRotation = horseVtable.write_vfunc(0x04, &HorseGetRotation);
        _OriginalHorseGetRotation = reinterpret_cast<GetRotationFn>(previousRotation);
        const auto previousLook = horseVtable.write_vfunc(0x0F, &HorseLookInput);
        _OriginalHorseLookInput = reinterpret_cast<HorseLookFn>(previousLook);

        // Diagnostic: who composes the mounted camera rotation each frame -- vtable
        // slot 0x0E is ThirdPersonState::UpdateRotation; TDM overrides exactly this slot
        // for HorseCameraState in its source, so a non-exe owner here implicates it (or
        // whoever else replaced the mounted rotation composer).
        const auto rotationComposer = *reinterpret_cast<std::uintptr_t*>(horseVtable.address() + 0x0E * sizeof(void*));
        const auto thirdComposer =
            *reinterpret_cast<std::uintptr_t*>(RELOCATION_ID(256647, 205236).address() + 0x0E * sizeof(void*));
        SKSE::log::info("rotation composer owners: horse(0x0E)={} third(0x0E)={}"sv, ModuleOf(rotationComposer),
                        ModuleOf(thirdComposer));

        // Diagnostic: module owning each displaced function -- vanilla exe means we
        // chained onto stock code, a DLL name means another mod hooked it first and
        // we are now inside its chain (or it will chain after us, install order).
        SKSE::log::info("hook chain owners: third={} first={} horse={} horseLook={} horseGetRotation={}"sv,
                        ModuleOf(reinterpret_cast<std::uintptr_t>(_OriginalThirdPersonUpdate)),
                        ModuleOf(reinterpret_cast<std::uintptr_t>(_OriginalFirstPersonUpdate)),
                        ModuleOf(reinterpret_cast<std::uintptr_t>(_OriginalHorseCameraUpdate)), ModuleOf(previousLook),
                        ModuleOf(previousRotation));
        SKSE::log::info("camera Update detours installed: third={:#x} first={:#x} horse={:#x} horseLook={:#x}"sv,
                        reinterpret_cast<std::uintptr_t>(_OriginalThirdPersonUpdate),
                        reinterpret_cast<std::uintptr_t>(_OriginalFirstPersonUpdate),
                        reinterpret_cast<std::uintptr_t>(_OriginalHorseCameraUpdate),
                        reinterpret_cast<std::uintptr_t>(_OriginalHorseLookInput));
    }
}
