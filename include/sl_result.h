/* Copyright (c) 2020-2023 MooreThreads Coporation. All rights reserved. */

#pragma once

namespace sl
{

enum class Result
{
    eOk,
    eErrorIO,
    eErrorDriverOutOfDate,
    eErrorOSOutOfDate,
    eErrorOSDisabledHWS,
    eErrorDeviceNotCreated,
    eErrorNoSupportedAdapterFound,
    eErrorAdapterNotSupported,
    eErrorNoPlugins,
    eErrorVulkanAPI,
    eErrorDXGIAPI,
    eErrorD3DAPI,
    eErrorNRDAPI,
    eErrorNVAPI,
    eErrorReflexAPI,
    eErrorNGXFailed,
    eErrorJSONParsing,
    eErrorMissingProxy,
    eErrorMissingResourceState,
    eErrorInvalidIntegration,
    eErrorMissingInputParameter,
    eErrorNotInitialized,
    eErrorComputeFailed,
    eErrorInitNotCalled,
    eErrorExceptionHandler,
    eErrorInvalidParameter,
    eErrorMissingConstants,
    eErrorDuplicatedConstants,
    eErrorMissingOrInvalidAPI,
    eErrorCommonConstantsMissing,
    eErrorUnsupportedInterface,
    eErrorFeatureMissing,
    eErrorFeatureNotSupported,
    eErrorFeatureMissingHooks,
    eErrorFeatureFailedToLoad,
    eErrorFeatureWrongPriority,
    eErrorFeatureMissingDependency,
    eErrorFeatureManagerInvalidState,
    eErrorInvalidState,
    eWarnOutOfVRAM
};

}
