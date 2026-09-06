#define SDL_MAIN_USE_CALLBACKS 1

#include "webgpu_context.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

static SDL_Window *window = NULL;

#if defined(__ANDROID__) || defined(SDL_PLATFORM_ANDROID)
static const int WIN_WIDTH = 1280;
static const int WIN_HEIGHT = 720;
#else
static const int WIN_WIDTH = 300;
static const int WIN_HEIGHT = 300;
#endif

SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[])
{
#ifndef __EMSCRIPTEN__
    if (!SDL_SetHint(SDL_HINT_MAIN_CALLBACK_RATE, "60"))
    {
        SDL_Log("Failed to set a frame rate: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
#endif
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "1");

    if (!SDL_Init(SDL_INIT_VIDEO))
        return SDL_APP_FAILURE;

    window = SDL_CreateWindow("WebGPU Context Setup", WIN_WIDTH, WIN_HEIGHT,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window)
        return SDL_APP_FAILURE;

    if (!InitWebGPUContext(&g_gpu, window))
    {
        return SDL_APP_FAILURE;
    }

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event)
{
    if (event->type == SDL_EVENT_QUIT)
    {
        return SDL_APP_SUCCESS;
    }

    if (event->type == SDL_EVENT_WINDOW_RESIZED ||
        event->type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)
    {
        g_gpu.need_reconfigure = true;
    }

    // Capture Android native surface lifecycle
    if (event->type == SDL_EVENT_WINDOW_DISPLAY_CHANGED ||
        event->type == SDL_EVENT_DID_ENTER_FOREGROUND)
    {
        g_gpu.need_recreate_surface = true;
    }

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate)
{
    if (g_gpu.instance)
    {
        wgpuInstanceProcessEvents(g_gpu.instance);
    }

    if (!g_gpu.device)
    {
        return SDL_APP_CONTINUE;
    }

    ReconfigureSurfaceIfNeeded(&g_gpu);

    WGPUSurfaceTexture surfaceTexture = { 0 };
    wgpuSurfaceGetCurrentTexture(g_gpu.surface, &surfaceTexture);

#if defined(WGPUSurfaceGetCurrentTextureStatus_Success) && defined(WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal)
#define SURFACE_STATUS_SUCCESS(s) ((s) == WGPUSurfaceGetCurrentTextureStatus_Success || (s) == WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal)
#elif defined(WGPUSurfaceGetCurrentTextureStatus_SuccessStatus)
#define SURFACE_STATUS_SUCCESS(s) ((s) == WGPUSurfaceGetCurrentTextureStatus_SuccessStatus)
#else
#define SURFACE_STATUS_SUCCESS(s) ((s) == 0 || (s) == 1)
#endif

    if (!SURFACE_STATUS_SUCCESS(surfaceTexture.status))
    {
        static bool status_logged = false;
        if (!status_logged)
        {
            LogApp(">>> wgpuSurfaceGetCurrentTexture failed with status: %d",
                (int)surfaceTexture.status);
            status_logged = true;
        }
        return SDL_APP_CONTINUE;
    }

    WGPUTextureView view = wgpuTextureCreateView(surfaceTexture.texture, NULL);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(g_gpu.device, NULL);

    WGPURenderPassColorAttachment colorAttachment = {
        .nextInChain = NULL,
        .view = view,
        .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
        .resolveTarget = NULL,
        .loadOp = WGPULoadOp_Clear,
        .storeOp = WGPUStoreOp_Store,
        .clearValue = { 0.118, 0.220, 0.170, 1.0 }
    };

    WGPURenderPassDescriptor renderPassDesc = {
        .colorAttachmentCount = 1,
        .colorAttachments = &colorAttachment
    };

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &renderPassDesc);
    wgpuRenderPassEncoderEnd(pass);

    WGPUCommandBuffer commandBuffer = wgpuCommandEncoderFinish(encoder, NULL);
    wgpuQueueSubmit(g_gpu.queue, 1, &commandBuffer);

#ifndef __EMSCRIPTEN__
    wgpuSurfacePresent(g_gpu.surface);
#endif

    if (commandBuffer)
        wgpuCommandBufferRelease(commandBuffer);
    if (pass)
        wgpuRenderPassEncoderRelease(pass);
    if (encoder)
        wgpuCommandEncoderRelease(encoder);
    if (view)
        wgpuTextureViewRelease(view);
    if (surfaceTexture.texture)
        wgpuTextureRelease(surfaceTexture.texture);

#undef SURFACE_STATUS_SUCCESS

    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result)
{
    DestroyWebGPUContext(&g_gpu);
    if (window)
        SDL_DestroyWindow(window);
    SDL_Quit();
}
