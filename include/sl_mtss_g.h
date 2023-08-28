/* Copyright (c) 2020-2023 MooreThreads Coporation. All rights reserved. */

#pragma once

#include "sl_consts.h"

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
    eShowDebugOverlay          = 1 << 1,
};

struct APIError
{
    union
    {
        HRESULT hres;
    };
};

// Adds various useful operators for our enum
SL_ENUM_OPERATORS_32(MTSSGFlags)

// {d7bf2851-c4c0-407d-a556-b5039b2754f9}
SL_STRUCT(MTSSGOptions, StructType({ 0xd7bf2851, 0xc4c0, 0x407d, { 0xa5, 0x56, 0xb5, 0x03, 0x9b, 0x27, 0x54, 0xf9 } }), kStructVersion1)
    //! Specifies which mode should be used.
    MTSSGMode mode = MTSSGMode::eOff;
    //! Must be 1
    uint32_t numFramesToGenerate = 1;
    //! Optional - Flags used to enable or disable certain functionality
    MTSSGFlags flags{};
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
    //! Some tag resources are invalid, see programming guide for more details
    eFailTagResourcesInvalid = 1 << 1,
    //! Some constants are invalid, see programming guide for more details
    eFailCommonConstantsInvalid = 1 << 2,
};

// Adds various useful operators for our enum
SL_ENUM_OPERATORS_32(MTSSGStatus)

// {66cbcc7c-2312-4f28-a2e9-5a5563267158}
SL_STRUCT(MTSSGState, StructType({ 0x66cbcc7c, 0x2312, 0x4f28, { 0xa2, 0xe9, 0x5a, 0x55, 0x63, 0x26, 0x71, 0x58 } }), kStructVersion1)
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
