// Copyright (c) 2023 Moore Threads Technology Co. Ltd. All rights reserved.
#include "mtss_common.hlsli"

//------------------------------------------------------- PARAMETERS
Texture2D<float2> currMvec;
Texture2D<float2> prevMvec;
RWTexture2D<float2> currMvecNorm;
RWTexture2D<float2> prevMvecNorm;

cbuffer shaderConsts : register(b0)
{
    uint2 dimensions;
    float2 distance;
    float2 viewportSize;
    float2 viewportInv;
}

#define TILE_SIZE 8

//------------------------------------------------------- ENTRY POINT
[shader("compute")]
[numthreads(TILE_SIZE, TILE_SIZE, 1)]
void main(uint2 groupId : SV_GroupID, uint2 localId : SV_GroupThreadID, uint groupThreadIndex : SV_GroupIndex)
{
    uint2 dispatchThreadId = localId + groupId * uint2(TILE_SIZE, TILE_SIZE);
    int2 currentPixelIndex = dispatchThreadId;
    
    bool bIsValidPixel = all(uint2(currentPixelIndex) < dimensions);
    if (bIsValidPixel)
    {
        currMvecNorm[currentPixelIndex] = currMvec[currentPixelIndex] * viewportInv;
        prevMvecNorm[currentPixelIndex] = prevMvec[currentPixelIndex] * viewportInv;
    }
}
