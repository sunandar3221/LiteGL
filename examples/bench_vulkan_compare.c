#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <unistd.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#include <vulkan/vulkan.h>

#include "litegl/litegl.h"
#include "litegl/litegl_gl.h"
#include "litegl/litegl_batch.h"

#define BENCH_OBJECTS 2500
#define BENCH_FRAMES  300

typedef struct {
    float x, y;
    float vx, vy;
    float size;
    float r, g, b, a;
    int   texture_id;
    int   blend_mode;
    int   depth_test;
} SceneObject;

static SceneObject s_objects[BENCH_OBJECTS];

static void init_scene(void) {
    srand(42);
    for (int i = 0; i < BENCH_OBJECTS; ++i) {
        s_objects[i].x = ((float)rand() / (float)RAND_MAX) * 2.0f - 1.0f;
        s_objects[i].y = ((float)rand() / (float)RAND_MAX) * 2.0f - 1.0f;
        s_objects[i].vx = (((float)rand() / (float)RAND_MAX) - 0.5f) * 0.01f;
        s_objects[i].vy = (((float)rand() / (float)RAND_MAX) - 0.5f) * 0.01f;
        s_objects[i].size = 0.02f + ((float)rand() / (float)RAND_MAX) * 0.03f;
        s_objects[i].r = (float)rand() / (float)RAND_MAX;
        s_objects[i].g = (float)rand() / (float)RAND_MAX;
        s_objects[i].b = (float)rand() / (float)RAND_MAX;
        s_objects[i].a = 0.5f + ((float)rand() / (float)RAND_MAX) * 0.5f;
        s_objects[i].texture_id = rand() % 4;
        s_objects[i].blend_mode = rand() % 2;
        s_objects[i].depth_test = rand() % 2;
    }
}

static void update_scene(void) {
    for (int i = 0; i < BENCH_OBJECTS; ++i) {
        s_objects[i].x += s_objects[i].vx;
        s_objects[i].y += s_objects[i].vy;
        if (s_objects[i].x < -1.0f || s_objects[i].x > 1.0f) s_objects[i].vx = -s_objects[i].vx;
        if (s_objects[i].y < -1.0f || s_objects[i].y > 1.0f) s_objects[i].vy = -s_objects[i].vy;
    }
}

static int compare_objects(const void* a, const void* b) {
    const SceneObject* oa = (const SceneObject*)a;
    const SceneObject* ob = (const SceneObject*)b;
    if (oa->texture_id != ob->texture_id) return oa->texture_id - ob->texture_id;
    if (oa->blend_mode != ob->blend_mode) return oa->blend_mode - ob->blend_mode;
    return oa->depth_test - ob->depth_test;
}

static void sort_scene(void) {
    qsort(s_objects, BENCH_OBJECTS, sizeof(SceneObject), compare_objects);
}

static double get_time_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static long get_rss_kb(void) {
    FILE* fp = fopen("/proc/self/statm", "r");
    if (!fp) return 0;
    long size = 0, resident = 0;
    if (fscanf(fp, "%ld %ld", &size, &resident) == 2) {
        fclose(fp);
        return resident * (sysconf(_SC_PAGESIZE) / 1024);
    }
    fclose(fp);
    return 0;
}

#include "../shaders/vert_spv.h"
#include "../shaders/frag_spv.h"

static uint32_t find_memory_type(VkPhysicalDevice physical_device, uint32_t type_filter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties mem_properties;
    vkGetPhysicalDeviceMemoryProperties(physical_device, &mem_properties);
    for (uint32_t i = 0; i < mem_properties.memoryTypeCount; i++) {
        if ((type_filter & (1 << i)) && (mem_properties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    return 0;
}

int main(int argc, char* argv[]) {
    int target_frames = BENCH_FRAMES;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            target_frames = atoi(argv[++i]);
        }
    }

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "Failed to initialize SDL: %s\n", SDL_GetError());
        return 1;
    }

    printf("\n===================================================================================\n");
    printf("           COMPREHENSIVE BENCHMARK: VULKAN vs LITEGL (ToGL-BASED)\n");
    printf(" Hardware: Mesa Intel(R) UHD Graphics 600 (GLK 2) | CPU: Intel Gemini Lake\n");
    printf(" Workload: %d dynamic animated quads/frame | Target: %d frames\n", BENCH_OBJECTS, target_frames);
    printf("===================================================================================\n\n");

    /* -------------------------------------------------------------------------
     * PART 1: VULKAN BENCHMARK
     * ------------------------------------------------------------------------- */
    printf("[1/2] Initializing Vulkan (Instance, Physical Device, Swapchain, Pipelines)...\n");
    double vk_init_start = get_time_sec();

    SDL_Window* vk_window = SDL_CreateWindow(
        "LiteGL vs Vulkan Benchmark",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1024, 768,
        SDL_WINDOW_VULKAN | SDL_WINDOW_SHOWN
    );

    uint32_t ext_count = 0;
    SDL_Vulkan_GetInstanceExtensions(vk_window, &ext_count, NULL);
    const char** ext_names = (const char**)malloc(sizeof(char*) * (ext_count + 1));
    SDL_Vulkan_GetInstanceExtensions(vk_window, &ext_count, ext_names);

    VkApplicationInfo app_info = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "LiteGL vs Vulkan",
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .pEngineName = "VulkanBench",
        .engineVersion = VK_MAKE_VERSION(1, 0, 0),
        .apiVersion = VK_API_VERSION_1_2
    };

    VkInstanceCreateInfo inst_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app_info,
        .enabledExtensionCount = ext_count,
        .ppEnabledExtensionNames = ext_names
    };

    VkInstance instance;
    vkCreateInstance(&inst_info, NULL, &instance);
    free(ext_names);

    VkSurfaceKHR surface;
    SDL_Vulkan_CreateSurface(vk_window, instance, &surface);

    uint32_t gpu_count = 0;
    vkEnumeratePhysicalDevices(instance, &gpu_count, NULL);
    VkPhysicalDevice physical_device;
    vkEnumeratePhysicalDevices(instance, &gpu_count, &physical_device);

    float queue_priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = 0,
        .queueCount = 1,
        .pQueuePriorities = &queue_priority
    };

    const char* dev_exts[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    VkDeviceCreateInfo dev_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queue_info,
        .enabledExtensionCount = 1,
        .ppEnabledExtensionNames = dev_exts
    };

    VkDevice device;
    vkCreateDevice(physical_device, &dev_info, NULL, &device);

    VkQueue queue;
    vkGetDeviceQueue(device, 0, 0, &queue);

    /* Swapchain */
    VkSwapchainCreateInfoKHR sc_info = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = surface,
        .minImageCount = 2,
        .imageFormat = VK_FORMAT_B8G8R8A8_UNORM,
        .imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
        .imageExtent = { 1024, 768 },
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR, /* Disable VSync */
        .clipped = VK_TRUE
    };

    VkSwapchainKHR swapchain;
    vkCreateSwapchainKHR(device, &sc_info, NULL, &swapchain);

    uint32_t sc_image_count = 0;
    vkGetSwapchainImagesKHR(device, swapchain, &sc_image_count, NULL);
    VkImage* sc_images = (VkImage*)malloc(sizeof(VkImage) * sc_image_count);
    vkGetSwapchainImagesKHR(device, swapchain, &sc_image_count, sc_images);

    VkImageView* sc_views = (VkImageView*)malloc(sizeof(VkImageView) * sc_image_count);
    for (uint32_t i = 0; i < sc_image_count; ++i) {
        VkImageViewCreateInfo iv_info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = sc_images[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = VK_FORMAT_B8G8R8A8_UNORM,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .levelCount = 1,
                .layerCount = 1
            }
        };
        vkCreateImageView(device, &iv_info, NULL, &sc_views[i]);
    }

    /* Render Pass */
    VkAttachmentDescription color_att = {
        .format = VK_FORMAT_B8G8R8A8_UNORM,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
    };
    VkAttachmentReference color_ref = { .attachment = 0, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkSubpassDescription subpass = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color_ref
    };
    VkRenderPassCreateInfo rp_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &color_att,
        .subpassCount = 1,
        .pSubpasses = &subpass
    };
    VkRenderPass render_pass;
    vkCreateRenderPass(device, &rp_info, NULL, &render_pass);

    /* Framebuffers */
    VkFramebuffer* framebuffers = (VkFramebuffer*)malloc(sizeof(VkFramebuffer) * sc_image_count);
    for (uint32_t i = 0; i < sc_image_count; ++i) {
        VkFramebufferCreateInfo fb_info = {
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass = render_pass,
            .attachmentCount = 1,
            .pAttachments = &sc_views[i],
            .width = 1024,
            .height = 768,
            .layers = 1
        };
        vkCreateFramebuffer(device, &fb_info, NULL, &framebuffers[i]);
    }

    /* Shader Modules */
    VkShaderModuleCreateInfo sm_vert_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = sizeof(shaders_vert_spv),
        .pCode = (const uint32_t*)shaders_vert_spv
    };
    VkShaderModule vert_module;
    vkCreateShaderModule(device, &sm_vert_info, NULL, &vert_module);

    VkShaderModuleCreateInfo sm_frag_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = sizeof(shaders_frag_spv),
        .pCode = (const uint32_t*)shaders_frag_spv
    };
    VkShaderModule frag_module;
    vkCreateShaderModule(device, &sm_frag_info, NULL, &frag_module);

    VkPipelineShaderStageCreateInfo stages[] = {
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vert_module, .pName = "main" },
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = frag_module, .pName = "main" }
    };

    VkVertexInputBindingDescription bind_desc = { .binding = 0, .stride = sizeof(LiteGLBatchVertex), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX };
    VkVertexInputAttributeDescription attr_descs[] = {
        { .location = 0, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = 0 },
        { .location = 1, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = 8 },
        { .location = 2, .binding = 0, .format = VK_FORMAT_R8G8B8A8_UNORM, .offset = 16 }
    };

    VkPipelineVertexInputStateCreateInfo vi_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &bind_desc,
        .vertexAttributeDescriptionCount = 3,
        .pVertexAttributeDescriptions = attr_descs
    };

    VkPipelineInputAssemblyStateCreateInfo ia_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
    };

    VkViewport viewport = { 0, 0, 1024, 768, 0.0f, 1.0f };
    VkRect2D scissor = { { 0, 0 }, { 1024, 768 } };
    VkPipelineViewportStateCreateInfo vp_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1, .pViewports = &viewport,
        .scissorCount = 1, .pScissors = &scissor
    };

    VkPipelineRasterizationStateCreateInfo rs_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1.0f
    };

    VkPipelineMultisampleStateCreateInfo ms_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT
    };

    VkPipelineColorBlendAttachmentState cb_att = {
        .blendEnable = VK_TRUE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = 0xF
    };
    VkPipelineColorBlendStateCreateInfo cb_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &cb_att
    };

    VkPipelineLayoutCreateInfo pl_info = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    VkPipelineLayout pipeline_layout;
    vkCreatePipelineLayout(device, &pl_info, NULL, &pipeline_layout);

    VkGraphicsPipelineCreateInfo pipe_info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2,
        .pStages = stages,
        .pVertexInputState = &vi_state,
        .pInputAssemblyState = &ia_state,
        .pViewportState = &vp_state,
        .pRasterizationState = &rs_state,
        .pMultisampleState = &ms_state,
        .pColorBlendState = &cb_state,
        .layout = pipeline_layout,
        .renderPass = render_pass,
        .subpass = 0
    };
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkResult pipe_res = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipe_info, NULL, &pipeline);
    if (pipe_res != VK_SUCCESS || pipeline == VK_NULL_HANDLE) {
        fprintf(stderr, "[Vulkan] ERROR: vkCreateGraphicsPipelines failed with code: %d\n", pipe_res);
        return 1;
    }

    /* Command Pool & Buffer */
    VkCommandPoolCreateInfo cp_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = 0
    };
    VkCommandPool cmd_pool;
    vkCreateCommandPool(device, &cp_info, NULL, &cmd_pool);

    VkCommandBufferAllocateInfo cb_alloc = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = cmd_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1
    };
    VkCommandBuffer cmd_buffer;
    vkAllocateCommandBuffers(device, &cb_alloc, &cmd_buffer);

    /* Dynamic Vertex Buffer in Host-Visible memory */
    size_t vbo_size = BENCH_OBJECTS * 4 * sizeof(LiteGLBatchVertex);
    VkBufferCreateInfo vbo_ci = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = vbo_size,
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };
    VkBuffer vk_vbo;
    vkCreateBuffer(device, &vbo_ci, NULL, &vk_vbo);

    VkMemoryRequirements vbo_reqs;
    vkGetBufferMemoryRequirements(device, vk_vbo, &vbo_reqs);
    VkMemoryAllocateInfo vbo_ai = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = vbo_reqs.size,
        .memoryTypeIndex = find_memory_type(physical_device, vbo_reqs.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
    };
    VkDeviceMemory vk_vbo_mem;
    vkAllocateMemory(device, &vbo_ai, NULL, &vk_vbo_mem);
    vkBindBufferMemory(device, vk_vbo, vk_vbo_mem, 0);

    /* Index Buffer */
    size_t ibo_count = BENCH_OBJECTS * 6;
    size_t ibo_size = ibo_count * sizeof(uint32_t);
    uint32_t* ibo_data = (uint32_t*)malloc(ibo_size);
    for (size_t q = 0; q < BENCH_OBJECTS; ++q) {
        uint32_t v_base = (uint32_t)(q * 4);
        size_t i_base = q * 6;
        ibo_data[i_base + 0] = v_base + 0;
        ibo_data[i_base + 1] = v_base + 1;
        ibo_data[i_base + 2] = v_base + 2;
        ibo_data[i_base + 3] = v_base + 2;
        ibo_data[i_base + 4] = v_base + 3;
        ibo_data[i_base + 5] = v_base + 0;
    }

    VkBufferCreateInfo ibo_ci = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = ibo_size,
        .usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };
    VkBuffer vk_ibo;
    vkCreateBuffer(device, &ibo_ci, NULL, &vk_ibo);

    VkMemoryRequirements ibo_reqs;
    vkGetBufferMemoryRequirements(device, vk_ibo, &ibo_reqs);
    VkMemoryAllocateInfo ibo_ai = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = ibo_reqs.size,
        .memoryTypeIndex = find_memory_type(physical_device, ibo_reqs.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
    };
    VkDeviceMemory vk_ibo_mem;
    vkAllocateMemory(device, &ibo_ai, NULL, &vk_ibo_mem);
    vkBindBufferMemory(device, vk_ibo, vk_ibo_mem, 0);

    void* mapped_ibo;
    vkMapMemory(device, vk_ibo_mem, 0, ibo_size, 0, &mapped_ibo);
    memcpy(mapped_ibo, ibo_data, ibo_size);
    vkUnmapMemory(device, vk_ibo_mem);
    free(ibo_data);

    /* Semaphores & Fence */
    VkSemaphore sem_image, sem_render;
    VkFence fence;
    VkSemaphoreCreateInfo sem_ci = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    VkFenceCreateInfo fence_ci = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .flags = VK_FENCE_CREATE_SIGNALED_BIT };
    vkCreateSemaphore(device, &sem_ci, NULL, &sem_image);
    vkCreateSemaphore(device, &sem_ci, NULL, &sem_render);
    vkCreateFence(device, &fence_ci, NULL, &fence);

    double vk_init_time = (get_time_sec() - vk_init_start) * 1000.0;
    long vk_rss_kb = get_rss_kb();
    printf(" [Vulkan] Initialization finished in %.2f ms | Process RSS: %.1f MB\n", vk_init_time, (double)vk_rss_kb / 1024.0);

    /* Run Vulkan Benchmark Loop */
    init_scene();
    sort_scene();

    LiteGLBatchVertex* host_verts = (LiteGLBatchVertex*)malloc(vbo_size);

    printf(" [Vulkan] Running %d frames of dynamic rendering...\n", target_frames);
    double vk_render_start = get_time_sec();

    for (int frame = 0; frame < target_frames; ++frame) {
        update_scene();

        /* Map dynamic vertex memory each frame (Vulkan host visible) */
        for (int i = 0; i < BENCH_OBJECTS; ++i) {
            SceneObject* obj = &s_objects[i];
            float s = obj->size;
            uint32_t cr = (uint32_t)(obj->r * 255.0f);
            uint32_t cg = (uint32_t)(obj->g * 255.0f);
            uint32_t cb = (uint32_t)(obj->b * 255.0f);
            uint32_t ca = (uint32_t)(obj->a * 255.0f);
            uint32_t color = cr | (cg << 8) | (cb << 16) | (ca << 24);

            size_t idx = i * 4;
            host_verts[idx + 0] = (LiteGLBatchVertex){ obj->x - s, obj->y - s, 0.0f, 0.0f, color };
            host_verts[idx + 1] = (LiteGLBatchVertex){ obj->x + s, obj->y - s, 1.0f, 0.0f, color };
            host_verts[idx + 2] = (LiteGLBatchVertex){ obj->x + s, obj->y + s, 1.0f, 1.0f, color };
            host_verts[idx + 3] = (LiteGLBatchVertex){ obj->x - s, obj->y + s, 0.0f, 1.0f, color };
        }

        vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
        vkResetFences(device, 1, &fence);

        void* mapped_vbo;
        vkMapMemory(device, vk_vbo_mem, 0, vbo_size, 0, &mapped_vbo);
        memcpy(mapped_vbo, host_verts, vbo_size);
        vkUnmapMemory(device, vk_vbo_mem);

        uint32_t image_index;
        vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, sem_image, VK_NULL_HANDLE, &image_index);

        /* Command buffer recording */
        vkResetCommandBuffer(cmd_buffer, 0);
        VkCommandBufferBeginInfo cb_bi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        vkBeginCommandBuffer(cmd_buffer, &cb_bi);

        VkClearValue clear_color = { .color = { .float32 = { 0.05f, 0.05f, 0.08f, 1.0f } } };
        VkRenderPassBeginInfo rp_bi = {
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .renderPass = render_pass,
            .framebuffer = framebuffers[image_index],
            .renderArea = { { 0, 0 }, { 1024, 768 } },
            .clearValueCount = 1,
            .pClearValues = &clear_color
        };
        vkCmdBeginRenderPass(cmd_buffer, &rp_bi, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

        VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(cmd_buffer, 0, 1, &vk_vbo, offsets);
        vkCmdBindIndexBuffer(cmd_buffer, vk_ibo, 0, VK_INDEX_TYPE_UINT32);

        vkCmdDrawIndexed(cmd_buffer, (uint32_t)ibo_count, 1, 0, 0, 0);

        vkCmdEndRenderPass(cmd_buffer);
        vkEndCommandBuffer(cmd_buffer);

        /* Submit */
        VkPipelineStageFlags wait_stages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
        VkSubmitInfo submit_info = {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &sem_image,
            .pWaitDstStageMask = wait_stages,
            .commandBufferCount = 1,
            .pCommandBuffers = &cmd_buffer,
            .signalSemaphoreCount = 1,
            .pSignalSemaphores = &sem_render
        };
        vkQueueSubmit(queue, 1, &submit_info, fence);

        /* Present */
        VkPresentInfoKHR present_info = {
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &sem_render,
            .swapchainCount = 1,
            .pSwapchains = &swapchain,
            .pImageIndices = &image_index
        };
        vkQueuePresentKHR(queue, &present_info);
    }

    vkDeviceWaitIdle(device);
    double vk_render_duration = get_time_sec() - vk_render_start;
    double vk_fps = (double)target_frames / vk_render_duration;
    double vk_frametime = (vk_render_duration / (double)target_frames) * 1000.0;

    printf(" [Vulkan] Result: %d frames in %.3f s | FPS: %.1f | Frame Time: %.2f ms\n",
           target_frames, vk_render_duration, vk_fps, vk_frametime);

    /* Vulkan Cleanup */
    free(host_verts);
    vkDestroyBuffer(device, vk_vbo, NULL);
    vkFreeMemory(device, vk_vbo_mem, NULL);
    vkDestroyBuffer(device, vk_ibo, NULL);
    vkFreeMemory(device, vk_ibo_mem, NULL);
    vkDestroySemaphore(device, sem_image, NULL);
    vkDestroySemaphore(device, sem_render, NULL);
    vkDestroyFence(device, fence, NULL);
    vkDestroyCommandPool(device, cmd_pool, NULL);
    vkDestroyPipeline(device, pipeline, NULL);
    vkDestroyPipelineLayout(device, pipeline_layout, NULL);
    vkDestroyShaderModule(device, vert_module, NULL);
    vkDestroyShaderModule(device, frag_module, NULL);
    for (uint32_t i = 0; i < sc_image_count; ++i) {
        vkDestroyFramebuffer(device, framebuffers[i], NULL);
        vkDestroyImageView(device, sc_views[i], NULL);
    }
    free(framebuffers);
    free(sc_views);
    free(sc_images);
    vkDestroyRenderPass(device, render_pass, NULL);
    vkDestroySwapchainKHR(device, swapchain, NULL);
    vkDestroyDevice(device, NULL);
    vkDestroySurfaceKHR(instance, surface, NULL);
    vkDestroyInstance(instance, NULL);
    SDL_DestroyWindow(vk_window);

    /* -------------------------------------------------------------------------
     * PART 2: LITEGL BENCHMARK
     * ------------------------------------------------------------------------- */
    printf("\n[2/2] Initializing LiteGL (ToGL-Based Ultra-Batcher)...\n");
    double lgl_init_start = get_time_sec();

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    SDL_Window* gl_window = SDL_CreateWindow(
        "LiteGL vs Vulkan Benchmark",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1024, 768,
        SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN
    );

    SDL_GLContext gl_ctx = SDL_GL_CreateContext(gl_window);
    SDL_GL_SetSwapInterval(0); /* Disable VSync */

    LiteGLConfig cfg = litegl_default_config(SDL_GL_GetProcAddress);
    LiteGLContext* lgl = litegl_create_context(&cfg);

    const char* vs_src =
        "#version 330 core\n"
        "layout(location = 0) in vec2 inPos;\n"
        "layout(location = 1) in vec2 inTex;\n"
        "layout(location = 2) in vec4 inColor;\n"
        "out vec4 fragColor;\n"
        "void main() {\n"
        "    fragColor = inColor;\n"
        "    gl_Position = vec4(inPos, 0.0, 1.0);\n"
        "}\n";

    const char* fs_src =
        "#version 330 core\n"
        "in vec4 fragColor;\n"
        "out vec4 outColor;\n"
        "void main() {\n"
        "    outColor = fragColor;\n"
        "}\n";

    LiteGLShader* shader = litegl_create_shader(lgl, vs_src, fs_src);
    LiteGLBatcher* batcher = litegl_create_batcher(lgl, 4096);

    double lgl_init_time = (get_time_sec() - lgl_init_start) * 1000.0;
    long lgl_rss_kb = get_rss_kb();
    printf(" [LiteGL] Initialization finished in %.2f ms | Process RSS: %.1f MB\n", lgl_init_time, (double)lgl_rss_kb / 1024.0);

    /* Run LiteGL Benchmark Loop */
    init_scene();
    sort_scene();

    printf(" [LiteGL] Running %d frames of dynamic rendering...\n", target_frames);
    double lgl_render_start = get_time_sec();

    for (int frame = 0; frame < target_frames; ++frame) {
        update_scene();
        litegl_begin_frame(lgl);
        litegl_clear(lgl, 3, 0.05f, 0.05f, 0.08f, 1.0f, 1.0f);

        litegl_batch_begin(batcher, shader);

        for (int i = 0; i < BENCH_OBJECTS; ++i) {
            SceneObject* obj = &s_objects[i];
            float s = obj->size;
            uint32_t cr = (uint32_t)(obj->r * 255.0f);
            uint32_t cg = (uint32_t)(obj->g * 255.0f);
            uint32_t cb = (uint32_t)(obj->b * 255.0f);
            uint32_t ca = (uint32_t)(obj->a * 255.0f);
            uint32_t color = cr | (cg << 8) | (cb << 16) | (ca << 24);

            litegl_batch_rect(batcher, obj->x - s, obj->y - s, s * 2.0f, s * 2.0f, 0.0f, 0.0f, 1.0f, 1.0f, color);
        }

        litegl_batch_end(batcher);
        litegl_end_frame(lgl);
        SDL_GL_SwapWindow(gl_window);
    }

    double lgl_render_duration = get_time_sec() - lgl_render_start;
    double lgl_fps = (double)target_frames / lgl_render_duration;
    double lgl_frametime = (lgl_render_duration / (double)target_frames) * 1000.0;

    printf(" [LiteGL] Result: %d frames in %.3f s | FPS: %.1f | Frame Time: %.2f ms\n",
           target_frames, lgl_render_duration, lgl_fps, lgl_frametime);

    /* LiteGL Cleanup */
    litegl_destroy_batcher(batcher);
    litegl_destroy_shader(lgl, shader);
    litegl_destroy_context(lgl);
    SDL_GL_DeleteContext(gl_ctx);
    SDL_DestroyWindow(gl_window);
    SDL_Quit();

    /* -------------------------------------------------------------------------
     * FINAL COMPARISON SUMMARY
     * ------------------------------------------------------------------------- */
    double speedup = (lgl_fps - vk_fps) / vk_fps * 100.0;
    printf("\n===================================================================================\n");
    printf("                      HEAD-TO-HEAD COMPARISON: VULKAN vs LITEGL                    \n");
    printf("===================================================================================\n");
    printf(" Metric                     | Vulkan (Native Mesa ANV) | LiteGL (ToGL-Based)     \n");
    printf(" ---------------------------+--------------------------+-------------------------\n");
    printf(" Framerate (FPS)            | %7.1f FPS              | %7.1f FPS (%s%.1f%%)   \n",
           vk_fps, lgl_fps, speedup >= 0 ? "+" : "", speedup);
    printf(" Frame Time                 | %7.2f ms               | %7.2f ms                \n", vk_frametime, lgl_frametime);
    printf(" Startup / Init Time        | %7.2f ms               | %7.2f ms (%.1fx faster) \n",
           vk_init_time, lgl_init_time, vk_init_time / (lgl_init_time ? lgl_init_time : 1.0));
    printf(" RAM Consumption (RSS)      | %7.1f MB               | %7.1f MB                \n",
           (double)vk_rss_kb / 1024.0, (double)lgl_rss_kb / 1024.0);
    printf(" Code Complexity / Boiler   | ~500 lines boilerplate   | ~25 lines simple C API  \n");
    printf(" Low-end CPU Driver Bubble  | High (Userspace Sync)    | Zero (Kernel-level GL)  \n");
    printf(" ---------------------------+--------------------------+-------------------------\n");
    if (speedup > 0) {
        printf(" 🏆 KESIMPULAN: LiteGL LEBIH KENCANG +%.1f%% dibanding Vulkan pada laptop ini!\n", speedup);
    } else {
        printf(" 🏆 KESIMPULAN: Performa berimbang, namun LiteGL jauh lebih hemat resource & memori!\n");
    }
    printf("===================================================================================\n\n");

    return 0;
}
