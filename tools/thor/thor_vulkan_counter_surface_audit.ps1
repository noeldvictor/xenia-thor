param(
    [string[]]$SourcePaths = @(
        "src/xenia/gpu/vulkan/vulkan_pipeline_cache.cc",
        "src/xenia/gpu/vulkan/vulkan_pipeline_cache.h",
        "src/xenia/gpu/vulkan/vulkan_command_processor.cc",
        "src/xenia/gpu/vulkan/vulkan_render_target_cache.cc",
        "src/xenia/gpu/vulkan/vulkan_texture_cache.cc",
        "src/xenia/gpu/vulkan/deferred_command_buffer.cc",
        "src/xenia/ui/vulkan/vulkan_presenter.cc",
        "src/xenia/ui/vulkan/vulkan_diagnostic_counters.h",
        "src/xenia/ui/vulkan/vulkan_gpu_completion_timeline.cc",
        "src/xenia/ui/vulkan/vulkan_upload_buffer_pool.cc",
        "src/xenia/ui/vulkan/single_layout_descriptor_set_pool.cc",
        "src/xenia/ui/vulkan/linked_type_descriptor_set_allocator.cc",
        "src/xenia/ui/vulkan/vulkan_util.cc",
        "src/xenia/ui/vulkan/functions/device_1_0.inc",
        "src/xenia/ui/vulkan/functions/device_khr_swapchain.inc",
        "src/xenia/gpu/draw_util.cc",
        "src/xenia/gpu/draw_util.h"
    )
)

$ErrorActionPreference = "Stop"

function Read-LineFile {
    param([string]$Path)

    if (!(Test-Path -LiteralPath $Path)) {
        throw "Path not found: $Path"
    }
    return @(Get-Content -LiteralPath $Path)
}

function Get-LoadedFiles {
    param([string[]]$Paths)

    $loaded = @{}
    foreach ($path in $Paths) {
        $loaded[$path] = [pscustomobject][ordered]@{
            path = $path
            lines = Read-LineFile $path
        }
    }
    return $loaded
}

function Find-Surface {
    param(
        [hashtable]$Files,
        [string]$Name,
        [string]$Group,
        [string[]]$Paths,
        [string[]]$Patterns,
        [string]$Counter,
        [string]$Note,
        [string]$NextPatch
    )

    $hits = 0
    $firstPath = ""
    $firstLine = 0
    $matchedPatterns = New-Object System.Collections.Generic.HashSet[string]

    foreach ($path in $Paths) {
        if (!$Files.ContainsKey($path)) {
            continue
        }
        $lines = [string[]]$Files[$path].lines
        for ($i = 0; $i -lt $lines.Count; $i++) {
            foreach ($pattern in $Patterns) {
                if ($lines[$i] -match $pattern) {
                    $hits++
                    [void]$matchedPatterns.Add($pattern)
                    if ($firstLine -eq 0) {
                        $firstPath = $path
                        $firstLine = $i + 1
                    }
                }
            }
        }
    }

    $source = "-"
    if ($firstLine -gt 0) {
        $source = ("{0}:{1}" -f $firstPath, $firstLine)
    }

    return [pscustomobject][ordered]@{
        name = $Name
        group = $Group
        status = $(if ($hits -gt 0) { "present" } else { "missing" })
        hits = $hits
        matched_patterns = $(if ($matchedPatterns.Count -gt 0) {
            ($matchedPatterns | Sort-Object) -join ","
        } else {
            "-"
        })
        first_source = $source
        counter = $Counter
        next_patch = $NextPatch
        note = $Note
    }
}

function Write-Surface {
    param([pscustomobject]$Surface)

    Write-Output ("surface name={0} group={1} status={2} hits={3} source={4} counter={5} next_patch={6} note={7}" -f `
        $Surface.name, $Surface.group, $Surface.status, $Surface.hits,
        $Surface.first_source, $Surface.counter, $Surface.next_patch,
        $Surface.note)
}

$files = Get-LoadedFiles $SourcePaths

$surfaces = @(
    @{
        Name = "default_off_counter_skeleton"
        Group = "instrumentation"
        Paths = @(
            "src/xenia/ui/vulkan/vulkan_presenter.cc",
            "src/xenia/ui/vulkan/vulkan_diagnostic_counters.h",
            "src/xenia/gpu/vulkan/vulkan_command_processor.cc",
            "src/xenia/gpu/vulkan/vulkan_pipeline_cache.cc",
            "src/xenia/ui/vulkan/vulkan_presenter.cc"
        )
        Patterns = @(
            "vulkan_trace_perf_counters",
            "VulkanPerfCountersLogSnapshot",
            "VulkanPerfCountersRecordQueueSubmit",
            "VulkanPerfCountersRecordPresent"
        )
        Counter = "vulkan_trace_perf_counters,vulkan_trace_perf_counters_log_interval"
        NextPatch = "run a Thor route capture with counters enabled before any GPU behavior change"
        Note = "counter plumbing must stay default-off and metadata-visible"
    },
    @{
        Name = "draw_pipeline_creation"
        Group = "pipeline"
        Paths = @(
            "src/xenia/gpu/vulkan/vulkan_pipeline_cache.cc",
            "src/xenia/gpu/vulkan/vulkan_pipeline_cache.h",
            "src/xenia/gpu/vulkan/vulkan_render_target_cache.cc"
        )
        Patterns = @(
            "EnsurePipelineCreated",
            "vkCreateGraphicsPipelines",
            "vulkan_trace_pipeline_creation",
            "creation threads"
        )
        Counter = "graphics_pipeline_create_count,graphics_pipeline_create_us,graphics_pipeline_cache_hit,shader_translate_us"
        NextPatch = "wrap EnsurePipelineCreated and the render-target graphics pipeline creates"
        Note = "cold/warm pipeline stalls are the first mature-port Vulkan speed question"
    },
    @{
        Name = "compute_pipeline_creation"
        Group = "pipeline"
        Paths = @(
            "src/xenia/ui/vulkan/vulkan_util.cc",
            "src/xenia/gpu/vulkan/vulkan_render_target_cache.cc"
        )
        Patterns = @(
            "CreateComputePipeline",
            "vkCreateComputePipelines",
            "Resolve Copy",
            "resolve_.*pipeline"
        )
        Counter = "compute_pipeline_create_count,compute_pipeline_create_us,resolve_pipeline_create_count"
        NextPatch = "wrap ui::vulkan::util::CreateComputePipeline call sites"
        Note = "resolve, clear, dump, and utility compute pipelines need separate cold-start attribution"
    },
    @{
        Name = "queue_submission"
        Group = "submission"
        Paths = @(
            "src/xenia/gpu/vulkan/vulkan_command_processor.cc",
            "src/xenia/ui/vulkan/vulkan_gpu_completion_timeline.cc",
            "src/xenia/ui/vulkan/functions/device_1_0.inc"
        )
        Patterns = @(
            "AcquireFenceAndSubmit",
            "SubmitAndUpdateLost",
            "vkQueueSubmit",
            "VkSubmitInfo"
        )
        Counter = "queue_submit_count,queue_submit_us,submitted_command_buffers,wait_semaphore_count"
        NextPatch = "time VulkanGPUCompletionTimeline::AcquireFenceAndSubmit and command-processor submit assembly"
        Note = "submission time and semaphore pressure should be logged before guessing GPU starvation"
    },
    @{
        Name = "present_and_swapchain"
        Group = "present"
        Paths = @(
            "src/xenia/gpu/vulkan/vulkan_command_processor.cc",
            "src/xenia/ui/vulkan/vulkan_presenter.cc",
            "src/xenia/ui/vulkan/functions/device_khr_swapchain.inc"
        )
        Patterns = @(
            "IssueSwap",
            "PaintAndPresentImpl",
            "vkQueuePresentKHR",
            "vkCreateSwapchainKHR",
            "Presentation to the swapchain image"
        )
        Counter = "issue_swap_count,present_count,present_us,acquire_result,paint_result,swapchain_recreate_count"
        NextPatch = "time IssueSwap, PaintAndPresentImpl acquire/submit/present phases separately"
        Note = "only consider Swappy/frame pacing after present/acquire timing proves a pacing wall"
    },
    @{
        Name = "completion_waits"
        Group = "wait"
        Paths = @(
            "src/xenia/gpu/vulkan/vulkan_command_processor.cc",
            "src/xenia/ui/vulkan/vulkan_gpu_completion_timeline.cc",
            "src/xenia/ui/vulkan/vulkan_presenter.cc",
            "src/xenia/ui/vulkan/functions/device_1_0.inc"
        )
        Patterns = @(
            "AwaitAllQueueOperationsCompletion",
            "AwaitSubmission",
            "vkWaitForFences",
            "vkGetFenceStatus"
        )
        Counter = "fence_wait_count,fence_wait_us,await_all_count,await_submission_count"
        NextPatch = "measure queue/fence waits before treating frames as GPU-bound"
        Note = "CPU bubbles caused by host waits should be distinguished from guest CPU/JIT body time"
    },
    @{
        Name = "barrier_batching"
        Group = "barrier"
        Paths = @(
            "src/xenia/gpu/vulkan/vulkan_command_processor.cc",
            "src/xenia/gpu/vulkan/vulkan_render_target_cache.cc",
            "src/xenia/gpu/vulkan/vulkan_texture_cache.cc",
            "src/xenia/gpu/vulkan/deferred_command_buffer.cc",
            "src/xenia/ui/vulkan/functions/device_1_0.inc"
        )
        Patterns = @(
            "SubmitBarriers",
            "PushBufferMemoryBarrier",
            "PushImageMemoryBarrier",
            "vkCmdPipelineBarrier"
        )
        Counter = "barrier_submit_count,buffer_barrier_count,image_barrier_count,barrier_force_end_render_pass_count"
        NextPatch = "count Push*MemoryBarrier and SubmitBarriers by caller/subsystem"
        Note = "tile/mobile GPUs punish unnecessary layout and render-pass breaks"
    },
    @{
        Name = "render_pass_transitions"
        Group = "render_pass"
        Paths = @(
            "src/xenia/gpu/vulkan/vulkan_command_processor.cc",
            "src/xenia/gpu/vulkan/vulkan_render_target_cache.cc",
            "src/xenia/gpu/vulkan/deferred_command_buffer.cc",
            "src/xenia/ui/vulkan/vulkan_presenter.cc"
        )
        Patterns = @(
            "CmdVkBeginRenderPass",
            "vkCmdBeginRenderPass",
            "SubmitBarriersAndEnterRenderTargetCacheRenderPass",
            "VkRenderPassBeginInfo"
        )
        Counter = "render_pass_begin_count,render_pass_forced_end_count,guest_output_render_pass_count"
        NextPatch = "separate game render-target passes from presenter passes"
        Note = "render-pass churn needs attribution before load/store or transient attachment work"
    },
    @{
        Name = "copy_upload_download"
        Group = "copy"
        Paths = @(
            "src/xenia/gpu/vulkan/deferred_command_buffer.cc",
            "src/xenia/gpu/vulkan/vulkan_command_processor.cc",
            "src/xenia/gpu/vulkan/vulkan_render_target_cache.cc",
            "src/xenia/ui/vulkan/vulkan_upload_buffer_pool.cc",
            "src/xenia/ui/vulkan/vulkan_util.cc",
            "src/xenia/ui/vulkan/functions/device_1_0.inc"
        )
        Patterns = @(
            "vkCmdCopyBuffer",
            "vkCmdCopyBufferToImage",
            "vkCmdCopyImageToBuffer",
            "VulkanUploadBufferPool",
            "MemoryPurpose::kUpload",
            "MemoryPurpose::kReadback"
        )
        Counter = "copy_buffer_count,copy_buffer_to_image_count,copy_image_to_buffer_count,upload_bytes,readback_bytes"
        NextPatch = "count copy commands and upload/readback allocation bytes at command-buffer and util layers"
        Note = "mature mobile ports measure external-memory traffic explicitly"
    },
    @{
        Name = "readback_and_mapping"
        Group = "readback"
        Paths = @(
            "src/xenia/gpu/vulkan/vulkan_command_processor.cc",
            "src/xenia/gpu/vulkan/vulkan_render_target_cache.cc",
            "src/xenia/ui/vulkan/vulkan_presenter.cc",
            "src/xenia/ui/vulkan/vulkan_upload_buffer_pool.cc",
            "src/xenia/ui/vulkan/functions/device_1_0.inc"
        )
        Patterns = @(
            "ReadbackSharedMemoryRange",
            "ReadbackEdramBufferRange",
            "CaptureGuestOutput",
            "vkMapMemory",
            "kReadback"
        )
        Counter = "readback_count,readback_bytes,map_memory_count,map_memory_us,guest_output_capture_count"
        NextPatch = "separate debug readbacks from gameplay-required readbacks in metadata"
        Note = "debug screenshots/checksums must not be mistaken for normal speed behavior"
    },
    @{
        Name = "descriptor_churn"
        Group = "descriptor"
        Paths = @(
            "src/xenia/gpu/vulkan/vulkan_command_processor.cc",
            "src/xenia/gpu/vulkan/vulkan_render_target_cache.cc",
            "src/xenia/gpu/vulkan/vulkan_texture_cache.cc",
            "src/xenia/ui/vulkan/single_layout_descriptor_set_pool.cc",
            "src/xenia/ui/vulkan/linked_type_descriptor_set_allocator.cc",
            "src/xenia/ui/vulkan/functions/device_1_0.inc"
        )
        Patterns = @(
            "vkAllocateDescriptorSets",
            "vkUpdateDescriptorSets",
            "SingleLayoutDescriptorSetPool",
            "LinkedTypeDescriptorSetAllocator",
            "AllocateSingleTransientDescriptor"
        )
        Counter = "descriptor_allocate_count,descriptor_update_count,transient_descriptor_count,descriptor_pool_create_count"
        NextPatch = "add per-frame descriptor allocation/update totals and pool-growth counters"
        Note = "descriptor churn is a common hidden CPU cost in Vulkan ports"
    },
    @{
        Name = "resolve_and_edram"
        Group = "resolve"
        Paths = @(
            "src/xenia/gpu/vulkan/vulkan_command_processor.cc",
            "src/xenia/gpu/vulkan/vulkan_render_target_cache.cc",
            "src/xenia/gpu/draw_util.cc",
            "src/xenia/gpu/draw_util.h"
        )
        Patterns = @(
            "Resolve\(",
            "GetResolveInfo",
            "ResolveCopyShader",
            "ReadbackEdramBufferRange",
            "PerformTransfersAndResolveClears",
            "MarkRangeAsResolved"
        )
        Counter = "resolve_count,resolve_copy_count,resolve_clear_count,resolve_readback_count,resolve_pixels"
        NextPatch = "counter-tag resolve paths by copy shader, clear path, and readback fallback"
        Note = "Xenos EDRAM resolve cost is a renderer-specific candidate, but it needs path counters first"
    },
    @{
        Name = "present_debug_milestones"
        Group = "milestone"
        Paths = @(
            "src/xenia/gpu/vulkan/vulkan_command_processor.cc",
            "src/xenia/ui/vulkan/vulkan_presenter.cc"
        )
        Patterns = @(
            "GPU swap trace",
            "Vulkan IssueSwap",
            "RequestSwapTexture",
            "RefreshGuestOutputImpl",
            "PaintAndPresentImpl"
        )
        Counter = "vd_swap_seen,issue_swap_seen,request_swap_texture_seen,refresh_guest_output_seen,present_seen"
        NextPatch = "emit one compact per-capture milestone line before RenderDoc/AGI work"
        Note = "black-screen or FPS claims need a console-GPU milestone ladder"
    }
)

Write-Output "# Vulkan Counter Surface Audit"
Write-Output ""
Write-Output ("source_count={0}" -f $files.Count)
Write-Output ("safe_behavior_patch=none")
Write-Output ("recommended_next_patch=thor_route_capture_with_vulkan_trace_perf_counters")
Write-Output ("counter_groups=pipeline,submission,present,barrier,copy_upload_readback,descriptor,render_pass,resolve,completion_wait,milestone")
Write-Output ("capture_metadata=cold_warm_pipeline_cache,pipeline_creates,pipeline_create_us,queue_submit_count,queue_submit_us,present_us,barriers,readbacks,uploads,copies,descriptor_updates,resolve_counts,fence_wait_us")
Write-Output ("do_not=broad_renderer_rewrite,renderdoc_fps_capture,swappy_before_present_timing")
Write-Output ""
Write-Output "## Surfaces"

$missing = 0
foreach ($surfaceSpec in $surfaces) {
    $surface = Find-Surface -Files $files `
        -Name $surfaceSpec.Name `
        -Group $surfaceSpec.Group `
        -Paths $surfaceSpec.Paths `
        -Patterns $surfaceSpec.Patterns `
        -Counter $surfaceSpec.Counter `
        -Note $surfaceSpec.Note `
        -NextPatch $surfaceSpec.NextPatch
    if ($surface.status -ne "present") {
        $missing++
    }
    Write-Surface $surface
}

Write-Output ""
Write-Output "## Decision"
Write-Output ("surfaces_missing={0}" -f $missing)
Write-Output "vulkan_speed_lane_status=diagnostics_ready_no_behavior_change"
Write-Output "blue_dragon_lane_bias=stay_on_a64_while_main_thread_is_wall"
Write-Output "next_gpu_slice=thor_route_capture_with_vulkan_trace_perf_counters"
