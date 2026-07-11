/**
 ******************************************************************************
 * Xenia : Xenos Emulator Research Project                                    *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_VULKAN_VULKAN_CUSTOM_RESOLVE_EXT_H_
#define XENIA_UI_VULKAN_VULKAN_CUSTOM_RESOLVE_EXT_H_

// VK_EXT_custom_resolve (extension #628, spec v1) shim.
//
// The bundled Vulkan-Headers copy predates this extension, but the Thor's
// bundled Turnip (Mesa 26.0-devel) EXPOSES it at runtime (confirmed via driver
// .so strings: tu_emit_custom_resolve_end for a7xx/chip7 = Adreno 740). It is
// the ONLY on-tile MSAA-resolve that ALSO changes format - a final shader-
// resolve subpass reads the multisampled float16 field as an input attachment
// and writes a 1x A2B10G10R10_UNORM_PACK32 color attachment DIRECTLY to system
// memory (the MSAA source stays GMEM-resident; the A2B10 output is not GMEM-
// allocated). This deletes BD's ~35 EDRAM color ownership-transfer passes (the
// measured 30ms between-pass tile-store wall) without the off-chip float16
// materialization that made every separate-pass convert perf-dead (1.8-2fps).
//
// Values transcribed verbatim from upstream Vulkan-Headers vulkan_core.h so the
// sTypes are bit-exact for the strict Turnip driver. Guarded on the upstream
// preprocessor token so this becomes a no-op once the bundled headers catch up.
//
// NOTE: NOT available on desktop NVIDIA/AMD - this path is Thor/Turnip-only and
// must be validated on-device (desktop falls back to the LLE transfer path).

// Provides the extension's declarations on top of the Vulkan core types. Must
// be included AFTER the Vulkan headers (e.g. via vulkan_instance.h) so VkStructureType,
// VkSubpassDescriptionFlagBits, VkFormat, VkBool32, VkCommandBuffer, VKAPI_PTR
// are already defined - this header intentionally pulls in no Vulkan include of
// its own to avoid an include cycle with vulkan_provider.h.

#ifndef VK_EXT_custom_resolve
#define VK_EXT_custom_resolve 1
#define VK_EXT_CUSTOM_RESOLVE_SPEC_VERSION 1
#define VK_EXT_CUSTOM_RESOLVE_EXTENSION_NAME "VK_EXT_custom_resolve"

// VkStructureType additions.
static const VkStructureType
    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUSTOM_RESOLVE_FEATURES_EXT =
        static_cast<VkStructureType>(1000628000);
static const VkStructureType VK_STRUCTURE_TYPE_BEGIN_CUSTOM_RESOLVE_INFO_EXT =
    static_cast<VkStructureType>(1000628001);
static const VkStructureType VK_STRUCTURE_TYPE_CUSTOM_RESOLVE_CREATE_INFO_EXT =
    static_cast<VkStructureType>(1000628002);

// VkSubpassDescriptionFlagBits addition (legacy render pass).
static const VkSubpassDescriptionFlagBits
    VK_SUBPASS_DESCRIPTION_CUSTOM_RESOLVE_BIT_EXT =
        static_cast<VkSubpassDescriptionFlagBits>(0x00000008);

typedef struct VkPhysicalDeviceCustomResolveFeaturesEXT {
  VkStructureType sType;
  void* pNext;
  VkBool32 customResolve;
} VkPhysicalDeviceCustomResolveFeaturesEXT;

typedef struct VkBeginCustomResolveInfoEXT {
  VkStructureType sType;
  void* pNext;
} VkBeginCustomResolveInfoEXT;

// Chained into the graphics pipeline's fragment-output stage for the custom-
// resolve subpass (colorAttachmentCount/pColorAttachmentFormats describe the
// resolve OUTPUT attachment formats - A2B10G10R10_UNORM_PACK32 for BD's field).
typedef struct VkCustomResolveCreateInfoEXT {
  VkStructureType sType;
  const void* pNext;
  VkBool32 customResolve;
  uint32_t colorAttachmentCount;
  const VkFormat* pColorAttachmentFormats;
  VkFormat depthAttachmentFormat;
  VkFormat stencilAttachmentFormat;
} VkCustomResolveCreateInfoEXT;

typedef void(VKAPI_PTR* PFN_vkCmdBeginCustomResolveEXT)(
    VkCommandBuffer commandBuffer,
    const VkBeginCustomResolveInfoEXT* pBeginCustomResolveInfo);

#endif  // VK_EXT_custom_resolve

#endif  // XENIA_UI_VULKAN_VULKAN_CUSTOM_RESOLVE_EXT_H_
