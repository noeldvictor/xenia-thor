// Vulkan 1.3 CORE dynamic rendering entry points.
//
// NOTE the contrast with device_khr_dynamic_rendering_local_read.inc: base
// dynamic rendering is core in 1.3, so our 1.3 headers spell these UNSUFFIXED.
// Local read is 1.4 and must carry the KHR suffix. Do not "make them
// consistent" - they are different spellings for a reason.
XE_UI_VULKAN_FUNCTION(vkCmdBeginRendering)
XE_UI_VULKAN_FUNCTION(vkCmdEndRendering)
