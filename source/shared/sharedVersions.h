/*
* Copyright (c) 2023 Moore Threads CORPORATION & AFFILIATES. All rights reserved.
*/
#pragma once

#include "include/sl_version.h"

#define SHARED_VERSION_MAJOR SL_VERSION_MAJOR
#define SHARED_VERSION_MINOR SL_VERSION_MINOR
#define SHARED_VERSION_PATCH SL_VERSION_PATCH
#if defined(SL_PRODUCTION)
#define BUILD_CONFIG_INFO "PRODUCTION"
#elif defined(SL_REL_EXT_DEV)
#define BUILD_CONFIG_INFO "DEVELOPMENT"
#elif defined(SL_DEBUG)
#define BUILD_CONFIG_INFO "DEBUG"
#elif defined(SL_RELEASE)
#define BUILD_CONFIG_INFO "RELEASE"
#elif defined(SL_PROFILING)
#define BUILD_CONFIG_INFO "PROFILING"
#else
#error "Unsupported build config"
#endif

#if defined(SL_PRODUCTION)
#define DISTRIBUTION_INFO "PRODUCTION"
#else
#define DISTRIBUTION_INFO "NOT FOR PRODUCTION"
#endif

