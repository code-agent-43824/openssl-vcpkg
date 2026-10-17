# DLLs linked with the static CRT (/MT, /MTd): they import only Windows system DLLs.
set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE static)
set(VCPKG_LIBRARY_LINKAGE dynamic)
