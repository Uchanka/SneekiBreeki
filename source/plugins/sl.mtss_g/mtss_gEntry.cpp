/* Copyright (c) 2020-2023 MooreThreads Coporation. All rights reserved. */

#include <dxgi1_6.h>
#include <future>
#include <assert.h>

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
#include "source/plugins/sl.imgui/imgui.h"
#include "external/nvapi/nvapi.h"
#include "external/json/include/nlohmann/json.hpp"
#include "_artifacts/gitVersion.h"

#include <d3d11.h>

using json = nlohmann::json;

namespace sl
{

namespace tmpl
{


struct UIStats
{
	std::mutex mtx{};
	std::string mode{};
	std::string viewport{};
	std::string runtime{};
	std::string vram{};
};

struct MTSSGContext
{
    SL_PLUGIN_CONTEXT_CREATE_DESTROY(MTSSGContext);

    // Called when plugin is loaded, do any custom constructor initialization here
    void onCreateContext() {};

    // Called when plugin is unloaded, destroy any objects on heap here
    void onDestroyContext() {};

    UIStats uiStats{};

    Constants* commonConsts{};
    // Our tagged inputs
    CommonResource mvec{};
    CommonResource depth{};
    CommonResource hudLessColor{};
    sl::chi::Resource prevMvec{};
    sl::chi::Resource prevDepth{};
    sl::chi::Resource prevHudLessColor{};

    // Compute API
    RenderAPI platform = RenderAPI::eD3D11;
    chi::ICompute* pCompute{};
    chi::ICommandListContext* pCmdList{};
    chi::CommandQueue cmdCopyQueue{};

    uint32_t swapChainWidth{};
    uint32_t swapChainHeight{};

    sl::chi::Resource appSurface{};
    sl::chi::Resource generateFrame{};
    sl::chi::Resource referFrame{};

    uint64_t frameId = 1;

    MTSSGOptions options;
    MTSSGState   state;
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

    return Result::eOk;
}

//! Get settings for our plugin (optional and depending on if we need to provide any settings back to the host)
Result slGetSettings(const void* cdata, void* sdata)
{
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
    SL_PLUGIN_COMMON_STARTUP();

    auto& ctx = (*tmpl::getContext());
    ctx.state.minWidthOrHeight = 1024;

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

    // ImGUI Plugin not support in out card, it require DX12
    imgui::ImGUI* ui{};
    param::getPointerParam(parameters, param::imgui::kInterface, &ui);
    if (ui)
    {
        // Runs async from the present thread where UI is rendered just before frame is presented
        auto renderUI = [&ctx](imgui::ImGUI* ui, bool finalFrame)->void
        {
            imgui::Float4 greenColor{ 0,1,0,1 };
            imgui::Float4 highlightColor{ 153.0f / 255.0f, 217.0f / 255.0f, 234.0f / 255.0f,1 };

            auto v = api::getContext()->pluginVersion;
            std::scoped_lock lock(ctx.uiStats.mtx);
            uint32_t lastFrame, frame;
            if (api::getContext()->parameters->get(sl::param::dlss::kCurrentFrame, &lastFrame))
            {
                ctx.pCompute->getFinishedFrameIndex(frame);
                if (lastFrame < frame)
                {
                    ctx.uiStats.mode = "Mode: Off";
                    ctx.uiStats.viewport = ctx.uiStats.runtime = {};
                }
                if (ui->collapsingHeader(extra::format("sl.mtss-g v{}", (v.toStr() + "." + GIT_LAST_COMMIT_SHORT)).c_str(), imgui::kTreeNodeFlagDefaultOpen))
                {
                    ui->text(ctx.uiStats.mode.c_str());
                }
            }
        };
        ui->registerRenderCallbacks(renderUI, nullptr);
    }

    return true;
}

void slOnPluginShutdown()
{
    auto& ctx = (*tmpl::getContext());

    ctx.pCompute->destroyResource(ctx.generateFrame);
    ctx.pCompute->destroyResource(ctx.referFrame);
    ctx.pCompute->destroyResource(ctx.prevDepth);
    ctx.pCompute->destroyResource(ctx.prevHudLessColor);
    ctx.pCompute->destroyResource(ctx.prevMvec);
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
    ctx.swapChainWidth  = pDesc->BufferDesc.Width;
    ctx.swapChainHeight = pDesc->BufferDesc.Height;

    pDesc->BufferCount = 2;

	chi::ResourceDescription desc;
	desc.width = pDesc->BufferDesc.Width;
	desc.height = pDesc->BufferDesc.Height;
	desc.nativeFormat = pDesc->BufferDesc.Format;

	ctx.pCompute->createTexture2D(desc, ctx.generateFrame, "generate frame");
	ctx.pCompute->createTexture2D(desc, ctx.referFrame, "refer frame");
    ctx.pCompute->createTexture2D(desc, ctx.prevDepth, "previous depth");
    ctx.pCompute->createTexture2D(desc, ctx.prevHudLessColor, "previous hudless color");
    ctx.pCompute->createTexture2D(desc, ctx.prevMvec, "previous mvec");
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

    if (ctx.options.mode == MTSSGMode::eOff)
    {
        SL_LOG_INFO("MTSS-G Mode is Off, present return directly.");
        Skip = false;
    }
    else
    {
        Skip = true;
        if (ctx.appSurface == nullptr)
        {
            ctx.pCompute->getSwapChainBuffer(swapChain, 0, ctx.appSurface);
        }

        sl::Result ret = getTaggedResource(kBufferTypeHUDLessColor, ctx.hudLessColor, 0);
        assert(ret == sl::Result::eOk);
        ret = getTaggedResource(kBufferTypeDepth, ctx.depth, 0);
        assert(ret == sl::Result::eOk);
        ret = getTaggedResource(kBufferTypeMotionVectors, ctx.mvec, 0);
        assert(ret == sl::Result::eOk);

        // First frame skip generate, just copy and present
        if (ctx.frameId == 1)
        {
            auto status = ctx.pCompute->copyResource(ctx.pCmdList->getCmdList(), ctx.referFrame, ctx.appSurface);
            assert(status == sl::chi::ComputeStatus::eOk);

            swapChain->Present(SyncInterval, Flags);
            ctx.state.numFramesActuallyPresented++;
        }
        else
        {
            // Not first frame, use current surface and refer frame to generate frame
            common::EventData eventData;
            eventData.id = 0;
            eventData.frame = ctx.frameId;
            auto getDataResult = common::getConsts(eventData, &ctx.commonConsts);
            assert(getDataResult == common::GetDataResult::eFound);


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
            box.right = ctx.swapChainWidth / 2;
            box.top = 0;
            box.bottom = ctx.swapChainHeight;
            box.front = 0;
            box.back = 1;
            reinterpret_cast<ID3D11DeviceContext*>(ctx.pCmdList->getCmdList())->CopySubresourceRegion(
                static_cast<ID3D11Resource*>(ctx.generateFrame->native), 0, 0, 0, 0,
                static_cast<ID3D11Resource*>(ctx.referFrame->native), 0, &box);

            box.left = ctx.swapChainWidth / 2;
            box.right = ctx.swapChainWidth;
            reinterpret_cast<ID3D11DeviceContext*>(ctx.pCmdList->getCmdList())->CopySubresourceRegion(
                static_cast<ID3D11Resource*>(ctx.generateFrame->native), 0, ctx.swapChainWidth / 2, 0, 0,
                static_cast<ID3D11Resource*>(ctx.appSurface->native), 0, &box);

            // Copy current surface to refer frame
            auto status = ctx.pCompute->copyResource(ctx.pCmdList->getCmdList(), ctx.referFrame, ctx.appSurface);
            assert(status == sl::chi::ComputeStatus::eOk);

            // Copy generate frame to surface present
            status = ctx.pCompute->copyResource(ctx.pCmdList->getCmdList(), ctx.appSurface, ctx.generateFrame);
            assert(status == sl::chi::ComputeStatus::eOk);
            swapChain->Present(SyncInterval, Flags);
            ctx.state.numFramesActuallyPresented++;
            ctx.state.status = MTSSGStatus::eOk;

            // Copy refer frame to surface present
            status = ctx.pCompute->copyResource(ctx.pCmdList->getCmdList(), ctx.appSurface, ctx.referFrame);
            assert(status == sl::chi::ComputeStatus::eOk);
            swapChain->Present(SyncInterval, Flags);
            ctx.state.numFramesActuallyPresented++;
        }

        auto status = ctx.pCompute->copyResource(ctx.pCmdList->getCmdList(), ctx.prevDepth, ctx.depth);
        assert(status == sl::chi::ComputeStatus::eOk);
        status = ctx.pCompute->copyResource(ctx.pCmdList->getCmdList(), ctx.prevMvec, ctx.mvec);
        assert(status == sl::chi::ComputeStatus::eOk);
        status = ctx.pCompute->copyResource(ctx.pCmdList->getCmdList(), ctx.prevHudLessColor, ctx.hudLessColor);
        assert(status == sl::chi::ComputeStatus::eOk);
    }

    ctx.frameId++;
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
        info.minDriver = sl::Version(0, 0, 0);
        // SL does not work on Win7, only Win10+
        info.minOS = sl::Version(10, 0, 0);
        // Specify 0 if our plugin runs on any adapter otherwise specify enum value `NV_GPU_ARCHITECTURE_*` from NVAPI
        info.minGPUArchitecture = 0;
        info.SHA = GIT_LAST_COMMIT_SHORT;
        updateCommonEmbeddedJSONConfig(&config, info);
    }
}

sl::Result slMTSSGGetState(const sl::ViewportHandle& viewport, sl::MTSSGState& state, const sl::MTSSGOptions* options)
{
    auto& ctx = (*tmpl::getContext());

    state = ctx.state;
    ctx.state.numFramesActuallyPresented = 0;

    return sl::Result::eOk;
}

sl::Result slMTSSGSetOptions(const sl::ViewportHandle& viewport, const sl::MTSSGOptions& options)
{
    auto& ctx = (*tmpl::getContext());

    ctx.options = options;

    SL_LOG_INFO("MTSS-G Option Mode:               %s ", ctx.options.mode == sl::MTSSGMode::eOn ? "On" : "Off");
    SL_LOG_INFO("MTSS-G Option NumBackBuffers:     %d ", ctx.options.numBackBuffers);
    SL_LOG_INFO("MTSS-G Option FrameBuffer Witdh:  %d ", ctx.options.colorWidth);
    SL_LOG_INFO("MTSS-G Option FrameBuffer Height: %d ", ctx.options.colorHeight);
    SL_LOG_INFO("MTSS-G Option FrameBuffer Format: %d ", ctx.options.colorBufferFormat);
    SL_LOG_INFO("MTSS-G Option DepthBuffer Format: %d ", ctx.options.depthBufferFormat);
    SL_LOG_INFO("MTSS-G Option numFramesToGenerate:%d ", ctx.options.numFramesToGenerate);
    SL_LOG_INFO("MTSS-G Option Mvec Depth Witdh:   %d ", ctx.options.mvecDepthWidth);
    SL_LOG_INFO("MTSS-G Option Mvec Depth Height:  %d ", ctx.options.mvecDepthHeight);

    ctx.uiStats.mode = ctx.options.mode == sl::MTSSGMode::eOn ? "MTSS-G On" : "MTSS-G Off";

    return sl::Result::eOk;
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

    SL_EXPORT_FUNCTION(slMTSSGGetState);
    SL_EXPORT_FUNCTION(slMTSSGSetOptions);

    return nullptr;
}

}
