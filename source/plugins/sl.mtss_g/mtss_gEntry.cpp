/* Copyright (c) 2020-2023 MooreThreads Coporation. All rights reserved. */

#include <dxgi1_6.h>
#include <future>

#include "include/sl.h"
#include "include/sl_consts.h"
#include "include/sl_mtss_g.h"
#include "source/core/sl.api/internal.h"
#include "source/core/sl.log/log.h"
#include "source/core/sl.plugin/plugin.h"
#include "source/core/sl.param/parameters.h"
#include "source/platforms/sl.chi/compute.h"
#include "source/plugins/sl.template/versions.h"
#include "source/plugins/sl.common/commonInterface.h"
#include "external/nvapi/nvapi.h"
#include "external/json/include/nlohmann/json.hpp"
#include "_artifacts/gitVersion.h"

#include <d3d11.h>

using json = nlohmann::json;

//! IMPORTANT: This is our include with our constants and settings (if any)
//!
#include "include/sl_template.h"

namespace sl
{

namespace tmpl
{


struct MTSSGContext
{
    SL_PLUGIN_CONTEXT_CREATE_DESTROY(MTSSGContext);

    // Called when plugin is loaded, do any custom constructor initialization here
    void onCreateContext() {};

    // Called when plugin is unloaded, destroy any objects on heap here
    void onDestroyContext() {};

    // For example, we can use this template to store incoming constants
    // 
    common::ViewportIdFrameData<> constants = { "template" };

    Constants* commonConsts{};

    // Our tagged inputs
    CommonResource mvec{};
    CommonResource depth{};
    CommonResource hudLessColor{};

    // Compute API
    RenderAPI platform = RenderAPI::eD3D11;
    chi::ICompute* pCompute{};

    chi::ICommandListContext* pCmdList{};
    chi::CommandQueue cmdCopyQueue{};

    sl::chi::Resource appSurface{};
    sl::chi::Resource generateFrame{};
    sl::chi::Resource referFrame{};

    uint64_t frameId = 1;
};
}

void updateEmbeddedJSON(json& config);

static const char* JSON = R"json(
{
    "id" : 10000,
    "priority" : 1000,
    "name" : "sl.mtss_g",
    "namespace" : "mtss_g",
    "required_plugins" : ["sl.common"],
    "exculusive_hooks" : ["IDXGISwapChain_GetCurrentBackBufferIndex", "IDXGISwapChain_GetBuffer"],
    "rhi" : ["d3d11"],
    
    "hooks" :
    [
        {
            "class": "IDXGIFactory",
            "target" : "CreateSwapChain",
            "replacement" : "slHookCreateSwapChain",
            "base" : "before"
        },
        {
            "class": "IDXGISwapChain",
            "target" : "Present",
            "replacement" : "slHookPresent",
            "base" : "before"
        },
        {
            "class": "IDXGISwapChain",
            "target" : "GetBuffer",
            "replacement" : "slHookGetBuffer",
            "base" : "before"
        },
        {
            "class": "IDXGISwapChain",
            "target" : "GetCurrentBackBufferIndex",
            "replacement" : "slHookGetCurrentBackBufferIndex",
            "base" : "before"
        }
    ]
}
)json";

//! Define our plugin, make sure to update version numbers in versions.h
SL_PLUGIN_DEFINE("sl.mtss_g", Version(VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH), Version(0, 0, 1), JSON, updateEmbeddedJSON, tmpl, MTSSGContext)

//! Set constants for our plugin (if any, this is optional and should be thread safe)
Result slSetConstants(const void* data, uint32_t frameIndex, uint32_t id)
{
    auto& ctx = (*tmpl::getContext());

    // For example, we can set out constants like this
    // 
    auto consts = (const TemplateConstants*)data;
    ctx.constants.set(frameIndex, id, consts);
    if (consts->mode == TemplateMode::eOff)
    {
        // User disabled our feature
        auto lambda = [/*capture references and data you need*/](void)->void
        {
            // Cleanup logic goes here
        };
        // Schedule delayed destroy (few frames later)
        CHI_VALIDATE(ctx.pCompute->destroy(lambda));
    }
    else
    {
        // User enabled our feature, nothing to do here
        // but rather in 'templateBeginEvaluation' when
        // we have access to the command buffer.
    }
    return Result::eOk;
}

//! Get settings for our plugin (optional and depending on if we need to provide any settings back to the host)
Result slGetSettings(const void* cdata, void* sdata)
{
    // For example, we can set out constants like this
    // 
    // Note that TemplateConstants should be defined in sl_constants.h and provided by the host
    // 
    auto consts = (const TemplateConstants*)cdata;
    auto settings = (TemplateSettings*)sdata;
    return Result::eOk;
}

//! Explicit allocation of resources
Result slAllocateResources(sl::CommandBuffer* cmdBuffer, sl::Feature feature, const sl::ViewportHandle& viewport)
{
    return Result::eOk;
}

//! Explicit de-allocation of resources
Result slFreeResources(sl::Feature feature, const sl::ViewportHandle& viewport)
{
    return Result::eOk;
}

bool slOnPluginStartup(const char* jsonConfig, void* device)
{
    //! Common startup and setup
    //!     
    SL_PLUGIN_COMMON_STARTUP();

    auto& ctx = (*tmpl::getContext());

    auto parameters = api::getContext()->parameters;

    //! Plugin manager gives us the device type and the application id
    //! 
    json& config = *(json*)api::getContext()->loaderConfig;
    uint32_t deviceType{};
    int appId{};
    config.at("appId").get_to(appId);
    config.at("deviceType").get_to(deviceType);

    //! Extra config is always `sl.plugin_name.json` so in our case `sl.template.json`
    //! 
    //! Populated automatically by the SL_PLUGIN_COMMON_STARTUP macro
    //! 
    json& extraConfig = *(json*)api::getContext()->extConfig;
    if (extraConfig.contains("myKey"))
    {
        //! Extract your configuration data and do something with it
    }

    //! Now let's obtain compute interface if we need to dispatch some compute work
    //! 
    ctx.platform = (RenderAPI)deviceType;
    if (!param::getPointerParam(parameters, sl::param::common::kComputeAPI, &ctx.pCompute))
    {
        // Log error
        return false;
    }

    ctx.pCompute->createCommandQueue(chi::CommandQueueType::eCopy, ctx.cmdCopyQueue, "mtss-g copy queue");
    assert(ctx.cmdCopyQueue != nullptr);

    ctx.pCompute->createCommandListContext(ctx.cmdCopyQueue, 1, ctx.pCmdList, "mtss-g ctx");
    assert(ctx.pCmdList != nullptr);

    return true;
}

//! Main exit point - shutting down our plugin
//! 
//! IMPORTANT: Plugins are shutdown in the inverse order based to their priority.
//! sl.common always shutsdown LAST since it has priority 0
//!
void slOnPluginShutdown()
{
    auto& ctx = (*tmpl::getContext());

    ctx.pCompute->destroyResource(ctx.generateFrame);
    ctx.pCompute->destroyResource(ctx.referFrame);
    ctx.pCompute->destroyResource(ctx.appSurface);

    ctx.pCompute->destroyCommandListContext(ctx.pCmdList);
    ctx.pCompute->destroyCommandQueue(ctx.cmdCopyQueue);

    plugin::onShutdown(api::getContext());
}

HRESULT slHookCreateSwapChain(IDXGIFactory* pFactory, IUnknown* pDevice, DXGI_SWAP_CHAIN_DESC* pDesc, IDXGISwapChain** ppSwapChain, bool& Skip)
{
    SL_LOG_INFO("CreateSwapChain Width: %u, Height: %u, Buffer Count: %u", pDesc->BufferDesc.Width, pDesc->BufferDesc.Height, pDesc->BufferCount);

    HRESULT hr = S_OK;

    auto& ctx = (*tmpl::getContext());
    
    pDesc->BufferCount = 2;

	chi::ResourceDescription desc;
	desc.width = pDesc->BufferDesc.Width;
	desc.height = pDesc->BufferDesc.Height;
	desc.nativeFormat = pDesc->BufferDesc.Format;

	ctx.pCompute->createTexture2D(desc, ctx.generateFrame, "generate frame");
	ctx.pCompute->createTexture2D(desc, ctx.referFrame, "refer frame");
    
    return hr;
}

HRESULT slHookGetBuffer(IDXGISwapChain* SwapChain, UINT Buffer, REFIID riid, void** ppSurface, bool& Skip)
{
    SL_LOG_INFO("GetBuffer Index: %u", Buffer);

    return S_OK;
}

UINT slHookGetCurrentBackBufferIndex(IDXGISwapChain* SwapChain, bool& Skip)
{
    return 0;
}

HRESULT slHookPresent(IDXGISwapChain* swapChain, UINT SyncInterval, UINT Flags, bool& Skip)
{
    auto& ctx = (*tmpl::getContext());

    if (ctx.appSurface == nullptr)
    {
        ctx.pCompute->getSwapChainBuffer(swapChain, 0, ctx.appSurface);
    }

    if (ctx.referFrame->native == nullptr)
    {
        ctx.pCompute->copyResource(ctx.pCmdList->getCmdList(), ctx.referFrame, ctx.appSurface);
        swapChain->Present(SyncInterval, Flags);
    }
    else
    {
        sl::Result ret = getTaggedResource(kBufferTypeHUDLessColor, ctx.hudLessColor, 0);
        assert(ret == sl::Result::eOk);
        ret = getTaggedResource(kBufferTypeDepth, ctx.depth, 0);
        assert(ret == sl::Result::eOk);
        ret = getTaggedResource(kBufferTypeMotionVectors, ctx.mvec, 0);
        assert(ret == sl::Result::eOk);

        common::EventData eventData;
        eventData.id = 0;
        eventData.frame = ctx.frameId;
        common::getConsts(eventData, &ctx.commonConsts);

        SL_LOG_INFO("Frame %llu jitterOffset: %f, %f", ctx.frameId, ctx.commonConsts->jitterOffset.x, ctx.commonConsts->jitterOffset.y);

        SL_LOG_INFO("ClipToPrevClip Row[0] %f, %f, %f, %f", ctx.commonConsts->clipToPrevClip.row[0].x, ctx.commonConsts->clipToPrevClip.row[0].y, ctx.commonConsts->clipToPrevClip.row[0].z, ctx.commonConsts->clipToPrevClip.row[0].w);
        SL_LOG_INFO("ClipToPrevClip Row[1] %f, %f, %f, %f", ctx.commonConsts->clipToPrevClip.row[1].x, ctx.commonConsts->clipToPrevClip.row[1].y, ctx.commonConsts->clipToPrevClip.row[1].z, ctx.commonConsts->clipToPrevClip.row[1].w);
        SL_LOG_INFO("ClipToPrevClip Row[2] %f, %f, %f, %f", ctx.commonConsts->clipToPrevClip.row[2].x, ctx.commonConsts->clipToPrevClip.row[2].y, ctx.commonConsts->clipToPrevClip.row[2].z, ctx.commonConsts->clipToPrevClip.row[2].w);
        SL_LOG_INFO("ClipToPrevClip Row[3] %f, %f, %f, %f", ctx.commonConsts->clipToPrevClip.row[3].x, ctx.commonConsts->clipToPrevClip.row[3].y, ctx.commonConsts->clipToPrevClip.row[3].z, ctx.commonConsts->clipToPrevClip.row[3].w);

        SL_LOG_INFO("PrevClipToClip Row[0] %f, %f, %f, %f", ctx.commonConsts->prevClipToClip.row[0].x, ctx.commonConsts->prevClipToClip.row[0].y, ctx.commonConsts->prevClipToClip.row[0].z, ctx.commonConsts->prevClipToClip.row[0].w);
        SL_LOG_INFO("PrevClipToClip Row[1] %f, %f, %f, %f", ctx.commonConsts->prevClipToClip.row[1].x, ctx.commonConsts->prevClipToClip.row[1].y, ctx.commonConsts->prevClipToClip.row[1].z, ctx.commonConsts->prevClipToClip.row[1].w);
        SL_LOG_INFO("PrevClipToClip Row[2] %f, %f, %f, %f", ctx.commonConsts->prevClipToClip.row[2].x, ctx.commonConsts->prevClipToClip.row[2].y, ctx.commonConsts->prevClipToClip.row[2].z, ctx.commonConsts->prevClipToClip.row[2].w);
        SL_LOG_INFO("PrevClipToClip Row[3] %f, %f, %f, %f", ctx.commonConsts->prevClipToClip.row[3].x, ctx.commonConsts->prevClipToClip.row[3].y, ctx.commonConsts->prevClipToClip.row[3].z, ctx.commonConsts->prevClipToClip.row[3].w);

        // Here we copy half left of last refer frame and copy half right of current app surface
        // Aim to simulate frame generate algorithm.
        D3D11_BOX box;
        box.left = 0;
        box.right = 1920 / 2;
        box.top = 0;
        box.bottom = 1080;
        box.front = 0;
        box.back = 1;
        reinterpret_cast<ID3D11DeviceContext*>(ctx.pCmdList->getCmdList())->CopySubresourceRegion(
            static_cast<ID3D11Resource*>(ctx.generateFrame->native), 0, 0, 0, 0, 
            static_cast<ID3D11Resource*>(ctx.referFrame->native), 0, &box);

        box.left = 1920 / 2;
        box.right = 1920;
        reinterpret_cast<ID3D11DeviceContext*>(ctx.pCmdList->getCmdList())->CopySubresourceRegion(
            static_cast<ID3D11Resource*>(ctx.generateFrame->native), 0, 1920 / 2, 0, 0,
            static_cast<ID3D11Resource*>(ctx.appSurface->native), 0, &box);

        // Copy current surface to refer frame
        ctx.pCompute->copyResource(ctx.pCmdList->getCmdList(), ctx.referFrame, ctx.appSurface);

        // Copy generate frame to surface present
        ctx.pCompute->copyResource(ctx.pCmdList->getCmdList(), ctx.appSurface, ctx.generateFrame);
        swapChain->Present(SyncInterval, Flags);

        // Copy refer frame to surface present
        ctx.pCompute->copyResource(ctx.pCmdList->getCmdList(), ctx.appSurface, ctx.referFrame);
        swapChain->Present(SyncInterval, Flags);
    }

    ctx.frameId++;
    Skip = true;
    return S_OK;
}

//! Figure out if we are supported on the current hardware or not
//! 
void updateEmbeddedJSON(json& config)
{
    // Check if plugin is supported or not on this platform and set the flag accordingly
    common::SystemCaps* caps = {};
    param::getPointerParam(api::getContext()->parameters, sl::param::common::kSystemCaps, &caps);
    common::PFunUpdateCommonEmbeddedJSONConfig* updateCommonEmbeddedJSONConfig{};
    param::getPointerParam(api::getContext()->parameters, sl::param::common::kPFunUpdateCommonEmbeddedJSONConfig, &updateCommonEmbeddedJSONConfig);
    if (caps && updateCommonEmbeddedJSONConfig)
    {
        common::PluginInfo info{};
        // Specify minimum driver version we need
        info.minDriver = sl::Version(455, 0, 0);
        // SL does not work on Win7, only Win10+
        info.minOS = sl::Version(10, 0, 0);
        // Specify 0 if our plugin runs on any adapter otherwise specify enum value `NV_GPU_ARCHITECTURE_*` from NVAPI
        info.minGPUArchitecture = 0;
        info.SHA = GIT_LAST_COMMIT_SHORT;
        updateCommonEmbeddedJSONConfig(&config, info);
    }
}

SL_EXPORT void* slGetPluginFunction(const char* functionName)
{
    //! Forward declarations
    bool slOnPluginLoad(sl::param::IParameters * params, const char* loaderJSON, const char** pluginJSON);

    //! Redirect to OTA if any
    SL_EXPORT_OTA;

    //! Core API
    SL_EXPORT_FUNCTION(slOnPluginLoad);
    SL_EXPORT_FUNCTION(slOnPluginShutdown);
    SL_EXPORT_FUNCTION(slOnPluginStartup);
    SL_EXPORT_FUNCTION(slSetConstants);
    SL_EXPORT_FUNCTION(slGetSettings);
    SL_EXPORT_FUNCTION(slAllocateResources);
    SL_EXPORT_FUNCTION(slFreeResources);


    SL_EXPORT_FUNCTION(slHookCreateSwapChain);
    SL_EXPORT_FUNCTION(slHookPresent);
    SL_EXPORT_FUNCTION(slHookGetBuffer);
    SL_EXPORT_FUNCTION(slHookGetCurrentBackBufferIndex);

    return nullptr;
}

}
