#pragma once

namespace GyroAim::CameraHook {
    // Detours ThirdPersonState::Update (vtable slot 0x03); gyro deltas are
    // injected in the post stage so stick input is never overwritten.
    void Install();
}
