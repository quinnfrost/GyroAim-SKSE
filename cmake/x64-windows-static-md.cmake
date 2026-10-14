# Overlay triplet referenced by VCPKG_OVERLAY_TRIPLETS in CMakePresets.json.
#
# x64-windows-static-md (x64 / dynamic CRT / static libs), built in both
# configurations so the debug preset gets /MDd debug libs to link against.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
