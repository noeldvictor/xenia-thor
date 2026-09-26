// The Adreno shader lab: builds graphics pipelines from SPIR-V modules on a
// Vulkan driver (on the PC: host Turnip over the freedreno drm-shim with
// FD_GPU_ID=740, tools/turnip/build_host_shim.sh) and prints the driver's
// VK_KHR_pipeline_executable_properties statistics for every stage.
//
//   shader_lab JOBS [IR_DIR]
//
// JOBS is a text file written by tools/turnip/shader_lab.py, one job per
// block, fields separated by tabs:
//   J <name>
//   S <v|f> <spirv path>                  one line per stage
//   B <set> <binding> <VkDescriptorType> <count> <VkShaderStageFlags>
//   P <push constant size>
//   A <vertex attribute count>            vec4 float attributes, binding 0
//   C <VkFormat>                          one line per color attachment
//   E                                     build the pipeline
// Output lines (tabs): "begin <name>", "stat <name> <executable> <statistic>
// <value>", "ir <name> <executable> <representation> <file>",
// "error <name> <message>", "end <name>". With IR_DIR the internal
// representations (NIR, ir3 assembly) go to IR_DIR/<name>.<exe>.<n>.txt.
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FUNCS(X)                                \
  X(vkEnumeratePhysicalDevices)                 \
  X(vkGetPhysicalDeviceProperties)              \
  X(vkGetPhysicalDeviceFeatures2)               \
  X(vkGetPhysicalDeviceQueueFamilyProperties)   \
  X(vkEnumerateDeviceExtensionProperties)       \
  X(vkCreateDevice)                             \
  X(vkDestroyDevice)                            \
  X(vkDestroyInstance)                          \
  X(vkCreateShaderModule)                       \
  X(vkDestroyShaderModule)                      \
  X(vkCreateDescriptorSetLayout)                \
  X(vkDestroyDescriptorSetLayout)               \
  X(vkCreatePipelineLayout)                     \
  X(vkDestroyPipelineLayout)                    \
  X(vkCreateGraphicsPipelines)                  \
  X(vkDestroyPipeline)                          \
  X(vkGetPipelineExecutablePropertiesKHR)       \
  X(vkGetPipelineExecutableStatisticsKHR)       \
  X(vkGetPipelineExecutableInternalRepresentationsKHR)

#define DECLARE(name) static PFN_##name name;
FUNCS(DECLARE)

static PFN_vkGetInstanceProcAddr get_instance_proc_addr;
static VkInstance instance;
static VkDevice device;
static const char* ir_dir;

enum { kMaxStages = 2, kMaxBindings = 64, kMaxSets = 8, kMaxColors = 8 };

struct binding {
  uint32_t set, binding, type, count, stages;
};

struct job {
  char name[256];
  int stage_count;
  VkShaderStageFlagBits stages[kMaxStages];
  char paths[kMaxStages][1024];
  int binding_count;
  struct binding bindings[kMaxBindings];
  uint32_t push_size;
  uint32_t attributes;
  int color_count;
  VkFormat colors[kMaxColors];
  // K: specialization constant 0 of the fragment stage (the zero rule
  // hybrid's interpolator mask).
  int has_fragment_constant;
  uint32_t fragment_constant;
};

static void* read_file(const char* path, size_t* size) {
  FILE* f = fopen(path, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  *size = (size_t)ftell(f);
  fseek(f, 0, SEEK_SET);
  void* data = malloc(*size);
  if (fread(data, 1, *size, f) != *size) {
    free(data);
    data = NULL;
  }
  fclose(f);
  return data;
}

static void sanitize(char* s) {
  for (; *s; ++s) {
    if (*s == ' ' || *s == '\t' || *s == '/') *s = '_';
  }
}

static void print_statistics(const char* job, VkPipeline pipeline) {
  VkPipelineInfoKHR pipeline_info = {VK_STRUCTURE_TYPE_PIPELINE_INFO_KHR};
  pipeline_info.pipeline = pipeline;
  uint32_t exe_count = 0;
  vkGetPipelineExecutablePropertiesKHR(device, &pipeline_info, &exe_count,
                                       NULL);
  VkPipelineExecutablePropertiesKHR* exes =
      calloc(exe_count, sizeof(*exes));
  for (uint32_t i = 0; i < exe_count; ++i) {
    exes[i].sType = VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_PROPERTIES_KHR;
  }
  vkGetPipelineExecutablePropertiesKHR(device, &pipeline_info, &exe_count,
                                       exes);
  for (uint32_t i = 0; i < exe_count; ++i) {
    char exe_name[VK_MAX_DESCRIPTION_SIZE];
    snprintf(exe_name, sizeof(exe_name), "%s", exes[i].name);
    sanitize(exe_name);
    VkPipelineExecutableInfoKHR exe_info = {
        VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_INFO_KHR};
    exe_info.pipeline = pipeline;
    exe_info.executableIndex = i;
    uint32_t stat_count = 0;
    vkGetPipelineExecutableStatisticsKHR(device, &exe_info, &stat_count, NULL);
    VkPipelineExecutableStatisticKHR* stats =
        calloc(stat_count, sizeof(*stats));
    for (uint32_t j = 0; j < stat_count; ++j) {
      stats[j].sType = VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_STATISTIC_KHR;
    }
    vkGetPipelineExecutableStatisticsKHR(device, &exe_info, &stat_count,
                                         stats);
    for (uint32_t j = 0; j < stat_count; ++j) {
      printf("stat\t%s\t%s\t%s\t", job, exe_name, stats[j].name);
      switch (stats[j].format) {
        case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_BOOL32_KHR:
          printf("%u\n", stats[j].value.b32);
          break;
        case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_INT64_KHR:
          printf("%lld\n", (long long)stats[j].value.i64);
          break;
        case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR:
          printf("%llu\n", (unsigned long long)stats[j].value.u64);
          break;
        default:
          printf("%g\n", stats[j].value.f64);
          break;
      }
    }
    free(stats);
    if (!ir_dir) continue;
    uint32_t ir_count = 0;
    vkGetPipelineExecutableInternalRepresentationsKHR(device, &exe_info,
                                                      &ir_count, NULL);
    VkPipelineExecutableInternalRepresentationKHR* irs =
        calloc(ir_count, sizeof(*irs));
    for (uint32_t j = 0; j < ir_count; ++j) {
      irs[j].sType =
          VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_INTERNAL_REPRESENTATION_KHR;
    }
    // First call: sizes; second call: the text.
    vkGetPipelineExecutableInternalRepresentationsKHR(device, &exe_info,
                                                      &ir_count, irs);
    for (uint32_t j = 0; j < ir_count; ++j) {
      irs[j].pData = malloc(irs[j].dataSize + 1);
    }
    vkGetPipelineExecutableInternalRepresentationsKHR(device, &exe_info,
                                                      &ir_count, irs);
    for (uint32_t j = 0; j < ir_count; ++j) {
      char path[2048];
      snprintf(path, sizeof(path), "%s/%s.%s.%u.txt", ir_dir, job, exe_name,
               j);
      FILE* f = fopen(path, "wb");
      if (f) {
        fwrite(irs[j].pData, 1, irs[j].dataSize, f);
        fclose(f);
      }
      printf("ir\t%s\t%s\t%s\t%s\n", job, exe_name, irs[j].name, path);
      free(irs[j].pData);
    }
    free(irs);
  }
  free(exes);
}

static void run_job(const struct job* job) {
  printf("begin\t%s\n", job->name);
  fflush(stdout);
  VkShaderModule modules[kMaxStages] = {0};
  VkPipelineShaderStageCreateInfo stages[kMaxStages];
  VkSpecializationMapEntry fragment_entry = {0, 0, sizeof(uint32_t)};
  VkSpecializationInfo fragment_specialization = {
      1, &fragment_entry, sizeof(uint32_t), &job->fragment_constant};
  VkDescriptorSetLayout set_layouts[kMaxSets] = {0};
  VkPipelineLayout layout = VK_NULL_HANDLE;
  VkPipeline pipeline = VK_NULL_HANDLE;
  VkResult result;
  for (int i = 0; i < job->stage_count; ++i) {
    size_t size;
    void* code = read_file(job->paths[i], &size);
    if (!code) {
      printf("error\t%s\tcannot read %s\n", job->name, job->paths[i]);
      goto done;
    }
    VkShaderModuleCreateInfo module_info = {
        VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    module_info.codeSize = size;
    module_info.pCode = code;
    result = vkCreateShaderModule(device, &module_info, NULL, &modules[i]);
    free(code);
    if (result != VK_SUCCESS) {
      printf("error\t%s\tvkCreateShaderModule %d\n", job->name, result);
      goto done;
    }
    memset(&stages[i], 0, sizeof(stages[i]));
    stages[i].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[i].stage = job->stages[i];
    stages[i].module = modules[i];
    stages[i].pName = "main";
    if (job->has_fragment_constant &&
        job->stages[i] == VK_SHADER_STAGE_FRAGMENT_BIT) {
      stages[i].pSpecializationInfo = &fragment_specialization;
    }
  }

  uint32_t set_count = 0;
  for (int i = 0; i < job->binding_count; ++i) {
    if (job->bindings[i].set + 1 > set_count) {
      set_count = job->bindings[i].set + 1;
    }
  }
  for (uint32_t set = 0; set < set_count; ++set) {
    VkDescriptorSetLayoutBinding bindings[kMaxBindings];
    uint32_t count = 0;
    for (int i = 0; i < job->binding_count; ++i) {
      const struct binding* b = &job->bindings[i];
      if (b->set != set) continue;
      memset(&bindings[count], 0, sizeof(bindings[count]));
      bindings[count].binding = b->binding;
      bindings[count].descriptorType = (VkDescriptorType)b->type;
      bindings[count].descriptorCount = b->count;
      bindings[count].stageFlags = b->stages;
      ++count;
    }
    VkDescriptorSetLayoutCreateInfo set_info = {
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    set_info.bindingCount = count;
    set_info.pBindings = bindings;
    result = vkCreateDescriptorSetLayout(device, &set_info, NULL,
                                         &set_layouts[set]);
    if (result != VK_SUCCESS) {
      printf("error\t%s\tvkCreateDescriptorSetLayout %d\n", job->name, result);
      goto done;
    }
  }
  VkPushConstantRange push_range = {
      VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
      job->push_size};
  VkPipelineLayoutCreateInfo layout_info = {
      VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  layout_info.setLayoutCount = set_count;
  layout_info.pSetLayouts = set_layouts;
  layout_info.pushConstantRangeCount = job->push_size ? 1 : 0;
  layout_info.pPushConstantRanges = &push_range;
  result = vkCreatePipelineLayout(device, &layout_info, NULL, &layout);
  if (result != VK_SUCCESS) {
    printf("error\t%s\tvkCreatePipelineLayout %d\n", job->name, result);
    goto done;
  }

  VkVertexInputBindingDescription vertex_binding = {
      0, 16 * job->attributes, VK_VERTEX_INPUT_RATE_VERTEX};
  VkVertexInputAttributeDescription attributes[32];
  for (uint32_t i = 0; i < job->attributes && i < 32; ++i) {
    attributes[i].location = i;
    attributes[i].binding = 0;
    attributes[i].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attributes[i].offset = 16 * i;
  }
  VkPipelineVertexInputStateCreateInfo vertex_input = {
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  vertex_input.vertexBindingDescriptionCount = job->attributes ? 1 : 0;
  vertex_input.pVertexBindingDescriptions = &vertex_binding;
  vertex_input.vertexAttributeDescriptionCount = job->attributes;
  vertex_input.pVertexAttributeDescriptions = attributes;
  VkPipelineInputAssemblyStateCreateInfo input_assembly = {
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
  input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkPipelineViewportStateCreateInfo viewport = {
      VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
  viewport.viewportCount = 1;
  viewport.scissorCount = 1;
  VkPipelineRasterizationStateCreateInfo raster = {
      VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
  raster.polygonMode = VK_POLYGON_MODE_FILL;
  raster.cullMode = VK_CULL_MODE_NONE;
  raster.frontFace = VK_FRONT_FACE_CLOCKWISE;
  raster.lineWidth = 1.0f;
  VkPipelineMultisampleStateCreateInfo multisample = {
      VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
  multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  VkPipelineDepthStencilStateCreateInfo depth = {
      VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
  depth.depthTestEnable = VK_TRUE;
  depth.depthWriteEnable = VK_TRUE;
  depth.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
  VkPipelineColorBlendAttachmentState blend[kMaxColors];
  memset(blend, 0, sizeof(blend));
  for (int i = 0; i < job->color_count; ++i) {
    blend[i].colorWriteMask = 0xF;
  }
  VkPipelineColorBlendStateCreateInfo color_blend = {
      VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
  color_blend.attachmentCount = job->color_count;
  color_blend.pAttachments = blend;
  VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT,
                                     VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dynamic = {
      VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
  dynamic.dynamicStateCount = 2;
  dynamic.pDynamicStates = dynamic_states;
  VkPipelineRenderingCreateInfo rendering = {
      VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
  rendering.colorAttachmentCount = job->color_count;
  rendering.pColorAttachmentFormats = job->colors;
  rendering.depthAttachmentFormat = VK_FORMAT_D24_UNORM_S8_UINT;
  rendering.stencilAttachmentFormat = VK_FORMAT_D24_UNORM_S8_UINT;
  VkGraphicsPipelineCreateInfo pipeline_info = {
      VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
  pipeline_info.pNext = &rendering;
  pipeline_info.flags = VK_PIPELINE_CREATE_CAPTURE_STATISTICS_BIT_KHR;
  if (ir_dir) {
    pipeline_info.flags |=
        VK_PIPELINE_CREATE_CAPTURE_INTERNAL_REPRESENTATIONS_BIT_KHR;
  }
  pipeline_info.stageCount = job->stage_count;
  pipeline_info.pStages = stages;
  pipeline_info.pVertexInputState = &vertex_input;
  pipeline_info.pInputAssemblyState = &input_assembly;
  pipeline_info.pViewportState = &viewport;
  pipeline_info.pRasterizationState = &raster;
  pipeline_info.pMultisampleState = &multisample;
  pipeline_info.pDepthStencilState = &depth;
  pipeline_info.pColorBlendState = &color_blend;
  pipeline_info.pDynamicState = &dynamic;
  pipeline_info.layout = layout;
  result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info,
                                     NULL, &pipeline);
  if (result != VK_SUCCESS) {
    printf("error\t%s\tvkCreateGraphicsPipelines %d\n", job->name, result);
    goto done;
  }
  print_statistics(job->name, pipeline);

done:
  if (pipeline) vkDestroyPipeline(device, pipeline, NULL);
  if (layout) vkDestroyPipelineLayout(device, layout, NULL);
  for (int i = 0; i < kMaxSets; ++i) {
    if (set_layouts[i]) vkDestroyDescriptorSetLayout(device, set_layouts[i], NULL);
  }
  for (int i = 0; i < kMaxStages; ++i) {
    if (modules[i]) vkDestroyShaderModule(device, modules[i], NULL);
  }
  printf("end\t%s\n", job->name);
  fflush(stdout);
}

static int create_device(void) {
  void* loader = dlopen("libvulkan.so.1", RTLD_NOW);
  if (!loader) {
    fprintf(stderr, "no libvulkan.so.1\n");
    return 0;
  }
  get_instance_proc_addr =
      (PFN_vkGetInstanceProcAddr)dlsym(loader, "vkGetInstanceProcAddr");
  PFN_vkCreateInstance create_instance =
      (PFN_vkCreateInstance)get_instance_proc_addr(NULL, "vkCreateInstance");
  VkApplicationInfo app = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
  app.pApplicationName = "xenia-thor shader lab";
  app.apiVersion = VK_API_VERSION_1_3;
  VkInstanceCreateInfo instance_info = {
      VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
  instance_info.pApplicationInfo = &app;
  if (create_instance(&instance_info, NULL, &instance) != VK_SUCCESS) {
    fprintf(stderr, "vkCreateInstance failed\n");
    return 0;
  }
#define LOAD(name) \
  name = (PFN_##name)get_instance_proc_addr(instance, #name);
  FUNCS(LOAD)
  uint32_t count = 1;
  VkPhysicalDevice physical;
  VkResult result = vkEnumeratePhysicalDevices(instance, &count, &physical);
  if ((result != VK_SUCCESS && result != VK_INCOMPLETE) || !count) {
    fprintf(stderr, "no physical device (the drm-shim LD_PRELOAD?)\n");
    return 0;
  }
  VkPhysicalDeviceProperties props;
  vkGetPhysicalDeviceProperties(physical, &props);
  fprintf(stderr, "device: %s, driver %u.%u.%u\n", props.deviceName,
          VK_API_VERSION_MAJOR(props.driverVersion),
          VK_API_VERSION_MINOR(props.driverVersion),
          VK_API_VERSION_PATCH(props.driverVersion));

  // Every supported feature, like xenia (vulkan_device.cc enables each one it
  // knows when supported, robustBufferAccess included).
  VkPhysicalDevicePipelineExecutablePropertiesFeaturesKHR exe_features = {
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_EXECUTABLE_PROPERTIES_FEATURES_KHR};
  VkPhysicalDeviceVulkan13Features features13 = {
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
  features13.pNext = &exe_features;
  VkPhysicalDeviceVulkan12Features features12 = {
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
  features12.pNext = &features13;
  VkPhysicalDeviceVulkan11Features features11 = {
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
  features11.pNext = &features12;
  VkPhysicalDeviceFeatures2 features = {
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
  features.pNext = &features11;
  vkGetPhysicalDeviceFeatures2(physical, &features);
  if (!exe_features.pipelineExecutableInfo) {
    fprintf(stderr, "no pipelineExecutableInfo\n");
    return 0;
  }
  // LAB_NO_ROBUST=1: the device without robust buffer and image access (the
  // cost of robustness in the compiled shaders).
  const char* no_robust = getenv("LAB_NO_ROBUST");
  if (no_robust && no_robust[0] == '1') {
    features.features.robustBufferAccess = VK_FALSE;
    features13.robustImageAccess = VK_FALSE;
    fprintf(stderr, "robust buffer and image access off\n");
  }

  uint32_t ext_count = 0;
  vkEnumerateDeviceExtensionProperties(physical, NULL, &ext_count, NULL);
  VkExtensionProperties* exts = calloc(ext_count, sizeof(*exts));
  vkEnumerateDeviceExtensionProperties(physical, NULL, &ext_count, exts);
  const char** names = calloc(ext_count, sizeof(*names));
  uint32_t name_count = 0;
  for (uint32_t i = 0; i < ext_count; ++i) {
    const char* n = exts[i].extensionName;
    // Presentation needs instance extensions this program does not enable.
    if (strstr(n, "swapchain") || strstr(n, "display") ||
        strstr(n, "present") || strstr(n, "surface")) {
      continue;
    }
    names[name_count++] = n;
  }

  float priority = 1.0f;
  VkDeviceQueueCreateInfo queue_info = {
      VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
  queue_info.queueFamilyIndex = 0;
  queue_info.queueCount = 1;
  queue_info.pQueuePriorities = &priority;
  VkDeviceCreateInfo device_info = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
  device_info.pNext = &features;
  device_info.queueCreateInfoCount = 1;
  device_info.pQueueCreateInfos = &queue_info;
  device_info.enabledExtensionCount = name_count;
  device_info.ppEnabledExtensionNames = names;
  result = vkCreateDevice(physical, &device_info, NULL, &device);
  if (result != VK_SUCCESS) {
    fprintf(stderr, "vkCreateDevice %d\n", result);
    return 0;
  }
  return 1;
}

static VkShaderStageFlagBits stage_bit(char c) {
  switch (c) {
    case 'v': return VK_SHADER_STAGE_VERTEX_BIT;
    case 'c': return VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
    case 'e': return VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
    case 'g': return VK_SHADER_STAGE_GEOMETRY_BIT;
    default: return VK_SHADER_STAGE_FRAGMENT_BIT;
  }
}

int main(int argc, char** argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: shader_lab JOBS [IR_DIR]\n");
    return 2;
  }
  ir_dir = argc > 2 ? argv[2] : NULL;
  FILE* f = fopen(argv[1], "r");
  if (!f) {
    fprintf(stderr, "cannot open %s\n", argv[1]);
    return 2;
  }
  if (!create_device()) return 1;
  static struct job job;
  char line[4096];
  while (fgets(line, sizeof(line), f)) {
    line[strcspn(line, "\r\n")] = 0;
    char* fields[8] = {0};
    int n = 0;
    for (char* p = strtok(line, "\t"); p && n < 8; p = strtok(NULL, "\t")) {
      fields[n++] = p;
    }
    if (!n) continue;
    switch (fields[0][0]) {
      case 'J':
        memset(&job, 0, sizeof(job));
        snprintf(job.name, sizeof(job.name), "%s", n > 1 ? fields[1] : "?");
        break;
      case 'S':
        if (n > 2 && job.stage_count < kMaxStages) {
          job.stages[job.stage_count] = stage_bit(fields[1][0]);
          snprintf(job.paths[job.stage_count], sizeof(job.paths[0]), "%s",
                   fields[2]);
          ++job.stage_count;
        }
        break;
      case 'B':
        if (n > 5 && job.binding_count < kMaxBindings) {
          struct binding* b = &job.bindings[job.binding_count++];
          b->set = (uint32_t)atoi(fields[1]);
          b->binding = (uint32_t)atoi(fields[2]);
          b->type = (uint32_t)atoi(fields[3]);
          b->count = (uint32_t)atoi(fields[4]);
          b->stages = (uint32_t)atoi(fields[5]);
          if (b->set >= kMaxSets) --job.binding_count;
        }
        break;
      case 'P':
        if (n > 1) job.push_size = (uint32_t)atoi(fields[1]);
        break;
      case 'A':
        if (n > 1) job.attributes = (uint32_t)atoi(fields[1]);
        break;
      case 'C':
        if (n > 1 && job.color_count < kMaxColors) {
          job.colors[job.color_count++] = (VkFormat)atoi(fields[1]);
        }
        break;
      case 'K':
        if (n > 1) {
          job.has_fragment_constant = 1;
          job.fragment_constant = (uint32_t)strtoul(fields[1], NULL, 0);
        }
        break;
      case 'E':
        run_job(&job);
        break;
    }
  }
  fclose(f);
  vkDestroyDevice(device, NULL);
  vkDestroyInstance(instance, NULL);
  return 0;
}
