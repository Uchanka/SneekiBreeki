/* Copyright (c) 2020-2023 MooreThreads Coporation. All rights reserved. */

#pragma once

#include "sl_consts.h"
#include <vector>

// Forward declarations matching MS and VK specs
using HRESULT = long;
enum VkResult : int;

namespace sl
{

//! MTSS Frame Generation
constexpr Feature kFeatureMTSS_G = 10000;

enum class MTSSGMode : uint32_t
{
    eOff,
    eOn,
    eCount
};

enum class MTSSGFlags : uint32_t
{
    eShowOnlyInterpolatedFrame = 1 << 0,
    eDynamicResolutionEnabled = 1 << 1,
    eRequestVRAMEstimate = 1 << 2
};

// Adds various useful operators for our enum
SL_ENUM_OPERATORS_32(MTSSGFlags)

// {FAC5F1CB-2DFD-4F36-A1E6-3A9E865256C5}
SL_STRUCT(MTSSGOptions, StructType({ 0xfac5f1cb, 0x2dfd, 0x4f36, { 0xa1, 0xe6, 0x3a, 0x9e, 0x86, 0x52, 0x56, 0xc5 } }), kStructVersion1)
    //! Specifies which mode should be used.
    MTSSGMode mode = MTSSGMode::eOff;
    //! Must be 1
    uint32_t numFramesToGenerate = 1;
    //! Optional - Flags used to enable or disable certain functionality
    MTSSGFlags flags{};
    //! Optional - Dynamic resolution optimal width (used only if eDynamicResolutionEnabled is set)
    uint32_t dynamicResWidth{};
    //! Optional - Dynamic resolution optimal height (used only if eDynamicResolutionEnabled is set)
    uint32_t dynamicResHeight{};
    //! Optional - Expected number of buffers in the swap-chain
    uint32_t numBackBuffers{};
    //! Optional - Expected width of the input render targets (depth, motion-vector buffers etc)
    uint32_t mvecDepthWidth{};
    //! Optional - Expected height of the input render targets (depth, motion-vector buffers etc)
    uint32_t mvecDepthHeight{};
    //! Optional - Expected width of the back buffers in the swap-chain
    uint32_t colorWidth{};
    //! Optional - Expected height of the back buffers in the swap-chain
    uint32_t colorHeight{};
    //! Optional - Indicates native format used for the swap-chain back buffers
    uint32_t colorBufferFormat{};
    //! Optional - Indicates native format used for eMotionVectors
    uint32_t mvecBufferFormat{};
    //! Optional - Indicates native format used for eDepth
    uint32_t depthBufferFormat{};
    //! Optional - Indicates native format used for eHUDLessColor
    uint32_t hudLessBufferFormat{};
    //! Optional - Indicates native format used for eUIColorAndAlpha
    uint32_t uiBufferFormat{};
    //! Optional - if specified MTSSG will return any errors which occur when calling underlying API (DXGI or Vulkan)
    //PFunOnAPIErrorCallback* onErrorCallback{};

    //! IMPORTANT: New members go here or if optional can be chained in a new struct, see sl_struct.h for details
};


enum class MTSSGStatus : uint32_t
{
    //! Everything is working as expected
    eOk = 0,
    //! Output resolution (size of the back buffers in the swap-chain) is too low
    eFailResolutionTooLow = 1 << 0,
    //! Reflex is not active while MTSS-G is running, Reflex must be turned on when MTSS-G is on
    eFailReflexNotDetectedAtRuntime = 1 << 1,
    //! HDR format not supported, see MTSS-G programming guide for more details
    eFailHDRFormatNotSupported = 1 << 2,
    //! Some constants are invalid, see programming guide for more details
    eFailCommonConstantsInvalid = 1 << 3,
    //! D3D integrations must use SwapChain::GetCurrentBackBufferIndex API
    eFailGetCurrentBackBufferIndexNotCalled = 1 << 4
};

// Adds various useful operators for our enum
SL_ENUM_OPERATORS_32(MTSSGStatus)

// {CC8AC8E1-A179-44F5-97FA-E74112F9BC61}
SL_STRUCT(MTSSGState, StructType({ 0xcc8ac8e1, 0xa179, 0x44f5, { 0x97, 0xfa, 0xe7, 0x41, 0x12, 0xf9, 0xbc, 0x61 } }), kStructVersion1)
    //! Specifies the amount of memory expected to be used
    uint64_t estimatedVRAMUsageInBytes{};
    //! Specifies current status of MTSS-G
    MTSSGStatus status{};
    //! Specifies minimum supported dimension
    uint32_t minWidthOrHeight{};
    //! Number of frames presented since the last 'slMTSSGGetState' call
    uint32_t numFramesActuallyPresented{};

    //! IMPORTANT: New members go here or if optional can be chained in a new struct, see sl_struct.h for details
};

}

//! Provides MTSS-G state
//!
//! Call this method to obtain current state of MTSS-G
//!
//! @param viewport Specified viewport we are working with
//! @param state Reference to a structure where state is returned
//! @param options Specifies MTSS-G options to use (can be null if not needed)
//! @return sl::ResultCode::eOk if successful, error code otherwise (see sl_result.h for details)
//!
//! This method is NOT thread safe.
using PFun_slMTSSGGetState = sl::Result(const sl::ViewportHandle& viewport, sl::MTSSGState& state, const sl::MTSSGOptions* options);

//! Sets MTSS-G options
//!
//! Call this method to turn MTSS-G on/off, change modes etc.
//!
//! @param viewport Specified viewport we are working with
//! @param options Specifies MTSS-G options to use
//! @return sl::ResultCode::eOk if successful, error code otherwise (see sl_result.h for details)
//!
//! This method is NOT thread safe.
using PFun_slMTSSGSetOptions = sl::Result(const sl::ViewportHandle& viewport, const sl::MTSSGOptions& options);

//! HELPERS
//! 
inline sl::Result slMTSSGGetState(const sl::ViewportHandle& viewport, sl::MTSSGState& state, const sl::MTSSGOptions* options)
{
    SL_FEATURE_FUN_IMPORT_STATIC(sl::kFeatureMTSS_G, slMTSSGGetState);
    return s_slMTSSGGetState(viewport, state, options);
}

inline sl::Result slMTSSGSetOptions(const sl::ViewportHandle& viewport, const sl::MTSSGOptions& options)
{
    SL_FEATURE_FUN_IMPORT_STATIC(sl::kFeatureMTSS_G, slMTSSGSetOptions);
    return s_slMTSSGSetOptions(viewport, options);
}
