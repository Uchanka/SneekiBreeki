// Copyright (c) 2023 Moore Threads Technology Co. Ltd. All rights reserved.
#include "mtss_common.hlsli"

//------------------------------------------------------- PARAMETERS
RWTexture2D<uint> motionReprojFullX;
RWTexture2D<uint> motionReprojFullY;

RWTexture2D<float2> motionReprojectedFull;

Texture2D<float2> currMotionUnprojected;
Texture2D<float> prevDepthTexture;

cbuffer shaderConsts : register(b0)
{
    float4x4 prevClipToClip;
    float4x4 clipToPrevClip;
    
    uint2 dimensions;
    float2 tipTopDistance;
    float2 viewportSize;
    float2 viewportInv;
}

SamplerState bilinearClampedSampler : register(s0);

#define TILE_SIZE 8

//------------------------------------------------------- ENTRY POINT
[shader("compute")]
[numthreads(TILE_SIZE, TILE_SIZE, 1)]
void main(uint2 groupId : SV_GroupID, uint2 localId : SV_GroupThreadID, uint groupThreadIndex : SV_GroupIndex)
{
    uint2 dispatchThreadId = localId + groupId * uint2(TILE_SIZE, TILE_SIZE);
    int2 currentPixelIndex = dispatchThreadId;
	
    float2 pixelCenter = float2(currentPixelIndex) + 0.5f;
    float2 viewportUV = pixelCenter * viewportInv;
    float2 screenPos = viewportUV;
	
    const float distanceHalfTop = tipTopDistance.y;

    uint fullX = motionReprojFullX[currentPixelIndex];
    uint fullY = motionReprojFullY[currentPixelIndex];
    int2 fullIndex = int2(fullX & IndexLast13DigitsMask, fullY & IndexLast13DigitsMask);
    bool bIsFullUnwritten = any(fullIndex == UnwrittenIndexIndicator);
    float2 motionVectorFull = currMotionUnprojected[fullIndex];
    float2 samplePosFull = screenPos - motionVectorFull * distanceHalfTop;
    float2 motionCaliberatedUVFull = samplePosFull;
    motionCaliberatedUVFull = clamp(motionCaliberatedUVFull, float2(0.0f, 0.0f), float2(1.0f, 1.0f));
    float2 motionFullCaliberated = currMotionUnprojected.SampleLevel(bilinearClampedSampler, motionCaliberatedUVFull, 0) * viewportInv;
    if (all(abs(motionFullCaliberated) < viewportInv))
    {
        motionFullCaliberated = -ComputeStaticVelocityTopTip(screenPos, prevDepthTexture.SampleLevel(bilinearClampedSampler, motionCaliberatedUVFull, 0).r, prevClipToClip);
    }
    
	{
        bool bIsValidhistoryPixel = all(uint2(currentPixelIndex) < dimensions);
        if (bIsValidhistoryPixel)
        {
            motionReprojectedFull[currentPixelIndex] = motionFullCaliberated;
        }
    }
}
