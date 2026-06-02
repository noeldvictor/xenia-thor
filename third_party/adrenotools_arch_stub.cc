// Stub translation unit so the libadrenotools hook shared libraries
// (libmain_hook.so / libhook_impl.so) remain VALID (but empty) ndk-build targets on
// the Android-x86_64 ABI, where the real ARM64-only hook sources are not built.
// libadrenotools / Turnip are ARM64-only; the loader path that dlopens these .so is
// XE_ARCH_ARM64-guarded (src/xenia/ui/vulkan/vulkan_instance.cc), so the x86_64 .so is
// never loaded. Listing the targets in the Gradle ndk-build `targets` list requires
// them to exist for every built ABI, hence this stub.
extern "C" int xenia_adrenotools_arch_stub = 0;
