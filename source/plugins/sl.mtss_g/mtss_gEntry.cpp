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
//#include "_artifacts/shaders/mtss_fg_pushing_cs.h"
//#include "_artifacts/shaders/mtss_fg_pulling_cs.h"
#include "_artifacts/shaders/mtss_fg_clearing_cs.h"
#include "_artifacts/shaders/mtss_fg_reprojection_cs.h"
#include "_artifacts/shaders/mtss_fg_resolution_cs.h"

#include <d3d11.h>

using json = nlohmann::json;

namespace sl
{

namespace mtssg
{

#define MTSSFG_DPF 0

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
    sl::chi::Resource reprojectedTip{};
    sl::chi::Resource reprojectedTop{};
    sl::chi::Resource prevDepth{};
    sl::chi::Resource prevHudLessColor{};

    // Compute API
    RenderAPI platform = RenderAPI::eD3D11;
    chi::ICompute* pCompute{};
    chi::ICommandListContext* pCmdList{};
    chi::CommandQueue cmdCopyQueue{};

    sl::chi::Kernel clearKernel;
    sl::chi::Kernel reprojectionKernel;
    sl::chi::Kernel resolutionKernel;

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
            "class": "IDXGIFactory",
            "target" : "CreateSwapChainForHwnd",
            "replacement" : "slHookCreateSwapChainForHwnd",
            "base" : "before"
        },
        {
            "class": "IDXGIFactory",
            "target" : "CreateSwapChainForCoreWindow",
            "replacement" : "slHookCreateSwapChainForCoreWindow",
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
            "target" : "Present1",
            "replacement" : "slHookPresent1",
            "base" : "before"
        },
        {
            "class": "IDXGISwapChain",
            "target" : "ResizeBuffers",
            "replacement" : "slHookResizeBuffersPre",
            "base" : "before"
        },
        {
            "class": "IDXGISwapChain",
            "target" : "ResizeBuffers",
            "replacement" : "slHookResizeBuffersPost",
            "base" : "after"
        },
        {
            "class": "IDXGISwapChain",
            "target" : "SetFullscreenState",
            "replacement" : "slHookSetFullscreenStatePre",
            "base" : "before"
        },
        {
            "class": "IDXGISwapChain",
            "target" : "SetFullscreenState",
            "replacement" : "slHookSetFullscreenStatePost",
            "base" : "after"
        }
    ]
}
)json";

//! Define our plugin, make sure to update version numbers in versions.h
SL_PLUGIN_DEFINE("sl.mtss_g", Version(VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH), Version(0, 0, 1), JSON, updateEmbeddedJSON, mtssg, MTSSGContext)

//! Set constants for our plugin (if any, this is optional and should be thread safe)
Result slSetConstants(const void* data, uint32_t frameIndex, uint32_t id)
{
    auto& ctx = (*mtssg::getContext());

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

    auto& ctx = (*mtssg::getContext());
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

    CHI_CHECK_RF(ctx.pCompute->createKernel((void*)mtss_fg_clearing_cs, mtss_fg_clearing_cs_len, "mtss_fg_clearing.cs", "main", ctx.clearKernel));
    CHI_CHECK_RF(ctx.pCompute->createKernel((void*)mtss_fg_reprojection_cs, mtss_fg_reprojection_cs_len, "mtss_fg_reprojection.cs", "main", ctx.reprojectionKernel));
    CHI_CHECK_RF(ctx.pCompute->createKernel((void*)mtss_fg_resolution_cs, mtss_fg_resolution_cs_len, "mtss_fg_resolution.cs", "main", ctx.resolutionKernel));

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
    auto& ctx = (*mtssg::getContext());

    ctx.pCompute->destroyResource(ctx.generateFrame);
    ctx.pCompute->destroyResource(ctx.referFrame);
    ctx.pCompute->destroyResource(ctx.prevDepth);
    ctx.pCompute->destroyResource(ctx.prevHudLessColor);
    ctx.pCompute->destroyResource(ctx.reprojectedTip);
    ctx.pCompute->destroyResource(ctx.reprojectedTop);
    ctx.pCompute->destroyResource(ctx.appSurface);

    CHI_VALIDATE(ctx.pCompute->destroyKernel(ctx.clearKernel));
    CHI_VALIDATE(ctx.pCompute->destroyKernel(ctx.reprojectionKernel));
    CHI_VALIDATE(ctx.pCompute->destroyKernel(ctx.resolutionKernel));

    ctx.pCompute->destroyCommandListContext(ctx.pCmdList);
    ctx.pCompute->destroyCommandQueue(ctx.cmdCopyQueue);

    plugin::onShutdown(api::getContext());
}

HRESULT slHookCreateSwapChain(IDXGIFactory* pFactory, IUnknown* pDevice, DXGI_SWAP_CHAIN_DESC* pDesc, IDXGISwapChain** ppSwapChain, bool& Skip)
{
    SL_LOG_INFO("CreateSwapChain Width: %u, Height: %u, Buffer Count: %u", pDesc->BufferDesc.Width, pDesc->BufferDesc.Height, pDesc->BufferCount);

    HRESULT result = S_OK;

    auto& ctx = (*mtssg::getContext());
    ctx.swapChainWidth  = pDesc->BufferDesc.Width;
    ctx.swapChainHeight = pDesc->BufferDesc.Height;

    pDesc->BufferCount = 2;

	chi::ResourceDescription desc;
	desc.width = pDesc->BufferDesc.Width;
	desc.height = pDesc->BufferDesc.Height;
	desc.nativeFormat = pDesc->BufferDesc.Format;
	ctx.pCompute->createTexture2D(desc, ctx.generateFrame, "generate frame");

    desc.nativeFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
    ctx.pCompute->createTexture2D(desc, ctx.reprojectedTip, "reprojectedTip");
    ctx.pCompute->createTexture2D(desc, ctx.reprojectedTop, "reprojectedTop");
    return result;
}


HRESULT slHookCreateSwapChainForHwnd(IDXGIFactory2 * pFactory, IUnknown * pDevice, HWND hWnd, const DXGI_SWAP_CHAIN_DESC1 * pDesc, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC * pFulScreenDesc, IDXGIOutput * pRestrictToOutput, IDXGISwapChain1 * *ppSwapChain, bool& Skip)
{
    SL_LOG_INFO("slHookCreateSwapChainForHwnd");

    HRESULT result = S_OK;

    return result;
}

HRESULT slHookCreateSwapChainForCoreWindow(IDXGIFactory2 * pFactory, IUnknown * pDevice, IUnknown * pWindow, const DXGI_SWAP_CHAIN_DESC1 * pDesc, IDXGIOutput * pRestrictToOutput, IDXGISwapChain1 * *ppSwapChain, bool& Skip)
{
    SL_LOG_INFO("slHookCreateSwapChainForCoreWindow");

    HRESULT result = S_OK;

    return result;
}

HRESULT slHookPresent(IDXGISwapChain* swapChain, UINT SyncInterval, UINT Flags, bool& Skip)
{
    auto& ctx = (*mtssg::getContext());

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
        if (ctx.referFrame == nullptr)
        {
            auto status = ctx.pCompute->cloneResource(ctx.appSurface, ctx.referFrame, "refer frame");
            assert(status == sl::chi::ComputeStatus::eOk);

            status = ctx.pCompute->copyResource(ctx.pCmdList->getCmdList(), ctx.referFrame, ctx.appSurface);
            assert(status == sl::chi::ComputeStatus::eOk);

            ctx.pCompute->cloneResource(ctx.depth, ctx.prevDepth, "prev depth");
            assert(status == sl::chi::ComputeStatus::eOk);
            ctx.pCompute->cloneResource(ctx.hudLessColor, ctx.prevHudLessColor, "prev hudless color");
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

#if MTSSFG_DPF
            SL_LOG_INFO("Frame %llu jitterOffset: %f, %f", ctx.frameId, ctx.commonConsts->jitterOffset.x, ctx.commonConsts->jitterOffset.y);

            SL_LOG_INFO("ClipToPrevClip Row[0] %f, %f, %f, %f", ctx.commonConsts->clipToPrevClip.row[0].x, ctx.commonConsts->clipToPrevClip.row[0].y, ctx.commonConsts->clipToPrevClip.row[0].z, ctx.commonConsts->clipToPrevClip.row[0].w);
            SL_LOG_INFO("ClipToPrevClip Row[1] %f, %f, %f, %f", ctx.commonConsts->clipToPrevClip.row[1].x, ctx.commonConsts->clipToPrevClip.row[1].y, ctx.commonConsts->clipToPrevClip.row[1].z, ctx.commonConsts->clipToPrevClip.row[1].w);
            SL_LOG_INFO("ClipToPrevClip Row[2] %f, %f, %f, %f", ctx.commonConsts->clipToPrevClip.row[2].x, ctx.commonConsts->clipToPrevClip.row[2].y, ctx.commonConsts->clipToPrevClip.row[2].z, ctx.commonConsts->clipToPrevClip.row[2].w);
            SL_LOG_INFO("ClipToPrevClip Row[3] %f, %f, %f, %f", ctx.commonConsts->clipToPrevClip.row[3].x, ctx.commonConsts->clipToPrevClip.row[3].y, ctx.commonConsts->clipToPrevClip.row[3].z, ctx.commonConsts->clipToPrevClip.row[3].w);

            SL_LOG_INFO("PrevClipToClip Row[0] %f, %f, %f, %f", ctx.commonConsts->prevClipToClip.row[0].x, ctx.commonConsts->prevClipToClip.row[0].y, ctx.commonConsts->prevClipToClip.row[0].z, ctx.commonConsts->prevClipToClip.row[0].w);
            SL_LOG_INFO("PrevClipToClip Row[1] %f, %f, %f, %f", ctx.commonConsts->prevClipToClip.row[1].x, ctx.commonConsts->prevClipToClip.row[1].y, ctx.commonConsts->prevClipToClip.row[1].z, ctx.commonConsts->prevClipToClip.row[1].w);
            SL_LOG_INFO("PrevClipToClip Row[2] %f, %f, %f, %f", ctx.commonConsts->prevClipToClip.row[2].x, ctx.commonConsts->prevClipToClip.row[2].y, ctx.commonConsts->prevClipToClip.row[2].z, ctx.commonConsts->prevClipToClip.row[2].w);
            SL_LOG_INFO("PrevClipToClip Row[3] %f, %f, %f, %f", ctx.commonConsts->prevClipToClip.row[3].x, ctx.commonConsts->prevClipToClip.row[3].y, ctx.commonConsts->prevClipToClip.row[3].z, ctx.commonConsts->prevClipToClip.row[3].w);
#endif

            sl::uint2 dimensions = sl::uint2(ctx.swapChainWidth, ctx.swapChainHeight);
            sl::float2 smoothing = sl::float2(1.0f, 1.0f);
            sl::float2 viewportSize = sl::float2(static_cast<float>(ctx.swapChainWidth), static_cast<float>(ctx.swapChainHeight));
            sl::float2 viewportInv = sl::float2(1.0f / viewportSize.x, 1.0f / viewportSize.y);

            uint32_t grid[] = { (ctx.swapChainWidth + 8 - 1) / 8, (ctx.swapChainHeight + 8 - 1) / 8, 1 };
            //MTFKClearing
            {
                struct ClearingConstParamStruct
                {
                    sl::uint2 dimensions;
                    sl::float2 smoothing;
                    sl::float2 viewportSize;
                    sl::float2 viewportInv;
                };
                ClearingConstParamStruct lb;

                lb.dimensions = dimensions;
                lb.smoothing = smoothing;
                lb.viewportSize = viewportSize;
                lb.viewportInv = viewportInv;

                CHI_VALIDATE(ctx.pCompute->bindSharedState(ctx.pCmdList->getCmdList()));

                CHI_VALIDATE(ctx.pCompute->bindKernel(ctx.clearKernel));
                CHI_VALIDATE(ctx.pCompute->bindRWTexture(0, 0, ctx.reprojectedTip));
                CHI_VALIDATE(ctx.pCompute->bindRWTexture(1, 1, ctx.reprojectedTop));
                CHI_VALIDATE(ctx.pCompute->bindConsts(2, 0, &lb, sizeof(lb), 1));

                CHI_VALIDATE(ctx.pCompute->dispatch(grid[0], grid[1], grid[2]));
            }

            //MTFKReprojection
            {
                struct MVecParamStruct
                {
                    sl::float4x4 prevClipToClip;
                    sl::float4x4 clipToPrevClip;

                    sl::uint2 dimensions;
                    sl::float2 smoothing;
                    sl::float2 viewportSize;
                    sl::float2 viewportInv;
                };
                MVecParamStruct cb;
                memcpy(&cb.prevClipToClip, &ctx.commonConsts->prevClipToClip, sizeof(float) * 16);
                memcpy(&cb.clipToPrevClip, &ctx.commonConsts->clipToPrevClip, sizeof(float) * 16);
                cb.dimensions = dimensions;
                cb.smoothing = sl::float2(1.0f, 1.0f);
                cb.viewportSize = viewportSize;
                cb.viewportInv = viewportInv;

                CHI_VALIDATE(ctx.pCompute->bindKernel(ctx.reprojectionKernel));

                CHI_VALIDATE(ctx.pCompute->bindTexture(0, 0, ctx.prevHudLessColor));
                CHI_VALIDATE(ctx.pCompute->bindTexture(1, 1, ctx.hudLessColor));
                CHI_VALIDATE(ctx.pCompute->bindTexture(2, 2, ctx.prevDepth));
                CHI_VALIDATE(ctx.pCompute->bindTexture(3, 3, ctx.depth));
                CHI_VALIDATE(ctx.pCompute->bindTexture(4, 4, ctx.mvec));

                CHI_VALIDATE(ctx.pCompute->bindRWTexture(5, 0, ctx.reprojectedTip));
                CHI_VALIDATE(ctx.pCompute->bindRWTexture(6, 1, ctx.reprojectedTop));

                CHI_VALIDATE(ctx.pCompute->bindConsts(7, 0, &cb, sizeof(cb), 1));
                
                CHI_VALIDATE(ctx.pCompute->bindSampler(8, 0, chi::eSamplerLinearMirror));

                CHI_VALIDATE(ctx.pCompute->dispatch(grid[0], grid[1], grid[2]));

                CHI_VALIDATE(ctx.pCompute->bindRWTexture(0, 0, {}));
                CHI_VALIDATE(ctx.pCompute->bindRWTexture(1, 1, {}));
            }

            //MTFKResolution
            {
                struct ResolutionConstParamStruct
                {
                    sl::uint2 dimensions;
                    sl::float2 smoothing;
                    sl::float2 viewportSize;
                    sl::float2 viewportInv;
                };
                ResolutionConstParamStruct rb;
                rb.dimensions = dimensions;
                rb.smoothing = sl::float2(1.0f, 1.0f);
                rb.viewportSize = viewportSize;
                rb.viewportInv = viewportInv;

                CHI_VALIDATE(ctx.pCompute->bindKernel(ctx.resolutionKernel));

                CHI_VALIDATE(ctx.pCompute->bindTexture(0, 0, ctx.prevHudLessColor));
                CHI_VALIDATE(ctx.pCompute->bindTexture(1, 1, ctx.hudLessColor));
                CHI_VALIDATE(ctx.pCompute->bindTexture(2, 2, ctx.prevDepth));
                CHI_VALIDATE(ctx.pCompute->bindTexture(3, 3, ctx.depth));
                CHI_VALIDATE(ctx.pCompute->bindTexture(4, 4, ctx.reprojectedTip));
                CHI_VALIDATE(ctx.pCompute->bindTexture(5, 5, ctx.reprojectedTop));

                CHI_VALIDATE(ctx.pCompute->bindRWTexture(6, 0, ctx.generateFrame));

                CHI_VALIDATE(ctx.pCompute->bindConsts(7, 0, &rb, sizeof(rb), 1));

                CHI_VALIDATE(ctx.pCompute->dispatch(grid[0], grid[1], grid[2]));
            }

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
        status = ctx.pCompute->copyResource(ctx.pCmdList->getCmdList(), ctx.prevHudLessColor, ctx.hudLessColor);
        assert(status == sl::chi::ComputeStatus::eOk);
    }

    ctx.frameId++;
    return S_OK;
}

HRESULT slHookPresent1(IDXGISwapChain * SwapChain, UINT SyncInterval, UINT PresentFlags, const DXGI_PRESENT_PARAMETERS * pPresentParameters, bool& Skip)
{
    SL_LOG_INFO("slHookPresent1");

    HRESULT result = S_OK;

    return result;
}

HRESULT slHookResizeBuffersPre(IDXGISwapChain* SwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT& SwapChainFlags, bool& Skip)
{
    SL_LOG_INFO("slHookResizeBuffersPre");

    HRESULT result = S_OK;

    return result;
}

HRESULT slHookResizeBuffersPost(IDXGISwapChain* SwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT& SwapChainFlags)
{
    SL_LOG_INFO("slHookResizeBuffersPost");

    HRESULT result = S_OK;

    return result;
}

HRESULT slHookSetFullscreenStatePre(IDXGISwapChain * SwapChain, BOOL pFullscreen, IDXGIOutput * ppTarget, bool& Skip)
{
    SL_LOG_INFO("slHookSetFullscreenStatePre");

    HRESULT result = S_OK;

    return result;
}

HRESULT slHookSetFullscreenStatePost(IDXGISwapChain* SwapChain, BOOL pFullscreen, IDXGIOutput* ppTarget)
{
    SL_LOG_INFO("slHookSetFullscreenStatePost");

    HRESULT result = S_OK;

    return result;
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
    auto& ctx = (*mtssg::getContext());

    state = ctx.state;
    ctx.state.numFramesActuallyPresented = 0;

    return sl::Result::eOk;
}

sl::Result slMTSSGSetOptions(const sl::ViewportHandle& viewport, const sl::MTSSGOptions& options)
{
    auto& ctx = (*mtssg::getContext());

    ctx.options = options;

#if MTSSFG_DPF
    SL_LOG_INFO("MTSS-G Option Mode:               %s ", ctx.options.mode == sl::MTSSGMode::eOn ? "On" : "Off");
    SL_LOG_INFO("MTSS-G Option NumBackBuffers:     %d ", ctx.options.numBackBuffers);
    SL_LOG_INFO("MTSS-G Option FrameBuffer Witdh:  %d ", ctx.options.colorWidth);
    SL_LOG_INFO("MTSS-G Option FrameBuffer Height: %d ", ctx.options.colorHeight);
    SL_LOG_INFO("MTSS-G Option FrameBuffer Format: %d ", ctx.options.colorBufferFormat);
    SL_LOG_INFO("MTSS-G Option DepthBuffer Format: %d ", ctx.options.depthBufferFormat);
    SL_LOG_INFO("MTSS-G Option numFramesToGenerate:%d ", ctx.options.numFramesToGenerate);
    SL_LOG_INFO("MTSS-G Option Mvec Depth Witdh:   %d ", ctx.options.mvecDepthWidth);
    SL_LOG_INFO("MTSS-G Option Mvec Depth Height:  %d ", ctx.options.mvecDepthHeight);
#endif

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
    SL_EXPORT_FUNCTION(slHookCreateSwapChainForHwnd);
    SL_EXPORT_FUNCTION(slHookCreateSwapChainForCoreWindow);
    SL_EXPORT_FUNCTION(slHookPresent);
    SL_EXPORT_FUNCTION(slHookPresent1);
    SL_EXPORT_FUNCTION(slHookResizeBuffersPre);
    SL_EXPORT_FUNCTION(slHookResizeBuffersPost);
    SL_EXPORT_FUNCTION(slHookSetFullscreenStatePre);
    SL_EXPORT_FUNCTION(slHookSetFullscreenStatePost);

    SL_EXPORT_FUNCTION(slMTSSGGetState);
    SL_EXPORT_FUNCTION(slMTSSGSetOptions);

    return nullptr;
}

}
