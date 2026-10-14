#pragma once

namespace GyroAim::CameraHook {
    // Detours TESCameraState::Update (vtable slot 0x03) for the third-person, first-person
    // and horse camera states; gyro deltas are injected in the post stage so stick input
    // is never overwritten.
    void Install();
}
