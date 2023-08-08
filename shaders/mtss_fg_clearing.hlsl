// Copyright (c) 2023 Moore Threads Technology Co. Ltd. All rights reserved.
#include "mtss_common.hlsli"

RWTexture2D<float4> reprojectedTip;
RWTexture2D<float4> reprojectedTop;

cbuffer shaderConsts : register(b0)
{
    uint2 dimensions;
    float2 smoothing;
    float2 viewportSize;
    float2 viewportInv;
}

#define TILE_SIZE 8

[shader("compute")]
[numthreads(TILE_SIZE, TILE_SIZE, 1)]
void main(uint2 groupId : SV_GroupID, uint2 localId : SV_GroupThreadID, uint groupThreadIndex : SV_GroupIndex)
{
    uint2 dispatchThreadId = localId + groupId * uint2(TILE_SIZE, TILE_SIZE);
    int2 currentPixelIndex = dispatchThreadId;
    
    bool bIsValidPixel = all(uint2(currentPixelIndex) < dimensions);
    if (bIsValidPixel)
    {
        reprojectedTip[currentPixelIndex] = mtss_float4(0.0f, 0.0f, 0.0f, 0.0f);
        reprojectedTop[currentPixelIndex] = mtss_float4(0.0f, 0.0f, 0.0f, 0.0f);
    }
}
