/* Copyright (c) 2020-2023 MooreThreads Coporation. All rights reserved. */

#pragma once

namespace sl
{

//! Each feature must have a unique id, please see sl.h Feature
//! 
constexpr uint32_t kFeatureTemplate = 0xffff;

//! If your plugin does not have any constants then the code below can be removed
//! 
enum class TemplateMode : uint32_t
{
    eOff,
    eOn
};

//! IMPORTANT: Each structure must have a unique GUID assigned, change this as needed
//!
// {29DF7FE0-273A-4D72-B481-2DC823D5B1AD}
SL_STRUCT(TemplateConstants, StructType({ 0x29df7fe0, 0x273a, 0x4d72, { 0xb4, 0x81, 0x2d, 0xc8, 0x23, 0xd5, 0xb1, 0xad } }), kStructVersion1)
    TemplateMode mode = TemplateMode::eOff;

    //! IMPORTANT: New members go here or if optional can be chained in a new struct, see sl_struct.h for details
};

//! IMPORTANT: Each structure must have a unique GUID assigned, change this as needed
//!
// {39DF7FE0-283A-4D72-B481-2DC823D5B1AD}
SL_STRUCT(TemplateSettings, StructType({ 0x39df7fe0, 0x283a, 0x4d72, { 0xb4, 0x81, 0x2d, 0xc8, 0x23, 0xd5, 0xb1, 0xad } }), kStructVersion1)
    
    //! IMPORTANT: New members go here or if optional can be chained in a new struct, see sl_struct.h for details
};

}
