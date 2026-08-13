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

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "shaders/fill_vert.h"
#include "shaders/fill_frag.h"

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
    else if (a == "--label") cfg.label = argv[i + 1], ++i;
    else {
      std::fprintf(stderr, "unknown arg: %s\n", a.c_str());
      return 1;
    }
  }
  cfg.view_width = std::min(cfg.view_width, cfg.width);
  cfg.view_height = std::min(cfg.view_height, cfg.height);

  VkApplicationInfo app{};
  app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  app.pApplicationName = "edram_bench";
  app.apiVersion = VK_API_VERSION_1_1;
  VkInstanceCreateInfo ici{};
  ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  ici.pApplicationInfo = &app;
  VkInstance inst;
  VK_CHECK(vkCreateInstance(&ici, nullptr, &inst));

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
  VkSubpassDescription sub{};
  sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  sub.colorAttachmentCount = 1;
  sub.pColorAttachments = &att_ref;
  VkRenderPassCreateInfo rp_ci{};
  rp_ci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  rp_ci.attachmentCount = 1;
  rp_ci.pAttachments = &att;
  rp_ci.subpassCount = 1;
  rp_ci.pSubpasses = &sub;
  VkRenderPass rp;
  VK_CHECK(vkCreateRenderPass(dev, &rp_ci, nullptr, &rp));

  VkFramebufferCreateInfo fb_ci{};
  fb_ci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
  fb_ci.renderPass = rp;
  fb_ci.attachmentCount = 1;
  fb_ci.pAttachments = &view;
  fb_ci.width = cfg.width;
  fb_ci.height = cfg.height;
  fb_ci.layers = 1;
  VkFramebuffer fb;
  VK_CHECK(vkCreateFramebuffer(dev, &fb_ci, nullptr, &fb));

  VkShaderModule vs = MakeShader(dev, kFillVert, sizeof(kFillVert));
  VkShaderModule fs = MakeShader(dev, kFillFrag, sizeof(kFillFrag));

  VkPushConstantRange pcr{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(float)};
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
  gp_ci.pColorBlendState = &cb;
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
      VkClearValue clear{};
      clear.color = {{0.1f, 0.2f, 0.3f, 1.0f}};
      VkRenderPassBeginInfo rp_bi{};
      rp_bi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
      rp_bi.renderPass = rp;
      rp_bi.framebuffer = fb;
      // renderArea is the FULL attachment, matching what the emulator does
      // today. Clamping it is the experiment already run and rejected.
      rp_bi.renderArea = {{0, 0}, {cfg.width, cfg.height}};
      rp_bi.clearValueCount = 1;
      rp_bi.pClearValues = &clear;
      vkCmdBeginRenderPass(cmd, &rp_bi, VK_SUBPASS_CONTENTS_INLINE);
      vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
      for (uint32_t d = 0; d < cfg.draws; ++d) {
        float t = float(d) * 0.031f + float(p) * 0.017f;
        vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(float), &t);
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
      "%-10s gpu=%s  rt=%ux%u view=%ux%u loadop=%s storeop=%s passes=%u "
      "draws=%u\n",
      cfg.label, props.deviceName, cfg.width, cfg.height, cfg.view_width,
      cfg.view_height, cfg.load_op.c_str(), cfg.store_op.c_str(), cfg.passes,
      cfg.draws);
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
  vkDestroyImageView(dev, view, nullptr);
  vkDestroyImage(dev, image, nullptr);
  vkFreeMemory(dev, mem, nullptr);
  vkDestroyDevice(dev, nullptr);
  vkDestroyInstance(inst, nullptr);
  return 0;
}
