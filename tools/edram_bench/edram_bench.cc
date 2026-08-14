// EDRAM render-target sizing harness.
//
// WHY THIS EXISTS (user directive 2026-08-13): test acceleration theories in a
// bespoke binary, not in the emulator. An APK cycle is 10-18 min plus a 143s
// title load plus a 140s route; this loop is ~45s. The theory needs the DEVICE
// and the DRIVER, not the emulator.
//
// THE THEORY, from the 2026-08-10 measurement: 82% of a BD gameplay frame is
// inside render passes, and TWO passes are 65% of that. The pass identities
// name the cause - the RT cache allocates every host render target at the
// EDRAM-tile-rounded height, so a 1280x720 game renders into 1280x2048
// (1280*2048*4 = 10,485,760 = exactly the 360's 10MB EDRAM). On a TBDR the
// driver bins, loads and stores those off-screen rows every pass.
//
// Clamping renderArea to the guest scissor HALVED in-pass time - proving the
// mechanism - but pushed the cost into the gaps for a net +18% SLOWER. So the
// real question is whether the ALLOCATION change pays. This prices it without
// rebuilding the RT cache first.
//
// The arms differ ONLY in attachment height. Both draw the same geometry into
// the same viewport (the top `view_height` rows), which is exactly the shape
// the emulator produces.
//
// HEADLESS ON PURPOSE: no swapchain, so this measures GPU pass cost, not the
// present path. It therefore CANNOT see compositor interaction or a
// FlexRender binning->direct switch driven by presentation. A result here is a
// MECHANISM check, not a game speedup.

#include <vulkan/vulkan.h>
#include <dlfcn.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "shaders/fill_vert.h"
#include "shaders/fill_frag.h"
#include "shaders/flags_dyn.h"
#include "shaders/flags_spec.h"

// ---------------------------------------------------------------------------
// Driver shim.
//
// We SHIP TURNIP, but the system Vulkan loader on Android hands out the
// Qualcomm blob and cannot be told otherwise without adrenotools' dlopen hook.
// A standalone binary can sidestep all of that: dlopen the ICD directly and
// resolve entry points from it. --driver <path> selects one; without it we use
// the system libvulkan, and the harness always PRINTS which device it got so a
// result can never be silently attributed to the wrong driver.
//
// Every vk* name below is #defined onto a loaded pointer, so the call sites in
// main() are unchanged.
// ---------------------------------------------------------------------------
#define XE_VK_GLOBAL_FUNCS(X) X(CreateInstance)

#define XE_VK_INSTANCE_FUNCS(X)      \
  X(DestroyInstance)                 \
  X(EnumeratePhysicalDevices)        \
  X(GetPhysicalDeviceProperties)     \
  X(GetPhysicalDeviceQueueFamilyProperties) \
  X(GetPhysicalDeviceMemoryProperties) \
  X(CreateDevice)                    \
  X(GetDeviceProcAddr)

#define XE_VK_DEVICE_FUNCS(X)  \
  X(DestroyDevice)             \
  X(GetDeviceQueue)            \
  X(CreateImage) X(DestroyImage)                 \
  X(GetImageMemoryRequirements)                  \
  X(AllocateMemory) X(FreeMemory) X(BindImageMemory) \
  X(CreateImageView) X(DestroyImageView)         \
  X(CreateRenderPass) X(DestroyRenderPass)       \
  X(CreateFramebuffer) X(DestroyFramebuffer)     \
  X(CreateShaderModule) X(DestroyShaderModule)   \
  X(CreatePipelineLayout) X(DestroyPipelineLayout) \
  X(CreateGraphicsPipelines) X(DestroyPipeline)  \
  X(CreateCommandPool) X(DestroyCommandPool)     \
  X(AllocateCommandBuffers)                      \
  X(CreateQueryPool) X(DestroyQueryPool)         \
  X(CreateFence) X(DestroyFence)                 \
  X(ResetCommandBuffer) X(BeginCommandBuffer) X(EndCommandBuffer) \
  X(CmdResetQueryPool) X(CmdPipelineBarrier) X(CmdWriteTimestamp) \
  X(CmdBeginRenderPass) X(CmdEndRenderPass)      \
  X(CmdBindPipeline) X(CmdPushConstants) X(CmdDraw) X(CmdClearAttachments) \
  X(ResetFences) X(QueueSubmit) X(WaitForFences) \
  X(GetQueryPoolResults)

namespace vkapi {
#define XE_DECL(name) PFN_vk##name name = nullptr;
XE_VK_GLOBAL_FUNCS(XE_DECL)
XE_VK_INSTANCE_FUNCS(XE_DECL)
XE_VK_DEVICE_FUNCS(XE_DECL)
#undef XE_DECL
PFN_vkGetInstanceProcAddr GetInstanceProcAddr = nullptr;
const char* source = "system libvulkan.so";
}  // namespace vkapi

#define vkCreateInstance vkapi::CreateInstance
#define XE_MAP(name)
#define vkDestroyInstance vkapi::DestroyInstance
#define vkEnumeratePhysicalDevices vkapi::EnumeratePhysicalDevices
#define vkGetPhysicalDeviceProperties vkapi::GetPhysicalDeviceProperties
#define vkGetPhysicalDeviceQueueFamilyProperties \
  vkapi::GetPhysicalDeviceQueueFamilyProperties
#define vkGetPhysicalDeviceMemoryProperties vkapi::GetPhysicalDeviceMemoryProperties
#define vkCreateDevice vkapi::CreateDevice
#define vkDestroyDevice vkapi::DestroyDevice
#define vkGetDeviceQueue vkapi::GetDeviceQueue
#define vkCreateImage vkapi::CreateImage
#define vkDestroyImage vkapi::DestroyImage
#define vkGetImageMemoryRequirements vkapi::GetImageMemoryRequirements
#define vkAllocateMemory vkapi::AllocateMemory
#define vkFreeMemory vkapi::FreeMemory
#define vkBindImageMemory vkapi::BindImageMemory
#define vkCreateImageView vkapi::CreateImageView
#define vkDestroyImageView vkapi::DestroyImageView
#define vkCreateRenderPass vkapi::CreateRenderPass
#define vkDestroyRenderPass vkapi::DestroyRenderPass
#define vkCreateFramebuffer vkapi::CreateFramebuffer
#define vkDestroyFramebuffer vkapi::DestroyFramebuffer
#define vkCreateShaderModule vkapi::CreateShaderModule
#define vkDestroyShaderModule vkapi::DestroyShaderModule
#define vkCreatePipelineLayout vkapi::CreatePipelineLayout
#define vkDestroyPipelineLayout vkapi::DestroyPipelineLayout
#define vkCreateGraphicsPipelines vkapi::CreateGraphicsPipelines
#define vkDestroyPipeline vkapi::DestroyPipeline
#define vkCreateCommandPool vkapi::CreateCommandPool
#define vkDestroyCommandPool vkapi::DestroyCommandPool
#define vkAllocateCommandBuffers vkapi::AllocateCommandBuffers
#define vkCreateQueryPool vkapi::CreateQueryPool
#define vkDestroyQueryPool vkapi::DestroyQueryPool
#define vkCreateFence vkapi::CreateFence
#define vkDestroyFence vkapi::DestroyFence
#define vkResetCommandBuffer vkapi::ResetCommandBuffer
#define vkBeginCommandBuffer vkapi::BeginCommandBuffer
#define vkEndCommandBuffer vkapi::EndCommandBuffer
#define vkCmdResetQueryPool vkapi::CmdResetQueryPool
#define vkCmdPipelineBarrier vkapi::CmdPipelineBarrier
#define vkCmdWriteTimestamp vkapi::CmdWriteTimestamp
#define vkCmdBeginRenderPass vkapi::CmdBeginRenderPass
#define vkCmdEndRenderPass vkapi::CmdEndRenderPass
#define vkCmdBindPipeline vkapi::CmdBindPipeline
#define vkCmdPushConstants vkapi::CmdPushConstants
#define vkCmdDraw vkapi::CmdDraw
#define vkCmdClearAttachments vkapi::CmdClearAttachments
#define vkResetFences vkapi::ResetFences
#define vkQueueSubmit vkapi::QueueSubmit
#define vkWaitForFences vkapi::WaitForFences
#define vkGetQueryPoolResults vkapi::GetQueryPoolResults

#define VK_CHECK(expr)                                                     \
  do {                                                                     \
    VkResult _r = (expr);                                                  \
    if (_r != VK_SUCCESS) {                                                \
      std::fprintf(stderr, "%s:%d: %s failed (VkResult %d)\n", __FILE__,   \
                   __LINE__, #expr, int(_r));                              \
      std::exit(1);                                                        \
    }                                                                      \
  } while (0)

namespace {

struct Config {
  uint32_t width = 1280;
  uint32_t height = 2048;       // attachment height - THE variable under test
  uint32_t view_width = 1280;   // what the guest actually draws into
  uint32_t view_height = 720;
  uint32_t passes = 16;
  uint32_t draws = 8;
  uint32_t iters = 20;
  uint32_t warmup = 5;
  std::string load_op = "clear";      // clear | load | dontcare
  std::string store_op = "store";     // store | dontcare
  const char* label = "arm";
  std::string driver;  // empty = system loader
  bool depth = false;          // add a depth attachment alongside colour
  std::string depth_load_op = "load";
  // In-pass vkCmdClearAttachments: none | full | scissor.
  std::string inpass_clear = "none";
  // Uber-shader probe: none | dyn | spec. dyn branches on push-constant
  // flags (what our translator emits); spec makes them specialization
  // constants so the compiler folds and dead-strips them.
  std::string flag_shader = "none";
  uint32_t flag_value = 0x5555u;
};

VkAttachmentLoadOp ParseLoadOp(const std::string& s) {
  if (s == "load") return VK_ATTACHMENT_LOAD_OP_LOAD;
  if (s == "dontcare") return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  return VK_ATTACHMENT_LOAD_OP_CLEAR;
}

VkAttachmentStoreOp ParseStoreOp(const std::string& s) {
  if (s == "dontcare") return VK_ATTACHMENT_STORE_OP_DONT_CARE;
  return VK_ATTACHMENT_STORE_OP_STORE;
}

uint32_t FindMemoryType(VkPhysicalDevice pd, uint32_t bits,
                        VkMemoryPropertyFlags props) {
  VkPhysicalDeviceMemoryProperties mp;
  vkGetPhysicalDeviceMemoryProperties(pd, &mp);
  for (uint32_t i = 0; i < mp.memoryTypeCount; ++i) {
    if ((bits & (1u << i)) &&
        (mp.memoryTypes[i].propertyFlags & props) == props) {
      return i;
    }
  }
  std::fprintf(stderr, "no suitable memory type\n");
  std::exit(1);
}

// Turnip on Android is built as a Vulkan HAL MODULE, not a plain ICD: it
// exports `HMI` (hw_module_t) and no vkGetInstanceProcAddr at all, which is
// why dlsym for the usual names finds nothing. The real entry is
// HMI -> methods->open("vk0") -> hwvulkan_device_t::GetInstanceProcAddr.
//
// These structs are AOSP platform headers (hardware/hardware.h,
// hardware/hwvulkan.h) and are NOT shipped in the NDK, so the minimum is
// redefined here. The layout has been ABI-frozen for a decade - every Android
// HAL depends on it.
struct xe_hw_module_t;
struct xe_hw_device_t;
struct xe_hw_module_methods_t {
  int (*open)(const struct xe_hw_module_t*, const char*, struct xe_hw_device_t**);
};
struct xe_hw_module_t {
  uint32_t tag;
  uint16_t module_api_version;
  uint16_t hal_api_version;
  const char* id;
  const char* name;
  const char* author;
  struct xe_hw_module_methods_t* methods;
  void* dso;
  uint32_t reserved[32 - 7];
};
struct xe_hw_device_t {
  uint32_t tag;
  uint32_t version;
  struct xe_hw_module_t* module;
  uint32_t reserved[12];
  int (*close)(struct xe_hw_device_t*);
};
struct xe_hwvulkan_device_t {
  struct xe_hw_device_t common;
  PFN_vkEnumerateInstanceExtensionProperties EnumerateInstanceExtensionProperties;
  PFN_vkCreateInstance CreateInstance;
  PFN_vkGetInstanceProcAddr GetInstanceProcAddr;
};

PFN_vkGetInstanceProcAddr TryLoadAsHalModule(void* h) {
  auto* module = reinterpret_cast<xe_hw_module_t*>(dlsym(h, "HMI"));
  std::fprintf(stderr, "[hal] HMI=%p\n", (void*)module);
  if (!module || !module->methods || !module->methods->open) return nullptr;
  std::fprintf(stderr, "[hal] tag=%#x id=%s methods=%p\n", module->tag,
               module->id ? module->id : "(null)", (void*)module->methods);
  xe_hw_device_t* dev = nullptr;
  int rc = module->methods->open(module, "vk0", &dev);
  std::fprintf(stderr, "[hal] open rc=%d dev=%p\n", rc, (void*)dev);
  if (rc != 0 || !dev) {
    return nullptr;
  }
  // hwvulkan_device_t is hw_device_t followed by
  // {EnumerateInstanceExtensionProperties, CreateInstance,
  // GetInstanceProcAddr}, and hw_device_t itself ends in close(). So the tail
  // is four consecutive function pointers and GetInstanceProcAddr is the LAST.
  //
  // The offset is NOT hardcoded: this build's hw_device_t is larger than the
  // AOSP header describes (close lands at +112, not +64), so a copied offset
  // would silently read a different field. Instead, find the last run of
  // pointers that dladdr maps back into the driver itself, then PROVE the
  // candidate by calling it - a real GetInstanceProcAddr must resolve
  // "vkCreateInstance" against a null instance.
  auto* words = reinterpret_cast<void**>(dev);
  void* last_in_driver = nullptr;
  Dl_info self{};
  const bool have_self = dladdr(reinterpret_cast<void*>(module), &self) != 0;
  for (int i = 0; i < 32; ++i) {
    if (!words[i]) continue;
    Dl_info info{};
    if (!dladdr(words[i], &info) || !info.dli_fname) continue;
    if (have_self && self.dli_fname &&
        std::strcmp(info.dli_fname, self.dli_fname) != 0) {
      continue;
    }
    last_in_driver = words[i];
  }
  if (!last_in_driver) {
    return nullptr;
  }
  auto gipa = reinterpret_cast<PFN_vkGetInstanceProcAddr>(last_in_driver);
  if (!gipa(nullptr, "vkCreateInstance")) {
    std::fprintf(stderr,
                 "[hal] candidate at %p did not resolve vkCreateInstance\n",
                 last_in_driver);
    return nullptr;
  }
  return gipa;
}

void LoadDriver(const std::string& driver_path) {
  const char* path =
      driver_path.empty() ? "libvulkan.so" : driver_path.c_str();
  void* h = dlopen(path, RTLD_NOW | RTLD_LOCAL);
  if (!h) {
    std::fprintf(stderr, "dlopen(%s) failed: %s\n", path, dlerror());
    std::exit(1);
  }
  // An ICD loaded directly exposes the loader-facing entry point; the system
  // libvulkan exposes the normal one. Accept either.
  vkapi::GetInstanceProcAddr =
      (PFN_vkGetInstanceProcAddr)dlsym(h, "vkGetInstanceProcAddr");
  if (!vkapi::GetInstanceProcAddr) {
    vkapi::GetInstanceProcAddr =
        (PFN_vkGetInstanceProcAddr)dlsym(h, "vk_icdGetInstanceProcAddr");
  }
  if (!vkapi::GetInstanceProcAddr) {
    // Turnip takes this path - it is a HAL module, not an ICD.
    vkapi::GetInstanceProcAddr = TryLoadAsHalModule(h);
  }
  if (!vkapi::GetInstanceProcAddr) {
    std::fprintf(stderr,
                 "no vkGetInstanceProcAddr, vk_icdGetInstanceProcAddr or "
                 "usable HMI hw_module in %s\n",
                 path);
    std::exit(1);
  }
  if (!driver_path.empty()) vkapi::source = driver_path.c_str();
#define XE_LOAD_GLOBAL(name)                                              \
  vkapi::name = (PFN_vk##name)vkapi::GetInstanceProcAddr(nullptr, "vk" #name); \
  if (!vkapi::name) {                                                     \
    std::fprintf(stderr, "missing vk" #name "\n");                        \
    std::exit(1);                                                         \
  }
  XE_VK_GLOBAL_FUNCS(XE_LOAD_GLOBAL)
#undef XE_LOAD_GLOBAL
}

void LoadInstanceFuncs(VkInstance inst) {
#define XE_LOAD(name)                                                     \
  vkapi::name = (PFN_vk##name)vkapi::GetInstanceProcAddr(inst, "vk" #name); \
  if (!vkapi::name) {                                                     \
    std::fprintf(stderr, "missing vk" #name "\n");                        \
    std::exit(1);                                                         \
  }
  XE_VK_INSTANCE_FUNCS(XE_LOAD)
#undef XE_LOAD
}

void LoadDeviceFuncs(VkDevice dev) {
#define XE_LOAD(name)                                                     \
  vkapi::name = (PFN_vk##name)vkapi::GetDeviceProcAddr(dev, "vk" #name);  \
  if (!vkapi::name) {                                                     \
    std::fprintf(stderr, "missing vk" #name "\n");                        \
    std::exit(1);                                                         \
  }
  XE_VK_DEVICE_FUNCS(XE_LOAD)
#undef XE_LOAD
}

VkShaderModule MakeShader(VkDevice dev, const uint32_t* code, size_t bytes) {
  VkShaderModuleCreateInfo ci{};
  ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  ci.codeSize = bytes;
  ci.pCode = code;
  VkShaderModule m;
  VK_CHECK(vkCreateShaderModule(dev, &ci, nullptr, &m));
  return m;
}

}  // namespace

int main(int argc, char** argv) {
  Config cfg;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto next = [&]() -> std::string {
      if (i + 1 >= argc) {
        std::fprintf(stderr, "missing value for %s\n", a.c_str());
        std::exit(1);
      }
      return argv[++i];
    };
    if (a == "--width") cfg.width = std::stoul(next());
    else if (a == "--height") cfg.height = std::stoul(next());
    else if (a == "--view-width") cfg.view_width = std::stoul(next());
    else if (a == "--view-height") cfg.view_height = std::stoul(next());
    else if (a == "--passes") cfg.passes = std::stoul(next());
    else if (a == "--draws") cfg.draws = std::stoul(next());
    else if (a == "--iters") cfg.iters = std::stoul(next());
    else if (a == "--loadop") cfg.load_op = next();
    else if (a == "--storeop") cfg.store_op = next();
    else if (a == "--driver") cfg.driver = next();
    else if (a == "--depth") cfg.depth = true;
    else if (a == "--depth-loadop") cfg.depth_load_op = next();
    else if (a == "--inpass-clear") cfg.inpass_clear = next();
    else if (a == "--flag-shader") cfg.flag_shader = next();
    else if (a == "--flag-value") cfg.flag_value = std::stoul(next(), nullptr, 0);
    else if (a == "--label") cfg.label = argv[i + 1], ++i;
    else {
      std::fprintf(stderr, "unknown arg: %s\n", a.c_str());
      return 1;
    }
  }
  cfg.view_width = std::min(cfg.view_width, cfg.width);
  cfg.view_height = std::min(cfg.view_height, cfg.height);

  LoadDriver(cfg.driver);

  VkApplicationInfo app{};
  app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  app.pApplicationName = "edram_bench";
  app.apiVersion = VK_API_VERSION_1_1;
  VkInstanceCreateInfo ici{};
  ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  ici.pApplicationInfo = &app;
  VkInstance inst;
  VK_CHECK(vkCreateInstance(&ici, nullptr, &inst));
  LoadInstanceFuncs(inst);

  uint32_t pd_count = 0;
  VK_CHECK(vkEnumeratePhysicalDevices(inst, &pd_count, nullptr));
  std::vector<VkPhysicalDevice> pds(pd_count);
  VK_CHECK(vkEnumeratePhysicalDevices(inst, &pd_count, pds.data()));
  if (!pd_count) {
    std::fprintf(stderr, "no Vulkan device\n");
    return 1;
  }
  VkPhysicalDevice pd = pds[0];
  VkPhysicalDeviceProperties props;
  vkGetPhysicalDeviceProperties(pd, &props);

  uint32_t qf_count = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(pd, &qf_count, nullptr);
  std::vector<VkQueueFamilyProperties> qfs(qf_count);
  vkGetPhysicalDeviceQueueFamilyProperties(pd, &qf_count, qfs.data());
  uint32_t qfi = UINT32_MAX;
  for (uint32_t i = 0; i < qf_count; ++i) {
    if (qfs[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) { qfi = i; break; }
  }
  if (qfi == UINT32_MAX) {
    std::fprintf(stderr, "no graphics queue\n");
    return 1;
  }
  // Timestamps are the whole measurement - refuse to report noise if the queue
  // cannot produce them.
  if (qfs[qfi].timestampValidBits == 0 || props.limits.timestampPeriod == 0.0f) {
    std::fprintf(stderr, "queue has no usable timestamps\n");
    return 1;
  }

  float prio = 1.0f;
  VkDeviceQueueCreateInfo qci{};
  qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  qci.queueFamilyIndex = qfi;
  qci.queueCount = 1;
  qci.pQueuePriorities = &prio;
  VkDeviceCreateInfo dci{};
  dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  dci.queueCreateInfoCount = 1;
  dci.pQueueCreateInfos = &qci;
  VkDevice dev;
  VK_CHECK(vkCreateDevice(pd, &dci, nullptr, &dev));
  LoadDeviceFuncs(dev);
  VkQueue queue;
  vkGetDeviceQueue(dev, qfi, 0, &queue);

  const VkFormat kFormat = VK_FORMAT_R8G8B8A8_UNORM;
  VkImageCreateInfo img_ci{};
  img_ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  img_ci.imageType = VK_IMAGE_TYPE_2D;
  img_ci.format = kFormat;
  img_ci.extent = {cfg.width, cfg.height, 1};
  img_ci.mipLevels = 1;
  img_ci.arrayLayers = 1;
  img_ci.samples = VK_SAMPLE_COUNT_1_BIT;
  img_ci.tiling = VK_IMAGE_TILING_OPTIMAL;
  img_ci.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                 VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  img_ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  VkImage image;
  VK_CHECK(vkCreateImage(dev, &img_ci, nullptr, &image));
  VkMemoryRequirements mreq;
  vkGetImageMemoryRequirements(dev, image, &mreq);
  VkMemoryAllocateInfo mai{};
  mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  mai.allocationSize = mreq.size;
  mai.memoryTypeIndex =
      FindMemoryType(pd, mreq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  VkDeviceMemory mem;
  VK_CHECK(vkAllocateMemory(dev, &mai, nullptr, &mem));
  VK_CHECK(vkBindImageMemory(dev, image, mem, 0));

  // Optional depth attachment. BD's real passes carry one, and our own LRZ
  // note says Turnip disables Adreno LRZ when depth enters via LOAD_OP_LOAD -
  // so depth is the most likely thing to reactivate a per-row cost that the
  // colour-only harness showed as free.
  const VkFormat kDepthFormat = VK_FORMAT_D24_UNORM_S8_UINT;
  VkImage depth_image = VK_NULL_HANDLE;
  VkDeviceMemory depth_mem = VK_NULL_HANDLE;
  VkImageView depth_view = VK_NULL_HANDLE;
  if (cfg.depth) {
    VkImageCreateInfo dci2 = img_ci;
    dci2.format = kDepthFormat;
    dci2.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    VK_CHECK(vkCreateImage(dev, &dci2, nullptr, &depth_image));
    VkMemoryRequirements dreq;
    vkGetImageMemoryRequirements(dev, depth_image, &dreq);
    VkMemoryAllocateInfo dmai{};
    dmai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    dmai.allocationSize = dreq.size;
    dmai.memoryTypeIndex = FindMemoryType(pd, dreq.memoryTypeBits,
                                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VK_CHECK(vkAllocateMemory(dev, &dmai, nullptr, &depth_mem));
    VK_CHECK(vkBindImageMemory(dev, depth_image, depth_mem, 0));
    VkImageViewCreateInfo dvi{};
    dvi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    dvi.image = depth_image;
    dvi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    dvi.format = kDepthFormat;
    dvi.subresourceRange = {
        VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, 0, 1, 0, 1};
    VK_CHECK(vkCreateImageView(dev, &dvi, nullptr, &depth_view));
  }

  VkImageViewCreateInfo iv_ci{};
  iv_ci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  iv_ci.image = image;
  iv_ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
  iv_ci.format = kFormat;
  iv_ci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  VkImageView view;
  VK_CHECK(vkCreateImageView(dev, &iv_ci, nullptr, &view));

  const VkAttachmentLoadOp load_op = ParseLoadOp(cfg.load_op);
  VkAttachmentDescription att{};
  att.format = kFormat;
  att.samples = VK_SAMPLE_COUNT_1_BIT;
  att.loadOp = load_op;
  att.storeOp = ParseStoreOp(cfg.store_op);
  att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  // LOAD requires meaningful prior contents, so the pass must start from a
  // layout that has them; CLEAR/DONT_CARE do not.
  att.initialLayout = (load_op == VK_ATTACHMENT_LOAD_OP_LOAD)
                          ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                          : VK_IMAGE_LAYOUT_UNDEFINED;
  att.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  VkAttachmentReference att_ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkAttachmentDescription depth_att{};
  VkAttachmentReference depth_ref{
      1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
  const VkAttachmentLoadOp depth_load_op = ParseLoadOp(cfg.depth_load_op);
  if (cfg.depth) {
    depth_att.format = kDepthFormat;
    depth_att.samples = VK_SAMPLE_COUNT_1_BIT;
    depth_att.loadOp = depth_load_op;
    depth_att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depth_att.stencilLoadOp = depth_load_op;
    depth_att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
    depth_att.initialLayout =
        (depth_load_op == VK_ATTACHMENT_LOAD_OP_LOAD)
            ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
            : VK_IMAGE_LAYOUT_UNDEFINED;
    depth_att.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
  }
  VkAttachmentDescription atts[2] = {att, depth_att};
  VkSubpassDescription sub{};
  sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  sub.colorAttachmentCount = 1;
  sub.pColorAttachments = &att_ref;
  if (cfg.depth) sub.pDepthStencilAttachment = &depth_ref;
  VkRenderPassCreateInfo rp_ci{};
  rp_ci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  rp_ci.attachmentCount = cfg.depth ? 2u : 1u;
  rp_ci.pAttachments = atts;
  rp_ci.subpassCount = 1;
  rp_ci.pSubpasses = &sub;
  VkRenderPass rp;
  VK_CHECK(vkCreateRenderPass(dev, &rp_ci, nullptr, &rp));

  VkFramebufferCreateInfo fb_ci{};
  fb_ci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
  fb_ci.renderPass = rp;
  VkImageView fb_views[2] = {view, depth_view};
  fb_ci.attachmentCount = cfg.depth ? 2u : 1u;
  fb_ci.pAttachments = fb_views;
  fb_ci.width = cfg.width;
  fb_ci.height = cfg.height;
  fb_ci.layers = 1;
  VkFramebuffer fb;
  VK_CHECK(vkCreateFramebuffer(dev, &fb_ci, nullptr, &fb));

  VkShaderModule vs = MakeShader(dev, kFillVert, sizeof(kFillVert));
  VkShaderModule fs;
  if (cfg.flag_shader == "dyn") {
    fs = MakeShader(dev, kFlagsDyn, sizeof(kFlagsDyn));
  } else if (cfg.flag_shader == "spec") {
    fs = MakeShader(dev, kFlagsSpec, sizeof(kFlagsSpec));
  } else {
    fs = MakeShader(dev, kFillFrag, sizeof(kFillFrag));
  }

  VkPushConstantRange pcr{VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                          sizeof(float) + sizeof(uint32_t)};
  VkPipelineLayoutCreateInfo pl_ci{};
  pl_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pl_ci.pushConstantRangeCount = 1;
  pl_ci.pPushConstantRanges = &pcr;
  VkPipelineLayout layout;
  VK_CHECK(vkCreatePipelineLayout(dev, &pl_ci, nullptr, &layout));

  VkPipelineShaderStageCreateInfo stages[2]{};
  stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  stages[0].module = vs;
  stages[0].pName = "main";
  stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[1].module = fs;
  stages[1].pName = "main";
  // The spec arm hands the flag word to the COMPILER, so every gated block
  // folds away at pipeline creation instead of branching per fragment.
  VkSpecializationMapEntry spec_entry{};
  VkSpecializationInfo spec_info{};
  if (cfg.flag_shader == "spec") {
    spec_entry.constantID = 0;
    spec_entry.offset = 0;
    spec_entry.size = sizeof(uint32_t);
    spec_info.mapEntryCount = 1;
    spec_info.pMapEntries = &spec_entry;
    spec_info.dataSize = sizeof(uint32_t);
    spec_info.pData = &cfg.flag_value;
    stages[1].pSpecializationInfo = &spec_info;
  }

  VkPipelineVertexInputStateCreateInfo vin{};
  vin.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  VkPipelineInputAssemblyStateCreateInfo ia{};
  ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  // The draw covers the top view_height rows only - exactly what the emulator
  // does when it renders a 720p guest scene into an EDRAM-span surface.
  VkViewport vp{0.0f, 0.0f, float(cfg.view_width), float(cfg.view_height),
                0.0f, 1.0f};
  VkRect2D sc{{0, 0}, {cfg.view_width, cfg.view_height}};
  VkPipelineViewportStateCreateInfo vps{};
  vps.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  vps.viewportCount = 1;
  vps.pViewports = &vp;
  vps.scissorCount = 1;
  vps.pScissors = &sc;
  VkPipelineRasterizationStateCreateInfo rs{};
  rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rs.polygonMode = VK_POLYGON_MODE_FILL;
  rs.cullMode = VK_CULL_MODE_NONE;
  rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  rs.lineWidth = 1.0f;
  VkPipelineMultisampleStateCreateInfo ms{};
  ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  VkPipelineColorBlendAttachmentState cba{};
  cba.colorWriteMask = 0xF;
  VkPipelineColorBlendStateCreateInfo cb{};
  cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  cb.attachmentCount = 1;
  cb.pAttachments = &cba;
  VkGraphicsPipelineCreateInfo gp_ci{};
  gp_ci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  gp_ci.stageCount = 2;
  gp_ci.pStages = stages;
  gp_ci.pVertexInputState = &vin;
  gp_ci.pInputAssemblyState = &ia;
  gp_ci.pViewportState = &vps;
  gp_ci.pRasterizationState = &rs;
  gp_ci.pMultisampleState = &ms;
  VkPipelineDepthStencilStateCreateInfo ds{};
  ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  ds.depthTestEnable = VK_TRUE;
  ds.depthWriteEnable = VK_TRUE;
  ds.depthCompareOp = VK_COMPARE_OP_ALWAYS;
  gp_ci.pColorBlendState = &cb;
  if (cfg.depth) gp_ci.pDepthStencilState = &ds;
  gp_ci.layout = layout;
  gp_ci.renderPass = rp;
  VkPipeline pipe;
  VK_CHECK(vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &gp_ci, nullptr,
                                     &pipe));

  VkCommandPoolCreateInfo cp_ci{};
  cp_ci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  cp_ci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  cp_ci.queueFamilyIndex = qfi;
  VkCommandPool pool;
  VK_CHECK(vkCreateCommandPool(dev, &cp_ci, nullptr, &pool));
  VkCommandBufferAllocateInfo cb_ai{};
  cb_ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  cb_ai.commandPool = pool;
  cb_ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  cb_ai.commandBufferCount = 1;
  VkCommandBuffer cmd;
  VK_CHECK(vkAllocateCommandBuffers(dev, &cb_ai, &cmd));

  const uint32_t kQueries = (cfg.passes + 1) * 2;
  VkQueryPoolCreateInfo qp_ci{};
  qp_ci.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
  qp_ci.queryType = VK_QUERY_TYPE_TIMESTAMP;
  qp_ci.queryCount = kQueries;
  VkQueryPool qpool;
  VK_CHECK(vkCreateQueryPool(dev, &qp_ci, nullptr, &qpool));

  VkFenceCreateInfo f_ci{};
  f_ci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  VkFence fence;
  VK_CHECK(vkCreateFence(dev, &f_ci, nullptr, &fence));

  const double ns_per_tick = double(props.limits.timestampPeriod);
  std::vector<double> totals;
  totals.reserve(cfg.iters);

  const uint32_t runs = cfg.warmup + cfg.iters;
  for (uint32_t run = 0; run < runs; ++run) {
    VK_CHECK(vkResetCommandBuffer(cmd, 0));
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(cmd, &bi));
    vkCmdResetQueryPool(cmd, qpool, 0, kQueries);

    // LOAD needs the attachment already in COLOR_ATTACHMENT_OPTIMAL with
    // defined contents; UNDEFINED->that transition happens once here.
    if (load_op == VK_ATTACHMENT_LOAD_OP_LOAD) {
      VkImageMemoryBarrier b{};
      b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
      b.oldLayout = (run == 0) ? VK_IMAGE_LAYOUT_UNDEFINED
                               : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
      b.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
      b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      b.image = image;
      b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
      b.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
      b.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
      vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                           VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0,
                           nullptr, 0, nullptr, 1, &b);
    }

    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, qpool, 0);
    for (uint32_t p = 0; p < cfg.passes; ++p) {
      VkClearValue clear[2]{};
      clear[0].color = {{0.1f, 0.2f, 0.3f, 1.0f}};
      clear[1].depthStencil = {1.0f, 0};
      VkRenderPassBeginInfo rp_bi{};
      rp_bi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
      rp_bi.renderPass = rp;
      rp_bi.framebuffer = fb;
      // renderArea is the FULL attachment, matching what the emulator does
      // today. Clamping it is the experiment already run and rejected.
      rp_bi.renderArea = {{0, 0}, {cfg.width, cfg.height}};
      rp_bi.clearValueCount = cfg.depth ? 2u : 1u;
      rp_bi.pClearValues = clear;
      vkCmdBeginRenderPass(cmd, &rp_bi, VK_SUBPASS_CONTENTS_INLINE);
      vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
      // The EDRAM emulation clears inside passes (rt_resolve_clears in our own
      // frame trace). A loadOp=CLEAR over an EDRAM-span attachment is
      // expensive; the question is whether the IN-PASS form costs the same,
      // and whether scissoring it to the drawn region recovers that.
      if (cfg.inpass_clear != "none") {
        VkClearAttachment ca{};
        ca.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        ca.colorAttachment = 0;
        ca.clearValue.color = {{0.4f, 0.5f, 0.6f, 1.0f}};
        VkClearRect cr{};
        cr.layerCount = 1;
        cr.baseArrayLayer = 0;
        cr.rect.offset = {0, 0};
        if (cfg.inpass_clear == "scissor") {
          cr.rect.extent = {cfg.view_width, cfg.view_height};
        } else {
          cr.rect.extent = {cfg.width, cfg.height};
        }
        vkCmdClearAttachments(cmd, 1, &ca, 1, &cr);
      }
      for (uint32_t d = 0; d < cfg.draws; ++d) {
        struct {
          float t;
          uint32_t flags;
        } push{float(d) * 0.031f + float(p) * 0.017f, cfg.flag_value};
        vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(push), &push);
        vkCmdDraw(cmd, 3, 1, 0, 0);
      }
      vkCmdEndRenderPass(cmd);
      vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, qpool,
                          p + 1);
    }
    VK_CHECK(vkEndCommandBuffer(cmd));

    VK_CHECK(vkResetFences(dev, 1, &fence));
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    VK_CHECK(vkQueueSubmit(queue, 1, &si, fence));
    VK_CHECK(vkWaitForFences(dev, 1, &fence, VK_TRUE, UINT64_MAX));

    if (run < cfg.warmup) continue;
    std::vector<uint64_t> ts(cfg.passes + 1);
    VK_CHECK(vkGetQueryPoolResults(
        dev, qpool, 0, cfg.passes + 1, ts.size() * sizeof(uint64_t), ts.data(),
        sizeof(uint64_t), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT));
    double total_us = double(ts[cfg.passes] - ts[0]) * ns_per_tick / 1000.0;
    totals.push_back(total_us);
  }

  std::sort(totals.begin(), totals.end());
  const double median = totals[totals.size() / 2];
  const double per_pass = median / double(cfg.passes);
  // Pixels the draws actually cover, so cost-per-covered-pixel exposes whether
  // the off-screen rows are being paid for.
  const double covered_mpx =
      double(cfg.view_width) * double(cfg.view_height) * double(cfg.draws) *
      double(cfg.passes) / 1e6;

  std::printf(
      "%-10s gpu=%s via %s\n"
      "%-10s rt=%ux%u view=%ux%u loadop=%s storeop=%s depth=%s inpass=%s fs=%s flags=%#x passes=%u draws=%u\n",
      cfg.label, props.deviceName, vkapi::source, cfg.label, cfg.width,
      cfg.height, cfg.view_width, cfg.view_height, cfg.load_op.c_str(),
      cfg.store_op.c_str(),
      cfg.depth ? cfg.depth_load_op.c_str() : "none",
      cfg.inpass_clear.c_str(), cfg.flag_shader.c_str(), cfg.flag_value,
      cfg.passes, cfg.draws);
  std::printf(
      "%-10s median_total=%.1fus  per_pass=%.1fus  min=%.1f max=%.1f  "
      "us_per_covered_Mpx=%.2f\n",
      cfg.label, median, per_pass, totals.front(), totals.back(),
      median / covered_mpx);

  vkDestroyFence(dev, fence, nullptr);
  vkDestroyQueryPool(dev, qpool, nullptr);
  vkDestroyCommandPool(dev, pool, nullptr);
  vkDestroyPipeline(dev, pipe, nullptr);
  vkDestroyPipelineLayout(dev, layout, nullptr);
  vkDestroyShaderModule(dev, vs, nullptr);
  vkDestroyShaderModule(dev, fs, nullptr);
  vkDestroyFramebuffer(dev, fb, nullptr);
  vkDestroyRenderPass(dev, rp, nullptr);
  if (cfg.depth) {
    vkDestroyImageView(dev, depth_view, nullptr);
    vkDestroyImage(dev, depth_image, nullptr);
    vkFreeMemory(dev, depth_mem, nullptr);
  }
  vkDestroyImageView(dev, view, nullptr);
  vkDestroyImage(dev, image, nullptr);
  vkFreeMemory(dev, mem, nullptr);
  vkDestroyDevice(dev, nullptr);
  vkDestroyInstance(inst, nullptr);
  return 0;
}
