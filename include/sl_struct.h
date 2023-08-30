/* Copyright (c) 2020-2023 MooreThreads Coporation. All rights reserved. */

#pragma once

#include <stdint.h>
#include <string.h>

namespace sl
{

//! GUID
struct StructType
{
    unsigned long  data1;
    unsigned short data2;
    unsigned short data3;
    unsigned char  data4[8];

    inline bool operator==(const StructType& rhs) const { return memcmp(this, &rhs, sizeof(this)) == 0; }
    inline bool operator!=(const StructType& rhs) const { return memcmp(this, &rhs, sizeof(this)) != 0; }
};

//! SL is using typed and versioned structures which can be chained or not.
//! 
//! --- OPTION 1 ---
//! 
//! New members must be added at the end and version needs to be increased:
//! 
//! SL_STRUCT(S1, GUID1, kStructVersion1)
//! {
//!     A
//!     B
//!     C
//! }
//! 
//! SL_STRUCT(S1, GUID1, kStructVersion2) // Note that version is bumped
//! {
//!     // V1
//!     A
//!     B
//!     C
//! 
//!     //! V2 - new members always go at the end!
//!     D
//!     E
//! }
//! 
//! Here is one example on how to check for version and handle backwards compatibility:
//! 
//! void func(const S1* input)
//! {
//!     // Access A, B, C as needed
//!     ...
//!     if (input->structVersion >= kStructVersion2)
//!     {
//!         // Safe to access D, E
//!     }
//! }


//! --- OPTION 2 ---
//! 
//! New members are optional and added to a new struct which is then chained as needed:
//! 
//! SL_STRUCT(S1, GUID1, kStructVersion1)
//! {
//!     A
//!     B
//!     C
//! }
//! 
//! SL_STRUCT(S2, GUID2, kStructVersion1) // Note that this is a different struct with new GUID
//! {
//!     D
//!     E
//! }
//! 
//! S1 s1;
//! S2 s2
//! s1.next = &s2; // optional parameters in S2

//! IMPORTANT: New members in the structure always go at the end!
//!
constexpr uint32_t kStructVersion1 = 1;
constexpr uint32_t kStructVersion2 = 2;
constexpr uint32_t kStructVersion3 = 3;

struct BaseStructure
{
    BaseStructure() = delete;
    BaseStructure(StructType t, uint32_t v) : structType(t), structVersion(v) {};
    BaseStructure* next{};
    StructType structType{};
    size_t structVersion;
};

#define SL_STRUCT(name, guid, version)                                      \
struct name : public BaseStructure                                          \
{                                                                           \
    name##() : BaseStructure(guid, version){}                               \
    constexpr static StructType s_structType = guid;                        \

#define SL_STRUCT_PROTECTED(name, guid, version)                            \
struct name : public BaseStructure                                          \
{                                                                           \
protected:                                                                  \
    name##() : BaseStructure(guid, version){}                               \
public:                                                                     \
    constexpr static StructType s_structType = guid;                        \

} // namespace sl
