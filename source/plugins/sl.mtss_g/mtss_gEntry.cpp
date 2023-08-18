/* Copyright (c) 2020-2023 MooreThreads Coporation. All rights reserved. */

#include <dxgi1_6.h>
#include <assert.h>

#include "include/sl.h"
#include "include/sl_consts.h"
#include "include/sl_mtss_g.h"
#include "source/core/sl.log/log.h"
#include "source/core/sl.plugin/plugin.h"
#include "source/core/sl.param/parameters.h"
#include "source/platforms/sl.chi/compute.h"
#include "source/plugins/sl.template/versions.h"
#include "source/plugins/sl.common/commonInterface.h"
#include "external/json/include/nlohmann/json.hpp"
#include "_artifacts/gitVersion.h"
//#include "_artifacts/shaders/mtss_fg_pushing_cs.h"
//#include "_artifacts/shaders/mtss_fg_pulling_cs.h"
#include "_artifacts/shaders/mtss_fg_clearing_cs.h"
#include "_artifacts/shaders/mtss_fg_reprojection_cs.h"
#include "_artifacts/shaders/mtss_fg_resolution_cs.h"

using json = nlohmann::json;

namespace sl
{

namespace mtssg
{

#define MTSSFG_DPF 0
#define MTSSFG_NOT_TEST() SL_LOG_WARN("This Path Not Test, Maybe Not Work")

struct ClearingConstParamStruct
{
    sl::uint2 dimensions;
    sl::float2 smoothing;
    sl::float2 viewportSize;
    sl::float2 viewportInv;
};

struct MVecParamStruct
{
    sl::float4x4 prevClipToClip;
    sl::float4x4 clipToPrevClip;

    sl::uint2 dimensions;
    sl::float2 smoothing;
    sl::float2 viewportSize;
    sl::float2 viewportInv;
};

struct ResolutionConstParamStruct
{
    sl::uint2 dimensions;
    sl::float2 smoothing;
    sl::float2 viewportSize;
    sl::float2 viewportInv;
};

enum class PresentApi : uint8_t
{
    Present,
    Present1,
};

struct MTSSGContext
{
    SL_PLUGIN_CONTEXT_CREATE_DESTROY(MTSSGContext);

    // Called when plugin is loaded, do any custom constructor initialization here
    void onCreateContext() {};

    // Called when plugin is unloaded, destroy any objects on heap here
    void onDestroyContext() {};

    Constants* commonConsts{};
    // Our tagged inputs
    CommonResource mvec{};
    CommonResource currDepth{};
    CommonResource currHudLessColor{};
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
    DXGI_FORMAT swapChainFormat{};

    sl::chi::Resource appSurface{};
    sl::chi::Resource generateFrame{};
    sl::chi::Resource appSurfaceBackup{};

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
            "target" : "ResizeBuffers1",
            "replacement" : "slHookResizeBuffers1Pre",
            "base" : "before"
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

bool slOnPluginStartup(const char* jsonConfig, void* device)
{
    SL_PLUGIN_COMMON_STARTUP();

    auto& ctx = (*mtssg::getContext());
    ctx.state.minWidthOrHeight = 128;
    ctx.state.status = MTSSGStatus::eOk;

    auto parameters = api::getContext()->parameters;

    json& config = *(json*)api::getContext()->loaderConfig;
    uint32_t deviceType{};
    int appId{};
    config.at("appId").get_to(appId);
    config.at("deviceType").get_to(deviceType);

    ctx.platform = (RenderAPI)deviceType;
    if (ctx.platform != RenderAPI::eD3D11)
    {
        SL_LOG_ERROR("MTSS-FG Only Support D3D11 Device Now!");
        return false;
    }

    if (!param::getPointerParam(parameters, sl::param::common::kComputeAPI, &ctx.pCompute))
    {
        return false;
    }

    ctx.pCompute->createCommandQueue(chi::CommandQueueType::eCopy, ctx.cmdCopyQueue, "mtss-g copy queue");
    assert(ctx.cmdCopyQueue != nullptr);

    ctx.pCompute->createCommandListContext(ctx.cmdCopyQueue, 1, ctx.pCmdList, "mtss-g ctx");
    assert(ctx.pCmdList != nullptr);

    CHI_CHECK_RF(ctx.pCompute->createKernel((void*)mtss_fg_clearing_cs, mtss_fg_clearing_cs_len, "mtss_fg_clearing.cs", "main", ctx.clearKernel));
    CHI_CHECK_RF(ctx.pCompute->createKernel((void*)mtss_fg_reprojection_cs, mtss_fg_reprojection_cs_len, "mtss_fg_reprojection.cs", "main", ctx.reprojectionKernel));
    CHI_CHECK_RF(ctx.pCompute->createKernel((void*)mtss_fg_resolution_cs, mtss_fg_resolution_cs_len, "mtss_fg_resolution.cs", "main", ctx.resolutionKernel));

    sl::CommonResource temp{};
    getTaggedResource(kBufferTypeHUDLessColor, temp, 0);
    getTaggedResource(kBufferTypeDepth, temp, 0);
    getTaggedResource(kBufferTypeMotionVectors, temp, 0);

    return true;
}

sl::chi::ComputeStatus destroyResource(sl::chi::Resource* pResource, uint32_t frameDelay = 0)
{
    auto& ctx = (*mtssg::getContext());

    auto ret = ctx.pCompute->destroyResource(*pResource, frameDelay);
    *pResource = nullptr;

    return ret;
}

sl::chi::ComputeStatus cloneResource(sl::chi::Resource resource, sl::chi::Resource& clone, const char friendlyName[])
{
    auto& ctx = (*mtssg::getContext());

    CHI_VALIDATE(destroyResource(&clone));
    CHI_VALIDATE(ctx.pCompute->cloneResource(resource, clone, friendlyName));

    return sl::chi::ComputeStatus::eOk;
}

sl::chi::ComputeStatus copyResource(sl::chi::Resource& dst, sl::chi::Resource& src)
{
    auto& ctx = (*mtssg::getContext());

    CHI_VALIDATE(ctx.pCompute->copyResource(ctx.pCmdList->getCmdList(), dst, src));

    return sl::chi::ComputeStatus::eOk;
}

void destroyFrameGenerationResource()
{
    auto& ctx = (*mtssg::getContext());

    CHI_VALIDATE(destroyResource(&ctx.appSurfaceBackup));
    CHI_VALIDATE(destroyResource(&ctx.prevDepth));
    CHI_VALIDATE(destroyResource(&ctx.prevHudLessColor));
    CHI_VALIDATE(destroyResource(&ctx.generateFrame));
    CHI_VALIDATE(destroyResource(&ctx.appSurface));
    CHI_VALIDATE(destroyResource(&ctx.reprojectedTip));
    CHI_VALIDATE(destroyResource(&ctx.reprojectedTop));
}

void slOnPluginShutdown()
{
    auto& ctx = (*mtssg::getContext());

    destroyFrameGenerationResource();

    CHI_VALIDATE(ctx.pCompute->destroyKernel(ctx.clearKernel));
    CHI_VALIDATE(ctx.pCompute->destroyKernel(ctx.reprojectionKernel));
    CHI_VALIDATE(ctx.pCompute->destroyKernel(ctx.resolutionKernel));

    ctx.pCompute->destroyCommandListContext(ctx.pCmdList);
    ctx.pCompute->destroyCommandQueue(ctx.cmdCopyQueue);

    plugin::onShutdown(api::getContext());
}

bool IsContextStatusOk()
{
    auto& ctx = (*mtssg::getContext());

    return ctx.state.status == MTSSGStatus::eOk;
}

uint32_t calcEstimatedVRAMUsageInBytes()
{
    auto& ctx = (*mtssg::getContext());

    uint32_t vRAMUsageInBytes = 0;

    {
        sl::chi::Format generatedFrameFormat{};
        CHI_VALIDATE(ctx.pCompute->getFormat(ctx.swapChainFormat, generatedFrameFormat));
        size_t generatedFrameBpp{};
        CHI_VALIDATE(ctx.pCompute->getBytesPerPixel(generatedFrameFormat, generatedFrameBpp));
        // We have ctx.generateFrame and prev frame copy(ctx.referFrame)
        vRAMUsageInBytes += ctx.swapChainWidth * ctx.swapChainHeight * generatedFrameBpp * 2;
    }

    {
        sl::chi::Format reprojectedFormat{};
        CHI_VALIDATE(ctx.pCompute->getFormat(ctx.reprojectedTip->nativeFormat, reprojectedFormat));
        size_t reprojectedBpp{};
        CHI_VALIDATE(ctx.pCompute->getBytesPerPixel(reprojectedFormat, reprojectedBpp));
        // We have ctx.reprojectedTip and ctx.reprojectedTop
        vRAMUsageInBytes += ctx.swapChainWidth * ctx.swapChainHeight * reprojectedBpp * 2;
    }

    if (ctx.prevDepth)
    {
        sl::chi::Format depthFromat{};
        CHI_VALIDATE(ctx.pCompute->getFormat(ctx.prevDepth->nativeFormat, depthFromat));
        size_t depthBpp{};
        CHI_VALIDATE(ctx.pCompute->getBytesPerPixel(depthFromat, depthBpp));
        vRAMUsageInBytes += ctx.swapChainWidth * ctx.swapChainHeight * depthBpp;
    }
    else
    {
        size_t depthBpp{};
        CHI_VALIDATE(ctx.pCompute->getBytesPerPixel(sl::chi::Format::eFormatD32S32, depthBpp));
        vRAMUsageInBytes += ctx.swapChainWidth * ctx.swapChainHeight * depthBpp;
    }

    if (ctx.prevHudLessColor)
    {
        sl::chi::Format hudLessColorFormat{};
        CHI_VALIDATE(ctx.pCompute->getFormat(ctx.prevHudLessColor->nativeFormat, hudLessColorFormat));
        size_t hudLessColorBpp{};
        CHI_VALIDATE(ctx.pCompute->getBytesPerPixel(hudLessColorFormat, hudLessColorBpp));
        vRAMUsageInBytes += ctx.swapChainWidth * ctx.swapChainHeight * hudLessColorBpp;
    }
    else
    {
        sl::chi::Format hudLessColorFormat{};
        CHI_VALIDATE(ctx.pCompute->getFormat(ctx.swapChainFormat, hudLessColorFormat));
        size_t hudLessColorBpp{};
        CHI_VALIDATE(ctx.pCompute->getBytesPerPixel(hudLessColorFormat, hudLessColorBpp));
        vRAMUsageInBytes += ctx.swapChainWidth * ctx.swapChainHeight * hudLessColorBpp;
    }

    return vRAMUsageInBytes;
}

void createGeneratedFrame(uint32_t width, uint32_t height, DXGI_FORMAT format)
{
    auto& ctx = (*mtssg::getContext());

    if (width < ctx.state.minWidthOrHeight || height < ctx.state.minWidthOrHeight)
    {
        SL_LOG_WARN("SwapChain Resolution Is Too Low, Please Check MTSSGState.minWidthOrHeight For Minimum Supported Resolution. MTSS-FG Will Do Nothing!");
        ctx.state.status = MTSSGStatus::eFailResolutionTooLow;
    }
    else
    {
        ctx.state.status = MTSSGStatus::eOk;
    }

    if ((IsContextStatusOk())           &&
        ((ctx.generateFrame == nullptr) || 
        (ctx.swapChainWidth != width)   ||
        (ctx.swapChainHeight != height) ||
        (ctx.swapChainFormat != format)))
    {
        void* pOldFrame = ctx.generateFrame;
        uint32_t oldWidth = ctx.swapChainWidth;
        uint32_t oldHeight = ctx.swapChainHeight;
        uint32_t oldFormat = ctx.swapChainFormat;

        destroyFrameGenerationResource();

        chi::ResourceDescription desc;
        desc.width = width;
        desc.height = height;
        desc.nativeFormat = format;
        auto status = ctx.pCompute->createTexture2D(desc, ctx.generateFrame, "generate frame");
        assert(status == sl::chi::ComputeStatus::eOk);

        ctx.swapChainWidth = width;
        ctx.swapChainHeight = height;
        ctx.swapChainFormat = format;
        SL_LOG_INFO("createGeneratedFrame width: %u -> %u, height: %u -> %u, format: %u -> %u, pFrame: %p -> %p", oldWidth, width, oldHeight, height,
            static_cast<uint32_t>(oldFormat), static_cast<uint32_t>(format), pOldFrame, ctx.generateFrame);

        desc.nativeFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
        status = ctx.pCompute->createTexture2D(desc, ctx.reprojectedTip, "reprojectedTip");
        assert(status == sl::chi::ComputeStatus::eOk);
        status = ctx.pCompute->createTexture2D(desc, ctx.reprojectedTop, "reprojectedTop");
        assert(status == sl::chi::ComputeStatus::eOk);

        ctx.state.estimatedVRAMUsageInBytes = calcEstimatedVRAMUsageInBytes();
        SL_LOG_INFO("estimatedVRAMUsageInBytes: %llu Bytes(%u MB)", ctx.state.estimatedVRAMUsageInBytes, ctx.state.estimatedVRAMUsageInBytes / 1024 / 1024);
    }
}

HRESULT slHookCreateSwapChain(IDXGIFactory* pFactory, IUnknown* pDevice, DXGI_SWAP_CHAIN_DESC* pDesc, IDXGISwapChain** ppSwapChain, bool& Skip)
{
    SL_LOG_INFO("CreateSwapChain Width: %u, Height: %u, Buffer Count: %u", pDesc->BufferDesc.Width, pDesc->BufferDesc.Height, pDesc->BufferCount);

    HRESULT result = S_OK;

    createGeneratedFrame(pDesc->BufferDesc.Width, pDesc->BufferDesc.Height, pDesc->BufferDesc.Format);

    return result;
}


HRESULT slHookCreateSwapChainForHwnd(IDXGIFactory2 * pFactory, IUnknown * pDevice, HWND hWnd, const DXGI_SWAP_CHAIN_DESC1 * pDesc, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC * pFulScreenDesc, IDXGIOutput * pRestrictToOutput, IDXGISwapChain1 * *ppSwapChain, bool& Skip)
{
    MTSSFG_NOT_TEST();
    SL_LOG_INFO("slHookCreateSwapChainForHwnd");

    HRESULT result = S_OK;

    createGeneratedFrame(pDesc->Width, pDesc->Height, pDesc->Format);

    return result;
}

HRESULT slHookCreateSwapChainForCoreWindow(IDXGIFactory2 * pFactory, IUnknown * pDevice, IUnknown * pWindow, const DXGI_SWAP_CHAIN_DESC1 * pDesc, IDXGIOutput * pRestrictToOutput, IDXGISwapChain1 * *ppSwapChain, bool& Skip)
{
    MTSSFG_NOT_TEST();
    SL_LOG_INFO("slHookCreateSwapChainForCoreWindow");

    HRESULT result = S_OK;

    createGeneratedFrame(pDesc->Width, pDesc->Height, pDesc->Format);

    return result;
}

bool checkTagedResourceUpdate(uint32_t viewportId)
{
    auto& ctx = (*mtssg::getContext());

    sl::CommonResource hudLessRes{};
    getTaggedResource(kBufferTypeHUDLessColor, hudLessRes, viewportId);
    if (static_cast<void*>(hudLessRes) != static_cast<void*>(ctx.currHudLessColor))
    {
        return true;
    }

    sl::CommonResource depthRes{};
    getTaggedResource(kBufferTypeDepth, depthRes, viewportId);
    if (static_cast<void*>(depthRes) != static_cast<void*>(ctx.currDepth))
    {
        return true;
    }

    sl::CommonResource mvecRes{};
    getTaggedResource(kBufferTypeMotionVectors, mvecRes, viewportId);
    if (static_cast<void*>(mvecRes) != static_cast<void*>(ctx.mvec))
    {
        return true;
    }

    return false;
}

sl::Result acquireTaggedResource(uint32_t viewportId)
{
    auto& ctx = (*mtssg::getContext());

    sl::Result ret = getTaggedResource(kBufferTypeHUDLessColor, ctx.currHudLessColor, viewportId);

    if (ret == sl::Result::eOk)
    {
        ret = getTaggedResource(kBufferTypeDepth, ctx.currDepth, viewportId);
    }

    if (ret == sl::Result::eOk)
    {
        ret = getTaggedResource(kBufferTypeMotionVectors, ctx.mvec, viewportId);
    }

    if (ret != sl::Result::eOk)
    {
        SL_LOG_ERROR("Acqueire FG Tagged Resource Fail");
        ctx.state.status = MTSSGStatus::eFailTagResourcesInvalid;
    }

    return ret;
}

sl::Result cloneTaggedResource(
    const sl::CommonResource & currHudLessColor,
    const sl::CommonResource&  currDepth,
    sl::chi::Resource&         clonedHudLessColor,
    sl::chi::Resource&         clonedDepth)
{
    auto& ctx = (*mtssg::getContext());

    cloneResource(currHudLessColor, clonedHudLessColor, "prev hudless color");
    cloneResource(currDepth, clonedDepth, "prev depth");

    return sl::Result::eOk;
}


void processFrameGenerationClearing(sl::mtssg::ClearingConstParamStruct *pCb, uint32_t grid[])
{
    auto& ctx = (*mtssg::getContext());

    CHI_VALIDATE(ctx.pCompute->bindSharedState(ctx.pCmdList->getCmdList()));

    CHI_VALIDATE(ctx.pCompute->bindKernel(ctx.clearKernel));
    CHI_VALIDATE(ctx.pCompute->bindRWTexture(0, 0, ctx.reprojectedTip));
    CHI_VALIDATE(ctx.pCompute->bindRWTexture(1, 1, ctx.reprojectedTop));
    CHI_VALIDATE(ctx.pCompute->bindConsts(2, 0, pCb, sizeof(*pCb), 1));

    CHI_VALIDATE(ctx.pCompute->dispatch(grid[0], grid[1], grid[2]));
}

void processFrameGenerationReprojection(sl::mtssg::MVecParamStruct *pCb, uint32_t grid[])
{
    auto& ctx = (*mtssg::getContext());

    CHI_VALIDATE(ctx.pCompute->bindKernel(ctx.reprojectionKernel));

    CHI_VALIDATE(ctx.pCompute->bindTexture(0, 0, ctx.prevHudLessColor));
    CHI_VALIDATE(ctx.pCompute->bindTexture(1, 1, ctx.currHudLessColor));
    CHI_VALIDATE(ctx.pCompute->bindTexture(2, 2, ctx.prevDepth));
    CHI_VALIDATE(ctx.pCompute->bindTexture(3, 3, ctx.currDepth));
    CHI_VALIDATE(ctx.pCompute->bindTexture(4, 4, ctx.mvec));

    CHI_VALIDATE(ctx.pCompute->bindRWTexture(5, 0, ctx.reprojectedTip));
    CHI_VALIDATE(ctx.pCompute->bindRWTexture(6, 1, ctx.reprojectedTop));

    CHI_VALIDATE(ctx.pCompute->bindConsts(7, 0, pCb, sizeof(*pCb), 1));

    CHI_VALIDATE(ctx.pCompute->bindSampler(8, 0, chi::eSamplerLinearMirror));

    CHI_VALIDATE(ctx.pCompute->dispatch(grid[0], grid[1], grid[2]));

    CHI_VALIDATE(ctx.pCompute->bindRWTexture(0, 0, {}));
    CHI_VALIDATE(ctx.pCompute->bindRWTexture(1, 1, {}));
}

void processFrameGenerationResolution(sl::mtssg::ResolutionConstParamStruct* pCb, uint32_t grid[])
{
    auto& ctx = (*mtssg::getContext());

    CHI_VALIDATE(ctx.pCompute->bindKernel(ctx.resolutionKernel));

    CHI_VALIDATE(ctx.pCompute->bindTexture(0, 0, ctx.prevHudLessColor));
    CHI_VALIDATE(ctx.pCompute->bindTexture(1, 1, ctx.currHudLessColor));
    CHI_VALIDATE(ctx.pCompute->bindTexture(2, 2, ctx.prevDepth));
    CHI_VALIDATE(ctx.pCompute->bindTexture(3, 3, ctx.currDepth));
    CHI_VALIDATE(ctx.pCompute->bindTexture(4, 4, ctx.reprojectedTip));
    CHI_VALIDATE(ctx.pCompute->bindTexture(5, 5, ctx.reprojectedTop));

    CHI_VALIDATE(ctx.pCompute->bindRWTexture(6, 0, ctx.generateFrame));

    CHI_VALIDATE(ctx.pCompute->bindConsts(7, 0, pCb, sizeof(*pCb), 1));

    CHI_VALIDATE(ctx.pCompute->dispatch(grid[0], grid[1], grid[2]));
}

void presentCommon(IDXGISwapChain* swapChain, UINT SyncInterval, UINT Flags, const DXGI_PRESENT_PARAMETERS* pPresentParameters, bool firstFrame, sl::mtssg::PresentApi api)
{
    auto& ctx = (*mtssg::getContext());

    common::EventData eventData;
    eventData.id = 0;
    eventData.frame = ctx.frameId;
    auto getDataResult = common::getConsts(eventData, &ctx.commonConsts);
    bool foundConstData = getDataResult == common::GetDataResult::eFound;
    if (foundConstData == false && IsContextStatusOk())
    {
        SL_LOG_ERROR("Const Data Not Found, MTSS-FG Will Do Nothing!");
        ctx.state.status = MTSSGStatus::eFailCommonConstantsInvalid;
    }
    else
    {
        ctx.state.status = MTSSGStatus::eOk;
    }

    bool taggedResourceUpdate = checkTagedResourceUpdate(0);
    acquireTaggedResource(0);
    if (taggedResourceUpdate)
    {
        cloneTaggedResource(ctx.currHudLessColor, ctx.currDepth, ctx.prevHudLessColor, ctx.prevDepth);
    }

    if (firstFrame || foundConstData == false || taggedResourceUpdate == true || IsContextStatusOk() == false)
    {
        if (api == sl::mtssg::PresentApi::Present)
        {
            swapChain->Present(SyncInterval, Flags);
        }
        else
        {
            static_cast<IDXGISwapChain1*>(swapChain)->Present1(SyncInterval, Flags, pPresentParameters);
        }
        ctx.state.numFramesActuallyPresented++;
    }
    else
    {
        // Not first frame and resource init success, use current surface and refer frame to generate frame
        sl::uint2 dimensions = sl::uint2(ctx.swapChainWidth, ctx.swapChainHeight);
        sl::float2 smoothing = sl::float2(1.0f, 1.0f);
        sl::float2 viewportSize = sl::float2(static_cast<float>(ctx.swapChainWidth), static_cast<float>(ctx.swapChainHeight));
        sl::float2 viewportInv = sl::float2(1.0f / viewportSize.x, 1.0f / viewportSize.y);

        uint32_t grid[] = { (ctx.swapChainWidth + 8 - 1) / 8, (ctx.swapChainHeight + 8 - 1) / 8, 1 };
        //MTFKClearing
        {
            sl::mtssg::ClearingConstParamStruct lb;
            lb.dimensions = dimensions;
            lb.smoothing = smoothing;
            lb.viewportSize = viewportSize;
            lb.viewportInv = viewportInv;

            processFrameGenerationClearing(&lb, grid);
        }

        //MTFKReprojection
        {
            sl::mtssg::MVecParamStruct cb;
            memcpy(&cb.prevClipToClip, &ctx.commonConsts->prevClipToClip, sizeof(float) * 16);
            memcpy(&cb.clipToPrevClip, &ctx.commonConsts->clipToPrevClip, sizeof(float) * 16);
            cb.dimensions = dimensions;
            cb.smoothing = sl::float2(1.0f, 1.0f);
            cb.viewportSize = viewportSize;
            cb.viewportInv = viewportInv;

            processFrameGenerationReprojection(&cb, grid);
        }

        //MTFKResolution
        {
            sl::mtssg::ResolutionConstParamStruct rb;
            rb.dimensions = dimensions;
            rb.smoothing = sl::float2(1.0f, 1.0f);
            rb.viewportSize = viewportSize;
            rb.viewportInv = viewportInv;

            processFrameGenerationResolution(&rb, grid);
        }

        // Copy current surface to refer frame
        auto status = ctx.pCompute->copyResource(ctx.pCmdList->getCmdList(), ctx.appSurfaceBackup, ctx.appSurface);
        assert(status == sl::chi::ComputeStatus::eOk);

        // Copy generate frame to surface present
        status = ctx.pCompute->copyResource(ctx.pCmdList->getCmdList(), ctx.appSurface, ctx.generateFrame);
        assert(status == sl::chi::ComputeStatus::eOk);
        if (api == sl::mtssg::PresentApi::Present)
        {
            swapChain->Present(SyncInterval, Flags);
        }
        else
        {
            static_cast<IDXGISwapChain1*>(swapChain)->Present1(SyncInterval, Flags, pPresentParameters);
        }

        ctx.state.numFramesActuallyPresented++;
        ctx.state.status = MTSSGStatus::eOk;

        // Copy refer frame to surface present
        status = ctx.pCompute->copyResource(ctx.pCmdList->getCmdList(), ctx.appSurface, ctx.appSurfaceBackup);
        assert(status == sl::chi::ComputeStatus::eOk);
        bool showRenderFrame = ((ctx.options.flags & MTSSGFlags::eShowOnlyInterpolatedFrame) == 0);
        if (showRenderFrame)
        {
            if (api == sl::mtssg::PresentApi::Present)
            {
                swapChain->Present(SyncInterval, Flags);
            }
            else
            {
                static_cast<IDXGISwapChain1*>(swapChain)->Present1(SyncInterval, Flags, pPresentParameters);
            }
            ctx.state.numFramesActuallyPresented++;
        }
    }

    auto status = ctx.pCompute->copyResource(ctx.pCmdList->getCmdList(), ctx.prevDepth, ctx.currDepth);
    assert(status == sl::chi::ComputeStatus::eOk);
    status = ctx.pCompute->copyResource(ctx.pCmdList->getCmdList(), ctx.prevHudLessColor, ctx.currHudLessColor);
    assert(status == sl::chi::ComputeStatus::eOk);
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
        bool firstFrame = false;
        if (ctx.appSurface == nullptr)
        {
            ctx.pCompute->getSwapChainBuffer(swapChain, 0, ctx.appSurface);
            cloneResource(ctx.appSurface, ctx.appSurfaceBackup, "app surface backup");
            firstFrame = true;
        }

        presentCommon(swapChain, SyncInterval, Flags, nullptr, firstFrame, sl::mtssg::PresentApi::Present);
    }

    ctx.frameId++;
    return S_OK;
}

HRESULT slHookPresent1(IDXGISwapChain * SwapChain, UINT SyncInterval, UINT PresentFlags, const DXGI_PRESENT_PARAMETERS * pPresentParameters, bool& Skip)
{
    MTSSFG_NOT_TEST();
    SL_LOG_INFO("slHookPresent1");

    HRESULT result = S_OK;

    auto& ctx = (*mtssg::getContext());
    if (ctx.options.mode == MTSSGMode::eOff)
    {
        SL_LOG_INFO("MTSS-G Mode is Off, present return directly.");
        Skip = false;
    }
    else
    {
        Skip = true;
        bool firstFrame = false;
        if (ctx.appSurface == nullptr)
        {
            ctx.pCompute->getSwapChainBuffer(SwapChain, 0, ctx.appSurface);
            cloneResource(ctx.appSurface, ctx.appSurfaceBackup, "app surface backup");
            firstFrame = true;
        }

        presentCommon(SwapChain, SyncInterval, PresentFlags, pPresentParameters, firstFrame, sl::mtssg::PresentApi::Present1);
    }

    ctx.frameId++;

    return result;
}

HRESULT slHookResizeBuffersPre(IDXGISwapChain* SwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT& SwapChainFlags, bool& Skip)
{
    SL_LOG_INFO("slHookResizeBuffersPre");

    HRESULT result = S_OK;

    createGeneratedFrame(Width, Height, NewFormat);

    return result;
}

HRESULT slHookResizeBuffersPost(IDXGISwapChain* SwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT& SwapChainFlags)
{
    SL_LOG_INFO("slHookResizeBuffersPost");

    HRESULT result = S_OK;

    return result;
}

HRESULT slHookResizeBuffers1Pre(IDXGISwapChain* SwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT Format, UINT SwapChainFlags, const UINT* pCreationNodeMask, IUnknown* const* ppPresentQueue, bool& Skip)
{
    SL_LOG_INFO("slHookResizeBuffers1Pre");

    return S_OK;
}

HRESULT slHookSetFullscreenStatePre(IDXGISwapChain * SwapChain, BOOL pFullscreen, IDXGIOutput * ppTarget, bool& Skip)
{
    SL_LOG_INFO("slHookSetFullscreenStatePre fullscreen:%s", pFullscreen ? "YES" : "NO");

    HRESULT result = S_OK;

    return result;
}

HRESULT slHookSetFullscreenStatePost(IDXGISwapChain* SwapChain, BOOL pFullscreen, IDXGIOutput* ppTarget)
{
    SL_LOG_INFO("slHookSetFullscreenStatePost fullscreen:%s", pFullscreen ? "YES" : "NO");

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
        info.requiredTags = { { kBufferTypeDepth, ResourceLifecycle::eValidUntilPresent},
                              {kBufferTypeMotionVectors, ResourceLifecycle::eValidUntilPresent},
                              { kBufferTypeHUDLessColor, ResourceLifecycle::eValidUntilPresent}};

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

    return sl::Result::eOk;
}

SL_EXPORT void* slGetPluginFunction(const char* functionName)
{
    //! Forward declarations
    bool slOnPluginLoad(sl::param::IParameters * params, const char* loaderJSON, const char** pluginJSON);

    //! Core API
    SL_EXPORT_FUNCTION(slOnPluginLoad);
    SL_EXPORT_FUNCTION(slOnPluginShutdown);
    SL_EXPORT_FUNCTION(slOnPluginStartup);

    SL_EXPORT_FUNCTION(slHookCreateSwapChain);
    SL_EXPORT_FUNCTION(slHookCreateSwapChainForHwnd);
    SL_EXPORT_FUNCTION(slHookCreateSwapChainForCoreWindow);
    SL_EXPORT_FUNCTION(slHookPresent);
    SL_EXPORT_FUNCTION(slHookPresent1);
    SL_EXPORT_FUNCTION(slHookResizeBuffersPre);
    SL_EXPORT_FUNCTION(slHookResizeBuffersPost);
    SL_EXPORT_FUNCTION(slHookResizeBuffers1Pre);
    SL_EXPORT_FUNCTION(slHookSetFullscreenStatePre);
    SL_EXPORT_FUNCTION(slHookSetFullscreenStatePost);

    SL_EXPORT_FUNCTION(slMTSSGGetState);
    SL_EXPORT_FUNCTION(slMTSSGSetOptions);

    return nullptr;
}

}
