/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/vulkan/vulkan_device.h"

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"
#include "xenia/base/platform.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace xe {
namespace ui {
namespace vulkan {

template <typename Structure, VkStructureType StructureType>
struct VulkanFeatures {
  Structure supported = {StructureType};
  Structure enabled = {StructureType};

  void Link(VkPhysicalDeviceFeatures2& supported_features_2,
            VkDeviceCreateInfo& device_create_info) {
    supported.pNext = supported_features_2.pNext;
    supported_features_2.pNext = &supported;
    enabled.pNext = const_cast<void*>(device_create_info.pNext);
    device_create_info.pNext = &enabled;
  }
};

std::unique_ptr<VulkanDevice> VulkanDevice::CreateIfSupported(
    const VulkanInstance* const vulkan_instance,
    const VkPhysicalDevice physical_device, const bool with_gpu_emulation,
    const bool with_swapchain) {
  assert_not_null(vulkan_instance);
  assert_not_null(physical_device);

  const VulkanInstance::Functions& ifn = vulkan_instance->functions();

  // Get supported Vulkan 1.0 properties and features.

  VkPhysicalDeviceProperties properties = {};
  ifn.vkGetPhysicalDeviceProperties(physical_device, &properties);

  // From the VkApplicationInfo specification:
  //
  // "The Khronos validation layers will treat apiVersion as the highest API
  // version the application targets, and will validate API usage against the
  // minimum of that version and the implementation version (instance or device,
  // depending on context). If an application tries to use functionality from a
  // greater version than this, a validation error will be triggered."
  //
  // "Vulkan 1.0 implementations were required to return
  // VK_ERROR_INCOMPATIBLE_DRIVER if apiVersion was larger than 1.0."
  //
  // Make sure that all usages of the API version in Xenia receive the highest
  // minor version that Xenia has been tested on.
  // Libraries such as the Vulkan Memory Allocator also may expect a minor
  // version that is known to them.
  const uint32_t unclamped_api_version = properties.apiVersion;
  const uint32_t clamped_api_minor_version = std::min(
      VK_MAKE_API_VERSION(VK_API_VERSION_VARIANT(unclamped_api_version),
                          VK_API_VERSION_MAJOR(unclamped_api_version),
                          VK_API_VERSION_MINOR(unclamped_api_version), 0),
      vulkan_instance->api_version() >= VK_MAKE_API_VERSION(0, 1, 1, 0)
          ? kHighestUsedApiMinorVersion
          : VK_MAKE_API_VERSION(0, 1, 0, 0));
  properties.apiVersion =
      VK_MAKE_API_VERSION(VK_API_VERSION_VARIANT(clamped_api_minor_version),
                          VK_API_VERSION_MAJOR(clamped_api_minor_version),
                          VK_API_VERSION_MINOR(clamped_api_minor_version),
                          VK_API_VERSION_PATCH(unclamped_api_version));

  VkPhysicalDeviceFeatures supported_features = {};
  ifn.vkGetPhysicalDeviceFeatures(physical_device, &supported_features);

  if (with_gpu_emulation) {
    if (!supported_features.independentBlend) {
      // Not trivial to work around:
      // - Affects not only the blend equation, but also the color write mask.
      // - Can't reuse the blend state of the first attachment for all because
      //   some attachments may have a format that doesn't support blending.
      // - Not possible to split the draw into per-attachment draws because of
      //   depth / stencil.
      // Not supported only on the proprietary driver for the Qualcomm
      // Adreno 4xx, where the driver is largely experimental and doesn't expose
      // a lot of the functionality available in the hardware.
      XELOGW(
          "Vulkan device '{}' doesn't support the independentBlend feature "
          "required for GPU emulation",
          properties.deviceName);
      return nullptr;
    }
  }

  // Enable needed extensions.

  std::unique_ptr<VulkanDevice> device(
      new VulkanDevice(vulkan_instance, physical_device));

  const bool get_physical_device_properties2_supported =
      vulkan_instance->extensions().ext_1_1_KHR_get_physical_device_properties2;

  // Name pointers from `requested_extensions` will be used in the enabled
  // extensions vector.
  std::unordered_map<std::string, bool*> requested_extensions;

  const auto request_promoted_extension =
      [&](const char* const name, uint32_t const major, uint32_t const minor,
          bool* const supported_ptr) {
        assert_not_null(supported_ptr);
        if (properties.apiVersion >= VK_MAKE_API_VERSION(0, major, minor, 0)) {
          *supported_ptr = true;
        } else {
          requested_extensions.emplace(name, supported_ptr);
        }
      };

#define XE_UI_VULKAN_STRUCT_EXTENSION(name) \
  requested_extensions.emplace("VK_" #name, &device->extensions_.ext_##name);
#define XE_UI_VULKAN_LOCAL_EXTENSION(name) \
  requested_extensions.emplace("VK_" #name, &ext_##name);
#define XE_UI_VULKAN_STRUCT_PROMOTED_EXTENSION(name, major, minor) \
  request_promoted_extension(                                      \
      "VK_" #name, major, minor,                                   \
      &device->extensions_.ext_##major##_##minor##_##name);
#define XE_UI_VULKAN_LOCAL_PROMOTED_EXTENSION(name, major, minor) \
  request_promoted_extension("VK_" #name, major, minor,           \
                             &ext_##major##_##minor##_##name);

  bool ext_KHR_portability_subset = false;
  bool ext_1_2_KHR_driver_properties = false;
  if (get_physical_device_properties2_supported) {
    // #164. Must be enabled according to the specification if the physical
    // device is a portability subset one.
    XE_UI_VULKAN_LOCAL_EXTENSION(KHR_portability_subset)
    // #197
    XE_UI_VULKAN_LOCAL_PROMOTED_EXTENSION(KHR_driver_properties, 1, 2)
  }

  // Used by the Vulkan Memory Allocator and potentially by Xenia.
  // #128.
  XE_UI_VULKAN_STRUCT_PROMOTED_EXTENSION(KHR_dedicated_allocation, 1, 1)
  // #147. Also must be enabled for VK_KHR_dedicated_allocation and
  // VK_KHR_sampler_ycbcr_conversion.
  XE_UI_VULKAN_STRUCT_PROMOTED_EXTENSION(KHR_get_memory_requirements2, 1, 1)
  // #158. Also must be enabled for VK_KHR_sampler_ycbcr_conversion.
  XE_UI_VULKAN_STRUCT_PROMOTED_EXTENSION(KHR_bind_memory2, 1, 1)
  if (get_physical_device_properties2_supported) {
    // #238.
    XE_UI_VULKAN_STRUCT_EXTENSION(EXT_memory_budget)
  }
  if (properties.apiVersion >= VK_MAKE_API_VERSION(0, 1, 1, 0)) {
    // #414.
    XE_UI_VULKAN_STRUCT_PROMOTED_EXTENSION(KHR_maintenance4, 1, 3)
  }

  if (with_swapchain) {
    // #2.
    XE_UI_VULKAN_STRUCT_EXTENSION(KHR_swapchain)
  }

  bool ext_1_2_KHR_sampler_mirror_clamp_to_edge = false;
  bool ext_1_1_KHR_maintenance1 = false;
  bool ext_1_2_KHR_shader_float_controls = false;
  bool ext_EXT_fragment_shader_interlock = false;
  bool ext_1_3_EXT_shader_demote_to_helper_invocation = false;
  bool ext_EXT_non_seamless_cube_map = false;
  if (with_gpu_emulation) {
    // #15.
    XE_UI_VULKAN_LOCAL_PROMOTED_EXTENSION(KHR_sampler_mirror_clamp_to_edge, 1,
                                          2)
    // #70. Must be enabled for VK_KHR_sampler_ycbcr_conversion.
    XE_UI_VULKAN_LOCAL_PROMOTED_EXTENSION(KHR_maintenance1, 1, 1)
    // #141.
    XE_UI_VULKAN_STRUCT_EXTENSION(EXT_shader_stencil_export)
    // #148.
    XE_UI_VULKAN_STRUCT_PROMOTED_EXTENSION(KHR_image_format_list, 1, 2)
    if (get_physical_device_properties2_supported) {
      // #157.
      XE_UI_VULKAN_STRUCT_PROMOTED_EXTENSION(KHR_sampler_ycbcr_conversion, 1, 1)
      // #198. Also must be enabled for VK_KHR_spirv_1_4.
      XE_UI_VULKAN_LOCAL_PROMOTED_EXTENSION(KHR_shader_float_controls, 1, 2)
      // #252.
      XE_UI_VULKAN_LOCAL_EXTENSION(EXT_fragment_shader_interlock)
      // #464 (track #6): rasterization-order attachment access - the FSI
      // alternative for the EDRAM-in-GMEM render path on Turnip (which lacks
      // fragment_shader_interlock). Detected here; consumed by the EDRAM ROAA
      // render-target path (gpu_vulkan_edram_roaa).
      XE_UI_VULKAN_STRUCT_EXTENSION(EXT_rasterization_order_attachment_access)
      // #233 (XenDroid port, 2026-08-06): in-pass reads of the CURRENT colour
      // attachments, i.e. tiler-native resolves. On a TBDR like the Adreno 740
      // every render-pass begin costs a GMEM store+reload, so xenia's EDRAM
      // resolves - which end the pass, copy, and start another - are the single
      // biggest structural cost in a frame. local_read lets the resolve happen
      // INSIDE the pass, reading the attachment on-tile, so the pass never
      // breaks. Turnip exposes it (feature audit 2026-08-03). Detected here;
      // consumed by the in-pass resolve path.
      XE_UI_VULKAN_STRUCT_PROMOTED_EXTENSION(KHR_dynamic_rendering_local_read,
                                             1, 4)
      // BD direct-native (2026-07-11): on-tile MSAA->1x resolve for the field
      // producer (renders 4x internally, resolves in tile memory to the 1x native
      // image) - avoids the off-chip MSAA materialization that made per-surface
      // convert perf-dead (1.8-2fps). Requested when supported; consumed by the
      // direct-native producer render pass.
      XE_UI_VULKAN_STRUCT_EXTENSION(EXT_multisampled_render_to_single_sampled)
      // #628 VK_EXT_custom_resolve (Turnip-only): the on-tile MSAA-resolve +
      // float16->A2B10 format-convert subpass that deletes BD's ~35 EDRAM color
      // ownership-transfer passes (the measured 30ms tile-store wall). Requested
      // when supported; consumed by the direct-native field custom-resolve pass.
      // Absent on desktop -> the field falls back to the LLE transfer path.
      XE_UI_VULKAN_STRUCT_EXTENSION(EXT_custom_resolve)
      // #277.
      XE_UI_VULKAN_LOCAL_PROMOTED_EXTENSION(
          EXT_shader_demote_to_helper_invocation, 1, 3)
      // #423.
      XE_UI_VULKAN_LOCAL_EXTENSION(EXT_non_seamless_cube_map)
      // #227 VK_KHR_fragment_shading_rate (VRS) - Thor novel-hardware speed lever:
      // coarse-shade overdraw-heavy alpha-test foliage (1 FS+alpha-test per NxN
      // block). Confirmed enumerable on both Thor drivers. Requested when supported;
      // INERT until the gpu_vrs_foliage_rate consumer sets a coarse rate.
      XE_UI_VULKAN_STRUCT_EXTENSION(KHR_fragment_shading_rate)
      // #219 VK_EXT_fragment_density_map (FDM) - Thor novel-hardware lever:
      // per-bin HW resolution downscale (GRAS_BIN_FOVEAT, gate-confirmed distinct
      // from VRS) cutting the fragment COUNT over the 3D viewport. Requested when
      // supported; INERT until a density map is attached (gpu_fdm_foliage).
      XE_UI_VULKAN_STRUCT_EXTENSION(EXT_fragment_density_map)
      // VK_EXT_extended_dynamic_state3 - pipeline-bind reducer
      // (gpu_dynamic_blend_state): move guest color BLEND enable/equation/write
      // mask to DYNAMIC state so blend variance stops minting new pipelines
      // (each bind is a TBDR context-roll). Requested when supported; the
      // needed dynamic-blend sub-features are probed + gated below. INERT until
      // the gpu_dynamic_blend_state consumer promotes blend to dynamic state.
      XE_UI_VULKAN_STRUCT_EXTENSION(EXT_extended_dynamic_state3)
    }
    if (properties.apiVersion >= VK_MAKE_API_VERSION(0, 1, 1, 0)) {
      // #237.
      XE_UI_VULKAN_STRUCT_PROMOTED_EXTENSION(KHR_spirv_1_4, 1, 2)
    }
    // #81. Push descriptors - eliminate per-draw descriptor set alloc + update +
    // bind by pushing descriptors inline into the command buffer.
    XE_UI_VULKAN_STRUCT_EXTENSION(KHR_push_descriptor)
    // #270 VK_KHR_pipeline_executable_properties - DIAGNOSTIC: query the
    // compiled Adreno (ir3) shader instruction counts per pipeline. Requested
    // when supported; INERT until the gpu_vulkan_shader_stats consumer queries
    // statistics at pipeline creation.
    XE_UI_VULKAN_STRUCT_EXTENSION(KHR_pipeline_executable_properties)
  }

#undef XE_UI_VULKAN_STRUCT_EXTENSION
#undef XE_UI_VULKAN_LOCAL_EXTENSION
#undef XE_UI_VULKAN_STRUCT_PROMOTED_EXTENSION
#undef XE_UI_VULKAN_LOCAL_PROMOTED_EXTENSION

  std::vector<const char*> enabled_extensions;
  {
    uint32_t supported_extension_count = 0;
    const VkResult get_supported_extension_count_result =
        ifn.vkEnumerateDeviceExtensionProperties(
            physical_device, nullptr, &supported_extension_count, nullptr);
    if (get_supported_extension_count_result != VK_SUCCESS &&
        get_supported_extension_count_result != VK_INCOMPLETE) {
      XELOGW("Failed to get the Vulkan device '{}' extension count",
             properties.deviceName);
      return nullptr;
    }
    if (supported_extension_count) {
      std::vector<VkExtensionProperties> supported_extensions(
          supported_extension_count);
      if (ifn.vkEnumerateDeviceExtensionProperties(
              physical_device, nullptr, &supported_extension_count,
              supported_extensions.data()) != VK_SUCCESS) {
        XELOGW("Failed to get the Vulkan device '{}' extensions",
               properties.deviceName);
        return nullptr;
      }
      assert_true(supported_extension_count == supported_extensions.size());
      // Diagnostic: log every supported device extension so we can check on-device
      // for routes like VK_EXT_external_memory_host (zero-copy guest-RAM import,
      // the only remaining hybrid-UMA option that avoids host-visible-device-local).
      for (const VkExtensionProperties& supported_extension :
           supported_extensions) {
        XELOGI("Vulkan device supported extension: {}",
               supported_extension.extensionName);
      }
      for (const VkExtensionProperties& supported_extension :
           supported_extensions) {
        const auto requested_extension_it =
            requested_extensions.find(supported_extension.extensionName);
        if (requested_extension_it == requested_extensions.cend()) {
          continue;
        }
        assert_not_null(requested_extension_it->second);
        if (!*requested_extension_it->second) {
          enabled_extensions.emplace_back(
              requested_extension_it->first.c_str());
          *requested_extension_it->second = true;
        }
      }
    }
  }

  if (with_swapchain && !device->extensions_.ext_KHR_swapchain) {
    XELOGW("Vulkan device '{}' doesn't support swapchains",
           properties.deviceName);
    return nullptr;
  }

  VkDeviceCreateInfo device_create_info = {
      VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};

  device_create_info.enabledExtensionCount =
      uint32_t(enabled_extensions.size());
  device_create_info.ppEnabledExtensionNames = enabled_extensions.data();

  // Get supported Vulkan 1.1+ and extension properties and features.
  //
  // The property and feature structures are initialized to zero or to the
  // minimum / maximum requirements for the simplicity of handling unavailable
  // VK_KHR_get_physical_device_properties2.

  VkPhysicalDeviceProperties2 properties_2 = {
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};

  VkPhysicalDeviceFeatures2 supported_features_2 = {
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};

  VulkanFeatures<VkPhysicalDeviceVulkan12Features,
                 VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES>
      features_1_2;
  VulkanFeatures<VkPhysicalDeviceVulkan13Features,
                 VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES>
      features_1_3;
  VulkanFeatures<
      VkPhysicalDevicePortabilitySubsetFeaturesKHR,
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PORTABILITY_SUBSET_FEATURES_KHR>
      features_KHR_portability_subset;
  VkPhysicalDeviceDriverPropertiesKHR properties_1_2_KHR_driver_properties = {
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES};
  VkPhysicalDeviceFloatControlsProperties
      properties_1_2_KHR_shader_float_controls = {
          VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FLOAT_CONTROLS_PROPERTIES};
  // Depth/stencil MSAA resolve (core in Vulkan 1.2). This is the TBDR-correct way
  // to serve BD's MSAA depth conversions: attach a depth RESOLVE ATTACHMENT to the
  // native pass (VkSubpassDescriptionDepthStencilResolve) so the resolve happens
  // as part of the pass's existing GMEM tile store - no extra render pass, no
  // extra tile round-trip. vkCmdResolveImage cannot do this (it is color-only), so
  // knowing the supported MODES is a prerequisite for the native-depth build.
  // Logged below; query only, nothing is enabled by it.
  VkPhysicalDeviceDepthStencilResolveProperties
      properties_1_2_depth_stencil_resolve = {
          VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_STENCIL_RESOLVE_PROPERTIES};
  VulkanFeatures<
      VkPhysicalDeviceFragmentShaderInterlockFeaturesEXT,
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADER_INTERLOCK_FEATURES_EXT>
      features_EXT_fragment_shader_interlock;
  // Track #6: rasterization-order attachment access feature (the FSI
  // alternative on Turnip enabling the transfer-free EDRAM-in-GMEM path).
  VulkanFeatures<
      VkPhysicalDeviceRasterizationOrderAttachmentAccessFeaturesEXT,
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RASTERIZATION_ORDER_ATTACHMENT_ACCESS_FEATURES_EXT>
      features_EXT_rasterization_order_attachment_access;
  VulkanFeatures<
      VkPhysicalDeviceDynamicRenderingLocalReadFeaturesKHR,
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_LOCAL_READ_FEATURES_KHR>
      features_1_4_KHR_dynamic_rendering_local_read;
  // BD direct-native: on-tile MSAA->1x resolve for the field producer.
  VulkanFeatures<
      VkPhysicalDeviceMultisampledRenderToSingleSampledFeaturesEXT,
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTISAMPLED_RENDER_TO_SINGLE_SAMPLED_FEATURES_EXT>
      features_EXT_multisampled_render_to_single_sampled;
  // VK_EXT_custom_resolve (#628, Turnip-only): on-tile MSAA-resolve+format-
  // convert subpass for BD's field (float16 -> A2B10). Shim decls in
  // vulkan_custom_resolve_ext.h.
  VulkanFeatures<VkPhysicalDeviceCustomResolveFeaturesEXT,
                 VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUSTOM_RESOLVE_FEATURES_EXT>
      features_EXT_custom_resolve;
  VulkanFeatures<
      VkPhysicalDeviceShaderDemoteToHelperInvocationFeaturesEXT,
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DEMOTE_TO_HELPER_INVOCATION_FEATURES_EXT>
      features_1_3_EXT_shader_demote_to_helper_invocation;
  VulkanFeatures<
      VkPhysicalDeviceNonSeamlessCubeMapFeaturesEXT,
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_NON_SEAMLESS_CUBE_MAP_FEATURES_EXT>
      features_EXT_non_seamless_cube_map;
  // #227 VK_KHR_fragment_shading_rate (VRS) - coarse-shade overdraw foliage.
  VulkanFeatures<
      VkPhysicalDeviceFragmentShadingRateFeaturesKHR,
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_FEATURES_KHR>
      features_KHR_fragment_shading_rate;
  // #219 VK_EXT_fragment_density_map (FDM) - per-bin HW resolution downscale.
  VulkanFeatures<
      VkPhysicalDeviceFragmentDensityMapFeaturesEXT,
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_DENSITY_MAP_FEATURES_EXT>
      features_EXT_fragment_density_map;
  // VK_EXT_extended_dynamic_state3 - dynamic color blend (gpu_dynamic_blend_state).
  VulkanFeatures<
      VkPhysicalDeviceExtendedDynamicState3FeaturesEXT,
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_FEATURES_EXT>
      features_EXT_extended_dynamic_state3;
  // #270 VK_KHR_pipeline_executable_properties (DIAGNOSTIC shader-stats).
  VulkanFeatures<
      VkPhysicalDevicePipelineExecutablePropertiesFeaturesKHR,
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_EXECUTABLE_PROPERTIES_FEATURES_KHR>
      features_KHR_pipeline_executable_properties;
  // FDM properties (the HW density-map texel size) - queried so the density-image
  // consumer sizes it correctly instead of assuming a constant.
  VkPhysicalDeviceFragmentDensityMapPropertiesEXT
      properties_EXT_fragment_density_map = {
          VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_DENSITY_MAP_PROPERTIES_EXT};
  VkPhysicalDevicePushDescriptorPropertiesKHR
      properties_KHR_push_descriptor = {
          VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PUSH_DESCRIPTOR_PROPERTIES_KHR};

  if (get_physical_device_properties2_supported) {
    if (properties.apiVersion >= VK_MAKE_API_VERSION(0, 1, 2, 0)) {
      features_1_2.Link(supported_features_2, device_create_info);
    }
    if (properties.apiVersion >= VK_MAKE_API_VERSION(0, 1, 3, 0)) {
      features_1_3.Link(supported_features_2, device_create_info);
    } else {
      if (ext_1_3_EXT_shader_demote_to_helper_invocation) {
        features_1_3_EXT_shader_demote_to_helper_invocation.Link(
            supported_features_2, device_create_info);
      }
    }
    if (ext_KHR_portability_subset) {
      features_KHR_portability_subset.Link(supported_features_2,
                                           device_create_info);
    }
    if (ext_1_2_KHR_driver_properties) {
      properties_1_2_KHR_driver_properties.pNext = properties_2.pNext;
      properties_2.pNext = &properties_1_2_KHR_driver_properties;
    }
    if (ext_1_2_KHR_shader_float_controls) {
      properties_1_2_KHR_shader_float_controls.pNext = properties_2.pNext;
      properties_2.pNext = &properties_1_2_KHR_shader_float_controls;
    }
    if (properties.apiVersion >= VK_MAKE_API_VERSION(0, 1, 2, 0)) {
      properties_1_2_depth_stencil_resolve.pNext = properties_2.pNext;
      properties_2.pNext = &properties_1_2_depth_stencil_resolve;
    }
    if (ext_EXT_fragment_shader_interlock) {
      features_EXT_fragment_shader_interlock.Link(supported_features_2,
                                                  device_create_info);
    }
    if (device->extensions_.ext_1_4_KHR_dynamic_rendering_local_read) {
      features_1_4_KHR_dynamic_rendering_local_read.Link(supported_features_2,
                                                        device_create_info);
    }
    if (device->extensions_.ext_EXT_rasterization_order_attachment_access) {
      features_EXT_rasterization_order_attachment_access.Link(
          supported_features_2, device_create_info);
    }
    if (device->extensions_.ext_EXT_multisampled_render_to_single_sampled) {
      features_EXT_multisampled_render_to_single_sampled.Link(
          supported_features_2, device_create_info);
    }
    if (device->extensions_.ext_EXT_custom_resolve) {
      features_EXT_custom_resolve.Link(supported_features_2, device_create_info);
    }
    if (ext_EXT_non_seamless_cube_map) {
      features_EXT_non_seamless_cube_map.Link(supported_features_2,
                                              device_create_info);
    }
    if (device->extensions_.ext_KHR_fragment_shading_rate) {
      features_KHR_fragment_shading_rate.Link(supported_features_2,
                                              device_create_info);
    }
    if (device->extensions_.ext_EXT_fragment_density_map) {
      features_EXT_fragment_density_map.Link(supported_features_2,
                                             device_create_info);
    }
    if (device->extensions_.ext_EXT_extended_dynamic_state3) {
      features_EXT_extended_dynamic_state3.Link(supported_features_2,
                                                device_create_info);
    }
    if (device->extensions_.ext_KHR_pipeline_executable_properties) {
      features_KHR_pipeline_executable_properties.Link(supported_features_2,
                                                       device_create_info);
    }
    if (device->extensions_.ext_KHR_push_descriptor) {
      properties_KHR_push_descriptor.pNext = properties_2.pNext;
      properties_2.pNext = &properties_KHR_push_descriptor;
    }
    if (device->extensions_.ext_EXT_fragment_density_map) {
      properties_EXT_fragment_density_map.pNext = properties_2.pNext;
      properties_2.pNext = &properties_EXT_fragment_density_map;
    }
    // FDM (VK_EXT_fragment_density_map) is now ENABLED (not query-only) via
    // features_EXT_fragment_density_map above when supported - its .supported bits
    // are populated by the vkGetPhysicalDeviceFeatures2 below. It shrinks whole
    // GMEM bins pre-raster (GRAS_BIN_FOVEAT) -> cuts the raster/depth/shade
    // fragment COUNT (the BD overdraw floor), distinct from the shipped VRS
    // (shading-rate only). INERT until a density map is attached to a render pass.
    ifn.vkGetPhysicalDeviceProperties2(physical_device, &properties_2);
    ifn.vkGetPhysicalDeviceFeatures2(physical_device, &supported_features_2);

    // GPU feature-gap audit (2026-06-20): one-time log of high-value FEATURE
    // bits that Turnip exposes but xenia does not yet exploit, so a lever build
    // targets confirmed-usable features (extension-listed != feature-enabled).
    // See docs/research/20260620-adreno-turnip-feature-gap-audit.md.
    XELOGI(
        "GPU feature-gap audit: shaderFloat16={} bufferDeviceAddress={} "
        "descriptorIndexing={} runtimeDescriptorArray={} "
        "sampledImageArrayNonUniformIndexing={} descriptorBindingPartiallyBound={} "
        "dynamicRendering={} synchronization2={} "
        "roaa_color={} roaa_depth={}",
        features_1_2.supported.shaderFloat16,
        features_1_2.supported.bufferDeviceAddress,
        features_1_2.supported.descriptorIndexing,
        features_1_2.supported.runtimeDescriptorArray,
        features_1_2.supported.shaderSampledImageArrayNonUniformIndexing,
        features_1_2.supported.descriptorBindingPartiallyBound,
        features_1_3.supported.dynamicRendering,
        features_1_3.supported.synchronization2,
        features_EXT_rasterization_order_attachment_access.supported
            .rasterizationOrderColorAttachmentAccess,
        features_EXT_rasterization_order_attachment_access.supported
            .rasterizationOrderDepthAttachmentAccess);
    // FDM (Fragment Density Map) sizing for the foliage-overdraw lever. The
    // nonSubsampledImages bit is the load-bearing one: true => ~1wk attach-to-
    // existing-RTs path; false => subsampled-RT rearch.
    XELOGI(
        "GPU FDM audit: fragmentDensityMap={} nonSubsampledImages={} "
        "dynamic={}",
        features_EXT_fragment_density_map.supported.fragmentDensityMap,
        features_EXT_fragment_density_map.supported
            .fragmentDensityMapNonSubsampledImages,
        features_EXT_fragment_density_map.supported.fragmentDensityMapDynamic);
  }

  uint32_t queue_family_count = 0;
  ifn.vkGetPhysicalDeviceQueueFamilyProperties(physical_device,
                                               &queue_family_count, nullptr);
  std::vector<VkQueueFamilyProperties> queue_families(queue_family_count);
  ifn.vkGetPhysicalDeviceQueueFamilyProperties(
      physical_device, &queue_family_count, queue_families.data());

  device->queue_families_.resize(queue_family_count);

  uint32_t first_queue_family_graphics_compute_sparse_binding = UINT32_MAX;
  uint32_t first_queue_family_graphics_compute = UINT32_MAX;
  uint32_t first_queue_family_sparse_binding = UINT32_MAX;
  bool has_presentation_queue_family = false;

  for (uint32_t queue_family_index = 0; queue_family_index < queue_family_count;
       ++queue_family_index) {
    QueueFamily& queue_family = device->queue_families_[queue_family_index];
    const VkQueueFamilyProperties& queue_family_properties =
        queue_families[queue_family_index];

    const VkQueueFlags queue_unsupported_flags =
        ~queue_family_properties.queueFlags;

    if (!(queue_unsupported_flags &
          (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT))) {
      first_queue_family_graphics_compute =
          std::min(queue_family_index, first_queue_family_graphics_compute);
    }

    if (with_gpu_emulation && supported_features.sparseBinding &&
        !(queue_unsupported_flags & VK_QUEUE_SPARSE_BINDING_BIT)) {
      first_queue_family_sparse_binding =
          std::min(queue_family_index, first_queue_family_sparse_binding);
      if (!(queue_unsupported_flags &
            (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT))) {
        first_queue_family_graphics_compute_sparse_binding =
            std::min(queue_family_index,
                     first_queue_family_graphics_compute_sparse_binding);
      }
    }

    if (with_swapchain) {
#if XE_PLATFORM_WIN32
      queue_family.may_support_presentation =
          vulkan_instance->extensions().ext_KHR_win32_surface &&
          ifn.vkGetPhysicalDeviceWin32PresentationSupportKHR(
              physical_device, queue_family_index);
#else
      queue_family.may_support_presentation = true;
#endif
      if (queue_family.may_support_presentation) {
        queue_family.queues.resize(
            std::max(size_t(1), queue_family.queues.size()));
        has_presentation_queue_family = true;
      }
    }
  }

  if (first_queue_family_graphics_compute == UINT32_MAX) {
    // Not valid according to the Vulkan specification, but for safety.
    XELOGW(
        "Vulkan device '{}' doesn't provide a graphics and compute queue "
        "family",
        properties.deviceName);
    return nullptr;
  }

  if (with_swapchain && !has_presentation_queue_family) {
    XELOGW(
        "Vulkan device '{}' doesn't provide a queue family that supports "
        "presentation",
        properties.deviceName);
    return nullptr;
  }

  // Get the queues to create.

  if (first_queue_family_sparse_binding == UINT32_MAX) {
    // Not valid not to provide a sparse binding queue if the sparseBinding
    // feature is supported according to the Vulkan specification, but for
    // safety and simplicity.
    supported_features.sparseBinding = VK_FALSE;
  }
  if (!supported_features.sparseBinding) {
    supported_features.sparseResidencyBuffer = VK_FALSE;
    supported_features.sparseResidencyImage2D = VK_FALSE;
    supported_features.sparseResidencyImage3D = VK_FALSE;
    supported_features.sparseResidency2Samples = VK_FALSE;
    supported_features.sparseResidency4Samples = VK_FALSE;
    supported_features.sparseResidency8Samples = VK_FALSE;
    supported_features.sparseResidency16Samples = VK_FALSE;
    supported_features.sparseResidencyAliased = VK_FALSE;
  }

  // Prefer using one queue for everything whenever possible for simplicity.
  // TODO(Triang3l): Research if separate queues for purposes like composition,
  // swapchain image presentation, and sparse binding, may be beneficial.

  if (first_queue_family_graphics_compute_sparse_binding != UINT32_MAX) {
    device->queue_family_graphics_compute_ =
        first_queue_family_graphics_compute_sparse_binding;
    device->queue_family_sparse_binding_ =
        first_queue_family_graphics_compute_sparse_binding;
  } else {
    device->queue_family_graphics_compute_ =
        first_queue_family_graphics_compute;
    device->queue_family_sparse_binding_ = first_queue_family_sparse_binding;
  }

  device->queue_families_[device->queue_family_graphics_compute_].queues.resize(
      std::max(size_t(1),
               device->queue_families_[device->queue_family_graphics_compute_]
                   .queues.size()));
  if (device->queue_family_sparse_binding_ != UINT32_MAX) {
    device->queue_families_[device->queue_family_sparse_binding_].queues.resize(
        std::max(size_t(1),
                 device->queue_families_[device->queue_family_sparse_binding_]
                     .queues.size()));
  }

  size_t max_enabled_queues_per_family = 0;
  for (const QueueFamily& queue_family : device->queue_families_) {
    max_enabled_queues_per_family =
        std::max(queue_family.queues.size(), max_enabled_queues_per_family);
  }
  const std::vector<float> queue_priorities(max_enabled_queues_per_family,
                                            1.0f);
  std::vector<VkDeviceQueueCreateInfo> queue_create_infos;
  for (size_t queue_family_index = 0;
       queue_family_index < device->queue_families_.size();
       ++queue_family_index) {
    const QueueFamily& queue_family =
        device->queue_families_[queue_family_index];
    if (queue_family.queues.empty()) {
      continue;
    }
    VkDeviceQueueCreateInfo& queue_create_info =
        queue_create_infos.emplace_back();
    queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_create_info.pNext = nullptr;
    queue_create_info.flags = 0;
    queue_create_info.queueFamilyIndex = uint32_t(queue_family_index);
    queue_create_info.queueCount = uint32_t(queue_family.queues.size());
    queue_create_info.pQueuePriorities = queue_priorities.data();
  }
  device_create_info.queueCreateInfoCount = uint32_t(queue_create_infos.size());
  device_create_info.pQueueCreateInfos = queue_create_infos.data();

  // Enable needed features and copy the properties.
  //
  // Enabling only actually used features because drivers may take more optimal
  // paths when certain features are disabled. Also, in VK_EXT_shader_object,
  // the state that the application must set for the draw depends on which
  // features are enabled.

  device->properties_.apiVersion = properties.apiVersion;
  device->properties_.driverVersion = properties.driverVersion;
  device->properties_.vendorID = properties.vendorID;
  device->properties_.deviceID = properties.deviceID;
  std::strcpy(device->properties_.deviceName, properties.deviceName);

  XELOGI(
      "Vulkan device '{}': API {}.{}.{} ({}.{} used), vendor 0x{:04X}, device "
      "0x{:04X}, driver version 0x{:X}",
      properties.deviceName, VK_VERSION_MAJOR(unclamped_api_version),
      VK_VERSION_MINOR(unclamped_api_version),
      VK_VERSION_PATCH(properties.apiVersion),
      VK_VERSION_MAJOR(properties.apiVersion),
      VK_VERSION_MINOR(properties.apiVersion), properties.vendorID,
      properties.deviceID, properties.driverVersion);

  XELOGI("Enabled Vulkan device extensions:");
  for (uint32_t enabled_extension_index = 0;
       enabled_extension_index < device_create_info.enabledExtensionCount;
       ++enabled_extension_index) {
    XELOGI("* {}",
           device_create_info.ppEnabledExtensionNames[enabled_extension_index]);
  }

  XELOGI("Vulkan device properties and enabled features:");

  VkPhysicalDeviceFeatures enabled_features = {};
  device_create_info.pEnabledFeatures = &enabled_features;

#define XE_UI_VULKAN_LIMIT(name)                     \
  device->properties_.name = properties.limits.name; \
  XELOGI("* " #name ": {}", properties.limits.name);
#define XE_UI_VULKAN_ENUM_LIMIT(name, type)          \
  device->properties_.name = properties.limits.name; \
  XELOGI("* " #name ": {}", vk::to_string(vk::type(properties.limits.name)));
#define XE_UI_VULKAN_FEATURE(name)                    \
  enabled_features.name = supported_features.name;    \
  device->properties_.name = supported_features.name; \
  if (supported_features.name) {                      \
    XELOGI("* " #name);                               \
  }
#define XE_UI_VULKAN_PROPERTY_2(structure, name) \
  device->properties_.name = structure.name;     \
  XELOGI("* " #name ": {}", structure.name);
#define XE_UI_VULKAN_ENUM_PROPERTY_2(structure, name, type) \
  device->properties_.name = structure.name;                \
  XELOGI("* " #name ": {}", vk::to_string(vk::type(structure.name)));
#define XE_UI_VULKAN_FEATURE_2(structure, name)        \
  structure.enabled.name = structure.supported.name;   \
  device->properties_.name = structure.supported.name; \
  if (structure.supported.name) {                      \
    XELOGI("* " #name);                                \
  }
#define XE_UI_VULKAN_FEATURE_IMPLIED(name) \
  device->properties_.name = true;         \
  XELOGI("* " #name);

  if (ext_1_2_KHR_driver_properties) {
    XE_UI_VULKAN_ENUM_PROPERTY_2(properties_1_2_KHR_driver_properties, driverID,
                                 DriverId);
    XELOGI("* driverName: {}", properties_1_2_KHR_driver_properties.driverName);
    if (properties_1_2_KHR_driver_properties.driverInfo[0]) {
      XELOGI("* driverInfo: {}",
             properties_1_2_KHR_driver_properties.driverInfo);
    }
    XELOGI("* conformanceVersion: {}.{}.{}.{}",
           properties_1_2_KHR_driver_properties.conformanceVersion.major,
           properties_1_2_KHR_driver_properties.conformanceVersion.minor,
           properties_1_2_KHR_driver_properties.conformanceVersion.subminor,
           properties_1_2_KHR_driver_properties.conformanceVersion.patch);
  }

  if (properties.apiVersion >= VK_MAKE_API_VERSION(0, 1, 2, 0)) {
    // Prerequisite for the TBDR-correct native depth resolve (see the struct decl
    // above). Bit 0x1 = SAMPLE_ZERO, 0x2 = AVERAGE, 0x4 = MIN, 0x8 = MAX; the
    // 1<->2 MSAA depth conversions BD's EDRAM transfers perform want SAMPLE_ZERO.
    XELOGI(
        "* depthStencilResolve: depthModes=0x{:X} stencilModes=0x{:X} "
        "independentResolveNone={} independentResolve={}",
        properties_1_2_depth_stencil_resolve.supportedDepthResolveModes,
        properties_1_2_depth_stencil_resolve.supportedStencilResolveModes,
        properties_1_2_depth_stencil_resolve.independentResolveNone ? 1 : 0,
        properties_1_2_depth_stencil_resolve.independentResolve ? 1 : 0);
  }

  XE_UI_VULKAN_LIMIT(maxImageDimension2D)
  XE_UI_VULKAN_LIMIT(maxImageDimension3D)
  XE_UI_VULKAN_LIMIT(maxImageDimensionCube)
  XE_UI_VULKAN_LIMIT(maxImageArrayLayers)
  XE_UI_VULKAN_LIMIT(maxStorageBufferRange)
  XE_UI_VULKAN_LIMIT(maxDrawIndirectCount)
  XE_UI_VULKAN_LIMIT(maxSamplerAllocationCount)
  XE_UI_VULKAN_LIMIT(maxPerStageDescriptorSamplers)
  XE_UI_VULKAN_LIMIT(maxPerStageDescriptorStorageBuffers)
  XE_UI_VULKAN_LIMIT(maxPerStageDescriptorSampledImages)
  XE_UI_VULKAN_LIMIT(maxPerStageResources)
  XE_UI_VULKAN_LIMIT(maxVertexOutputComponents)
  XE_UI_VULKAN_LIMIT(maxTessellationEvaluationOutputComponents)
  XE_UI_VULKAN_LIMIT(maxGeometryInputComponents)
  XE_UI_VULKAN_LIMIT(maxGeometryOutputComponents)
  XE_UI_VULKAN_LIMIT(maxFragmentInputComponents)
  XE_UI_VULKAN_LIMIT(maxFragmentCombinedOutputResources)
  XE_UI_VULKAN_LIMIT(maxSamplerAnisotropy)
  XE_UI_VULKAN_LIMIT(maxViewportDimensions[0])
  XE_UI_VULKAN_LIMIT(maxViewportDimensions[1])
  XE_UI_VULKAN_LIMIT(minUniformBufferOffsetAlignment)
  XE_UI_VULKAN_LIMIT(minStorageBufferOffsetAlignment)
  XE_UI_VULKAN_LIMIT(maxFramebufferWidth)
  XE_UI_VULKAN_LIMIT(maxFramebufferHeight)
  XE_UI_VULKAN_ENUM_LIMIT(framebufferColorSampleCounts, SampleCountFlags)
  XE_UI_VULKAN_ENUM_LIMIT(framebufferDepthSampleCounts, SampleCountFlags)
  XE_UI_VULKAN_ENUM_LIMIT(framebufferStencilSampleCounts, SampleCountFlags)
  XE_UI_VULKAN_ENUM_LIMIT(framebufferNoAttachmentsSampleCounts,
                          SampleCountFlags)
  XE_UI_VULKAN_ENUM_LIMIT(sampledImageColorSampleCounts, SampleCountFlags)
  XE_UI_VULKAN_ENUM_LIMIT(sampledImageIntegerSampleCounts, SampleCountFlags)
  XE_UI_VULKAN_ENUM_LIMIT(sampledImageDepthSampleCounts, SampleCountFlags)
  XE_UI_VULKAN_ENUM_LIMIT(sampledImageStencilSampleCounts, SampleCountFlags)
  XE_UI_VULKAN_LIMIT(standardSampleLocations)
  XE_UI_VULKAN_LIMIT(optimalBufferCopyOffsetAlignment)
  XE_UI_VULKAN_LIMIT(optimalBufferCopyRowPitchAlignment)
  XE_UI_VULKAN_LIMIT(nonCoherentAtomSize)
  XE_UI_VULKAN_LIMIT(timestampPeriod)

  if (with_gpu_emulation) {
    XE_UI_VULKAN_FEATURE(robustBufferAccess)
    XE_UI_VULKAN_FEATURE(fullDrawIndexUint32)
    XE_UI_VULKAN_FEATURE(multiDrawIndirect)
    XE_UI_VULKAN_FEATURE(independentBlend)
    XE_UI_VULKAN_FEATURE(geometryShader)
    XE_UI_VULKAN_FEATURE(tessellationShader)
    XE_UI_VULKAN_FEATURE(sampleRateShading)
    XE_UI_VULKAN_FEATURE(depthClamp)
    XE_UI_VULKAN_FEATURE(fillModeNonSolid)
    XE_UI_VULKAN_FEATURE(samplerAnisotropy)
    XE_UI_VULKAN_FEATURE(occlusionQueryPrecise)
    XE_UI_VULKAN_FEATURE(vertexPipelineStoresAndAtomics)
    XE_UI_VULKAN_FEATURE(fragmentStoresAndAtomics)
    XE_UI_VULKAN_FEATURE(shaderClipDistance)
    XE_UI_VULKAN_FEATURE(shaderCullDistance)
    XE_UI_VULKAN_FEATURE(sparseBinding)
    XE_UI_VULKAN_FEATURE(sparseResidencyBuffer)
  }

  if (properties.apiVersion >= VK_MAKE_API_VERSION(0, 1, 2, 0)) {
    if (with_gpu_emulation) {
      XE_UI_VULKAN_FEATURE_2(features_1_2, samplerMirrorClampToEdge);
      // VK_EXT_descriptor_indexing bits for the BRICK 1 bindless render path
      // (gpu_native_render_path). Enabled when supported; inert for the legacy
      // per-draw descriptor path (see the Properties struct comment). The
      // command processor only builds the bindless set when the cvar is on AND
      // these were enabled, so a device without them cleanly stays legacy.
      XE_UI_VULKAN_FEATURE_2(features_1_2, descriptorIndexing);
      XE_UI_VULKAN_FEATURE_2(features_1_2,
                             shaderSampledImageArrayNonUniformIndexing);
      XE_UI_VULKAN_FEATURE_2(features_1_2, runtimeDescriptorArray);
      XE_UI_VULKAN_FEATURE_2(features_1_2, descriptorBindingPartiallyBound);
      XE_UI_VULKAN_FEATURE_2(features_1_2,
                             descriptorBindingSampledImageUpdateAfterBind);
      XE_UI_VULKAN_FEATURE_2(features_1_2,
                             descriptorBindingUpdateUnusedWhilePending);
    }
  } else {
    if (ext_1_2_KHR_sampler_mirror_clamp_to_edge) {
      XE_UI_VULKAN_FEATURE_IMPLIED(samplerMirrorClampToEdge)
    }
  }

  if (properties.apiVersion >= VK_MAKE_API_VERSION(0, 1, 3, 0)) {
    if (with_gpu_emulation) {
      XE_UI_VULKAN_FEATURE_2(features_1_3, shaderDemoteToHelperInvocation);
      // Prerequisite for dynamicRenderingLocalRead - that extension only has
      // meaning inside a vkCmdBeginRendering scope. Requested here so the
      // in-pass resolve work can gate on a real capability bit rather than
      // assuming the device has it.
      XE_UI_VULKAN_FEATURE_2(features_1_3, dynamicRendering);
    }
  } else {
    if (ext_1_3_EXT_shader_demote_to_helper_invocation) {
      if (with_gpu_emulation) {
        XE_UI_VULKAN_FEATURE_2(
            features_1_3_EXT_shader_demote_to_helper_invocation,
            shaderDemoteToHelperInvocation);
      }
    }
  }

  if (ext_KHR_portability_subset) {
    if (with_gpu_emulation) {
      XE_UI_VULKAN_FEATURE_2(features_KHR_portability_subset,
                             constantAlphaColorBlendFactors)
      XE_UI_VULKAN_FEATURE_2(features_KHR_portability_subset,
                             imageViewFormatReinterpretation)
      XE_UI_VULKAN_FEATURE_2(features_KHR_portability_subset,
                             imageViewFormatSwizzle)
      XE_UI_VULKAN_FEATURE_2(features_KHR_portability_subset, pointPolygons)
      XE_UI_VULKAN_FEATURE_2(features_KHR_portability_subset,
                             separateStencilMaskRef)
      XE_UI_VULKAN_FEATURE_2(features_KHR_portability_subset,
                             shaderSampleRateInterpolationFunctions)
      XE_UI_VULKAN_FEATURE_2(features_KHR_portability_subset, triangleFans)
    }
  } else {
    // Not a portability subset device.
    XE_UI_VULKAN_FEATURE_IMPLIED(constantAlphaColorBlendFactors)
    XE_UI_VULKAN_FEATURE_IMPLIED(imageViewFormatReinterpretation)
    XE_UI_VULKAN_FEATURE_IMPLIED(imageViewFormatSwizzle)
    XE_UI_VULKAN_FEATURE_IMPLIED(pointPolygons)
    XE_UI_VULKAN_FEATURE_IMPLIED(separateStencilMaskRef)
    XE_UI_VULKAN_FEATURE_IMPLIED(shaderSampleRateInterpolationFunctions)
    XE_UI_VULKAN_FEATURE_IMPLIED(triangleFans)
  }

  if (ext_1_2_KHR_shader_float_controls) {
    XE_UI_VULKAN_PROPERTY_2(properties_1_2_KHR_shader_float_controls,
                            shaderSignedZeroInfNanPreserveFloat32);
    XE_UI_VULKAN_PROPERTY_2(properties_1_2_KHR_shader_float_controls,
                            shaderDenormFlushToZeroFloat32);
    XE_UI_VULKAN_PROPERTY_2(properties_1_2_KHR_shader_float_controls,
                            shaderRoundingModeRTEFloat32);
  }

  if (ext_EXT_fragment_shader_interlock) {
    if (with_gpu_emulation) {
      XE_UI_VULKAN_FEATURE_2(features_EXT_fragment_shader_interlock,
                             fragmentShaderSampleInterlock)
      XE_UI_VULKAN_FEATURE_2(features_EXT_fragment_shader_interlock,
                             fragmentShaderPixelInterlock)
    }
  }

  if (device->extensions_.ext_1_4_KHR_dynamic_rendering_local_read &&
      with_gpu_emulation) {
    XE_UI_VULKAN_FEATURE_2(features_1_4_KHR_dynamic_rendering_local_read,
                           dynamicRenderingLocalRead)
  }

  if (device->extensions_.ext_EXT_rasterization_order_attachment_access) {
    if (with_gpu_emulation) {
      XE_UI_VULKAN_FEATURE_2(features_EXT_rasterization_order_attachment_access,
                             rasterizationOrderColorAttachmentAccess)
      XE_UI_VULKAN_FEATURE_2(features_EXT_rasterization_order_attachment_access,
                             rasterizationOrderDepthAttachmentAccess)
      XE_UI_VULKAN_FEATURE_2(features_EXT_rasterization_order_attachment_access,
                             rasterizationOrderStencilAttachmentAccess)
    }
  }

  if (device->extensions_.ext_EXT_multisampled_render_to_single_sampled) {
    if (with_gpu_emulation) {
      XE_UI_VULKAN_FEATURE_2(features_EXT_multisampled_render_to_single_sampled,
                             multisampledRenderToSingleSampled)
    }
  }

  if (device->extensions_.ext_EXT_custom_resolve) {
    if (with_gpu_emulation) {
      XE_UI_VULKAN_FEATURE_2(features_EXT_custom_resolve, customResolve)
    }
  }

  if (ext_EXT_non_seamless_cube_map) {
    if (with_gpu_emulation) {
      XE_UI_VULKAN_FEATURE_2(features_EXT_non_seamless_cube_map,
                             nonSeamlessCubeMap)
    }
  }

  if (device->extensions_.ext_KHR_fragment_shading_rate) {
    // Only the pipeline (per-draw, via vkCmdSetFragmentShadingRate) rate is used.
    // Enable manually (not via XE_UI_VULKAN_FEATURE_2, which also mirrors into
    // device->properties_ - a struct that has no VRS member).
    if (with_gpu_emulation) {
      features_KHR_fragment_shading_rate.enabled.pipelineFragmentShadingRate =
          features_KHR_fragment_shading_rate.supported.pipelineFragmentShadingRate;
    }
    // Keep the extension flag honest: only count it available if the pipeline
    // rate actually got enabled (the consumer guards on this flag before calling
    // vkCmdSetFragmentShadingRateKHR).
    device->extensions_.ext_KHR_fragment_shading_rate =
        with_gpu_emulation &&
        features_KHR_fragment_shading_rate.supported.pipelineFragmentShadingRate ==
            VK_TRUE;
    XELOGI("* VK_KHR_fragment_shading_rate (pipelineFragmentShadingRate: {})",
           device->extensions_.ext_KHR_fragment_shading_rate ? "yes" : "no");
  }

  if (device->extensions_.ext_EXT_fragment_density_map) {
    // Enable manually (like VRS): mirroring into device->properties_ via
    // XE_UI_VULKAN_FEATURE_2 would assert (no FDM member there). Enable the base
    // feature + nonSubsampledImages (the attach-to-existing-RTs path); leave
    // dynamic off (a static/per-frame density map; =0 on the Thor anyway).
    if (with_gpu_emulation) {
      features_EXT_fragment_density_map.enabled.fragmentDensityMap =
          features_EXT_fragment_density_map.supported.fragmentDensityMap;
      features_EXT_fragment_density_map.enabled
          .fragmentDensityMapNonSubsampledImages =
          features_EXT_fragment_density_map.supported
              .fragmentDensityMapNonSubsampledImages;
    }
    // Keep the flag honest: available only when BOTH the base feature AND
    // nonSubsampledImages are enabled - xenia's render targets are NOT created
    // with VK_IMAGE_CREATE_SUBSAMPLED_BIT, so attaching an FDM to them requires
    // nonSubsampledImages (VUID-VkFramebufferCreateInfo-renderPass-02553). The
    // consumer guards on this flag before attaching a density map.
    device->extensions_.ext_EXT_fragment_density_map =
        with_gpu_emulation &&
        features_EXT_fragment_density_map.supported.fragmentDensityMap ==
            VK_TRUE &&
        features_EXT_fragment_density_map.supported
                .fragmentDensityMapNonSubsampledImages == VK_TRUE;
    // Store the HW density-map texel size so the consumer sizes the density image
    // as ceil(framebuffer / maxTexel) (the per-framebuffer VUID lower bound),
    // instead of a hardcoded constant. Zero if not populated -> consumer skips FDM.
    device->extensions_.fragment_density_map_max_texel_size =
        properties_EXT_fragment_density_map.maxFragmentDensityTexelSize;
    XELOGI(
        "* VK_EXT_fragment_density_map (enabled: {}, nonSubsampledImages: {}, "
        "maxTexelSize: {}x{})",
        device->extensions_.ext_EXT_fragment_density_map ? "yes" : "no",
        features_EXT_fragment_density_map.supported
                .fragmentDensityMapNonSubsampledImages == VK_TRUE
            ? "yes"
            : "no",
        properties_EXT_fragment_density_map.maxFragmentDensityTexelSize.width,
        properties_EXT_fragment_density_map.maxFragmentDensityTexelSize.height);
  }

  if (device->extensions_.ext_KHR_push_descriptor) {
    device->extensions_.max_push_descriptors =
        properties_KHR_push_descriptor.maxPushDescriptors;
    XELOGI("* VK_KHR_push_descriptor (maxPushDescriptors: {})",
           properties_KHR_push_descriptor.maxPushDescriptors);
  }

  if (device->extensions_.ext_KHR_pipeline_executable_properties) {
    // Enable manually (like VRS/FDM): mirroring into device->properties_ via
    // XE_UI_VULKAN_FEATURE_2 would assert (no member there). The
    // pipelineExecutableInfo feature must be enabled to call
    // vkGetPipelineExecutable{Properties,Statistics}KHR (the diagnostic
    // consumer guards on this flag).
    features_KHR_pipeline_executable_properties.enabled.pipelineExecutableInfo =
        features_KHR_pipeline_executable_properties.supported
            .pipelineExecutableInfo;
    // Keep the flag honest: available only when the feature actually enabled.
    device->extensions_.ext_KHR_pipeline_executable_properties =
        features_KHR_pipeline_executable_properties.supported
            .pipelineExecutableInfo == VK_TRUE;
    XELOGI("* VK_KHR_pipeline_executable_properties (pipelineExecutableInfo: {})",
           device->extensions_.ext_KHR_pipeline_executable_properties ? "yes"
                                                                      : "no");
  }

  if (device->extensions_.ext_EXT_extended_dynamic_state3) {
    // gpu_dynamic_blend_state (EDS3): promote guest color blend enable/equation/
    // write mask to dynamic state so blend variance stops minting pipelines.
    // Enable manually (like VRS/FDM) - mirroring into device->properties_ via
    // XE_UI_VULKAN_FEATURE_2 would assert (no member there). Enable only the 3
    // blend sub-features the consumer uses; each gates one of the 3 dynamic
    // states (VK_DYNAMIC_STATE_COLOR_BLEND_ENABLE / _EQUATION / _WRITE_MASK_EXT).
    // The .supported bits were populated by the vkGetPhysicalDeviceFeatures2 above
    // (the feature was Linked when the extension was requested).
    if (with_gpu_emulation) {
      features_EXT_extended_dynamic_state3.enabled
          .extendedDynamicState3ColorBlendEnable =
          features_EXT_extended_dynamic_state3.supported
              .extendedDynamicState3ColorBlendEnable;
      features_EXT_extended_dynamic_state3.enabled
          .extendedDynamicState3ColorBlendEquation =
          features_EXT_extended_dynamic_state3.supported
              .extendedDynamicState3ColorBlendEquation;
      features_EXT_extended_dynamic_state3.enabled
          .extendedDynamicState3ColorWriteMask =
          features_EXT_extended_dynamic_state3.supported
              .extendedDynamicState3ColorWriteMask;
      // EDS3 completion: polygon mode + depth clamp enable (each gates its own
      // dynamic state; independent of the 3-blend-sub-feature bundle).
      features_EXT_extended_dynamic_state3.enabled
          .extendedDynamicState3PolygonMode =
          features_EXT_extended_dynamic_state3.supported
              .extendedDynamicState3PolygonMode;
      features_EXT_extended_dynamic_state3.enabled
          .extendedDynamicState3DepthClampEnable =
          features_EXT_extended_dynamic_state3.supported
              .extendedDynamicState3DepthClampEnable;
    }
    device->extensions_.eds3_dynamic_polygon_mode =
        features_EXT_extended_dynamic_state3.supported
            .extendedDynamicState3PolygonMode == VK_TRUE;
    device->extensions_.eds3_dynamic_depth_clamp =
        features_EXT_extended_dynamic_state3.supported
            .extendedDynamicState3DepthClampEnable == VK_TRUE;
    device->extensions_.eds3_dynamic_blend_enable =
        features_EXT_extended_dynamic_state3.supported
            .extendedDynamicState3ColorBlendEnable == VK_TRUE;
    device->extensions_.eds3_dynamic_blend_equation =
        features_EXT_extended_dynamic_state3.supported
            .extendedDynamicState3ColorBlendEquation == VK_TRUE;
    device->extensions_.eds3_dynamic_write_mask =
        features_EXT_extended_dynamic_state3.supported
            .extendedDynamicState3ColorWriteMask == VK_TRUE;
    // Keep the extension flag honest: available only with GPU emulation AND all 3
    // needed dynamic-blend sub-features (the consumer promotes enable+equation+
    // write-mask together, so a partial set is unusable). The consumers guard on
    // this flag (and the 3 sub-flags) before emitting the dynamic blend state.
    device->extensions_.ext_EXT_extended_dynamic_state3 =
        with_gpu_emulation && device->extensions_.eds3_dynamic_blend_enable &&
        device->extensions_.eds3_dynamic_blend_equation &&
        device->extensions_.eds3_dynamic_write_mask;
    XELOGI(
        "* VK_EXT_extended_dynamic_state3 (dynamic blend enable/equation/"
        "writemask: {})",
        device->extensions_.ext_EXT_extended_dynamic_state3 ? "yes" : "no");
  }

#undef XE_UI_VULKAN_LIMIT
#undef XE_UI_VULKAN_ENUM_LIMIT
#undef XE_UI_VULKAN_FEATURE
#undef XE_UI_VULKAN_PROPERTY_2
#undef XE_UI_VULKAN_ENUM_PROPERTY_2
#undef XE_UI_VULKAN_FEATURE_2

  // Create the device.

  const VkResult device_create_result = ifn.vkCreateDevice(
      physical_device, &device_create_info, nullptr, &device->device_);
  if (device_create_result != VK_SUCCESS) {
    XELOGE(
        "Failed to create a Vulkan logical device from the physical device "
        "'{}': {}",
        properties.deviceName, vk::to_string(vk::Result(device_create_result)));
    return nullptr;
  }

  // Load device functions.

  bool functions_loaded = true;

  Functions& dfn = device->functions_;

#define XE_UI_VULKAN_FUNCTION(name)                                   \
  functions_loaded &= (dfn.name = PFN_##name(ifn.vkGetDeviceProcAddr( \
                           device->device_, #name))) != nullptr;

  // Vulkan 1.0.
#include "xenia/ui/vulkan/functions/device_1_0.inc"

  // Extensions promoted to a Vulkan version supported by the device.
#define XE_UI_VULKAN_FUNCTION_PROMOTED(extension_name, core_name) \
  functions_loaded &=                                             \
      (dfn.core_name = PFN_##core_name(                           \
           ifn.vkGetDeviceProcAddr(device->device_, #core_name))) != nullptr;
  if (properties.apiVersion >= VK_MAKE_API_VERSION(0, 1, 1, 0)) {
#include "xenia/ui/vulkan/functions/device_1_1_khr_bind_memory2.inc"
#include "xenia/ui/vulkan/functions/device_1_1_khr_get_memory_requirements2.inc"
  }
  if (properties.apiVersion >= VK_MAKE_API_VERSION(0, 1, 2, 0)) {
#include "xenia/ui/vulkan/functions/device_1_2_khr_draw_indirect_count.inc"
#include "xenia/ui/vulkan/functions/device_1_2_khr_create_renderpass2.inc"
  }
  if (properties.apiVersion >= VK_MAKE_API_VERSION(0, 1, 3, 0)) {
#include "xenia/ui/vulkan/functions/device_1_3_khr_maintenance4.inc"
#include "xenia/ui/vulkan/functions/device_1_3_ext_extended_dynamic_state.inc"
  }
#undef XE_UI_VULKAN_FUNCTION_PROMOTED

  // Non-promoted extensions, and extensions promoted to a Vulkan version not
  // supported by the device.
#define XE_UI_VULKAN_FUNCTION_PROMOTED(extension_name, core_name) \
  functions_loaded &=                                             \
      (dfn.core_name = PFN_##core_name(ifn.vkGetDeviceProcAddr(   \
           device->device_, #extension_name))) != nullptr;
  if (properties.apiVersion < VK_MAKE_API_VERSION(0, 1, 1, 0)) {
    if (device->extensions_.ext_1_1_KHR_get_memory_requirements2) {
#include "xenia/ui/vulkan/functions/device_1_1_khr_get_memory_requirements2.inc"
    }
    if (device->extensions_.ext_1_1_KHR_bind_memory2) {
#include "xenia/ui/vulkan/functions/device_1_1_khr_bind_memory2.inc"
    }
  }
  if (properties.apiVersion < VK_MAKE_API_VERSION(0, 1, 3, 0)) {
    if (device->extensions_.ext_1_3_KHR_maintenance4) {
#include "xenia/ui/vulkan/functions/device_1_3_khr_maintenance4.inc"
    }
  }
  if (device->extensions_.ext_KHR_swapchain) {
#include "xenia/ui/vulkan/functions/device_khr_swapchain.inc"
  }
  if (device->extensions_.ext_KHR_push_descriptor) {
#include "xenia/ui/vulkan/functions/device_khr_push_descriptor.inc"
  }
  if (device->extensions_.ext_KHR_fragment_shading_rate) {
#include "xenia/ui/vulkan/functions/device_khr_fragment_shading_rate.inc"
  }
  if (device->extensions_.ext_EXT_extended_dynamic_state3) {
#include "xenia/ui/vulkan/functions/device_ext_extended_dynamic_state3.inc"
  }
  if (device->extensions_.ext_KHR_pipeline_executable_properties) {
#include "xenia/ui/vulkan/functions/device_khr_pipeline_executable_properties.inc"
  }
  // Core in 1.3, so gate on the API version rather than an extension. The Thor
  // reports 1.3+ (Turnip advertises 1.4), so this loads in practice; the guard
  // is here so a hypothetical 1.2 device fails to find the symbol rather than
  // taking a null function pointer into a render pass.
  if (properties.apiVersion >= VK_MAKE_API_VERSION(0, 1, 3, 0)) {
#include "xenia/ui/vulkan/functions/device_1_3_dynamic_rendering.inc"
  }
  if (device->extensions_.ext_1_4_KHR_dynamic_rendering_local_read) {
#include "xenia/ui/vulkan/functions/device_khr_dynamic_rendering_local_read.inc"
  }
#undef XE_UI_VULKAN_FUNCTION_PROMOTED

#undef XE_UI_VULKAN_FUNCTION

  if (!functions_loaded) {
    XELOGE("Failed to get all Vulkan device function pointers for '{}'",
           properties.deviceName);
    return nullptr;
  }

  // Get the queues.

  for (size_t queue_family_index = 0;
       queue_family_index < device->queue_families_.size();
       ++queue_family_index) {
    QueueFamily& queue_family = device->queue_families_[queue_family_index];
    for (size_t queue_index = 0; queue_index < queue_family.queues.size();
         ++queue_index) {
      VkQueue queue;
      dfn.vkGetDeviceQueue(device->device_, uint32_t(queue_family_index),
                           uint32_t(queue_index), &queue);
      queue_family.queues[queue_index] = std::make_unique<Queue>(queue);
    }
  }

  // Get the memory types.

  VkPhysicalDeviceMemoryProperties memory_properties;
  ifn.vkGetPhysicalDeviceMemoryProperties(physical_device, &memory_properties);
  for (uint32_t memory_type_index = 0;
       memory_type_index < memory_properties.memoryTypeCount;
       ++memory_type_index) {
    const uint32_t memory_type_bit = uint32_t(1) << memory_type_index;
    const VkMemoryPropertyFlags memory_type_flags =
        memory_properties.memoryTypes[memory_type_index].propertyFlags;
    if (memory_type_flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) {
      device->memory_types_.device_local |= memory_type_bit;
    }
    if (memory_type_flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
      device->memory_types_.host_visible |= memory_type_bit;
    }
    if (memory_type_flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) {
      device->memory_types_.host_coherent |= memory_type_bit;
    }
    if (memory_type_flags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT) {
      device->memory_types_.host_cached |= memory_type_bit;
    }
  }

  return device;
}

VulkanDevice::~VulkanDevice() {
  if (device_) {
    vulkan_instance_->functions().vkDestroyDevice(device_, nullptr);
  }
}

VulkanDevice::VulkanDevice(const VulkanInstance* const vulkan_instance,
                           const VkPhysicalDevice physical_device)
    : vulkan_instance_(vulkan_instance), physical_device_(physical_device) {
  assert_not_null(vulkan_instance);
  assert_not_null(physical_device);
}

}  // namespace vulkan
}  // namespace ui
}  // namespace xe
