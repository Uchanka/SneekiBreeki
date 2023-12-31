// Copyright (c) 2023 Moore Threads Technology Co. Ltd. All rights reserved.
#include "mtss_common.hlsli"

//------------------------------------------------------- PARAMETERS
RWTexture2D<uint> motionReprojHalfTipX;
RWTexture2D<uint> motionReprojHalfTipY;
RWTexture2D<uint> motionReprojFullX;
RWTexture2D<uint> motionReprojFullY;

RWTexture2D<float2> motionReprojectedTip;
RWTexture2D<float2> motionReprojectedFull;

Texture2D<float2> currMotionUnprojected;
Texture2D<float2> prevMotionUnprojected;
Texture2D<float> prevDepthUnprojected;

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
	
    const float distanceFull = tipTopDistance.x + tipTopDistance.y;
    const float distanceHalfTip = tipTopDistance.x;
    const float distanceHalfTop = tipTopDistance.y;
    
    uint halfTipX = motionReprojHalfTipX[currentPixelIndex];
    uint halfTipY = motionReprojHalfTipY[currentPixelIndex];
    int2 halfTipIndex = int2(halfTipX & IndexLast13DigitsMask, halfTipY & IndexLast13DigitsMask);
    bool bIsHalfTipUnwritten = any(halfTipIndex == UnwrittenIndexIndicator);
    float2 motionVectorHalfTip = prevMotionUnprojected[halfTipIndex];
    float2 samplePosHalfTip = screenPos + motionVectorHalfTip * distanceHalfTip;
    float2 motionCaliberatedUVHalfTip = samplePosHalfTip;
    motionCaliberatedUVHalfTip = clamp(motionCaliberatedUVHalfTip, float2(0.0f, 0.0f), float2(1.0f, 1.0f));
    float2 motionHalfTipCaliberated = prevMotionUnprojected.SampleLevel(bilinearClampedSampler, motionCaliberatedUVHalfTip, 0).xy;
    //prevMotionUnprojected.SampleLevel(bilinearClampedSampler, motionCaliberatedUVHalfTip, 0);
    if (bIsHalfTipUnwritten)
    {
        motionHalfTipCaliberated = float2(0.0f, 0.0f) + float2(ImpossibleMotionOffset, ImpossibleMotionOffset);
    }

    uint fullX = motionReprojFullX[currentPixelIndex];
    uint fullY = motionReprojFullY[currentPixelIndex];
    int2 fullIndex = int2(fullX & IndexLast13DigitsMask, fullY & IndexLast13DigitsMask);
    bool bIsFullUnwritten = any(fullIndex == UnwrittenIndexIndicator);
    float2 motionVectorFull = currMotionUnprojected[fullIndex];
    float2 samplePosFull = screenPos - motionVectorFull * distanceHalfTop;
    float2 motionCaliberatedUVFull = samplePosFull;
    motionCaliberatedUVFull = clamp(motionCaliberatedUVFull, float2(0.0f, 0.0f), float2(1.0f, 1.0f));
    float2 motionFullCaliberated = currMotionUnprojected.SampleLevel(bilinearClampedSampler, motionCaliberatedUVFull, 0);
    if (all(abs(motionFullCaliberated) < viewportInv))
    {
        motionFullCaliberated = motionHalfTipCaliberated;
    }
    
	{
        bool bIsValidhistoryPixel = all(uint2(currentPixelIndex) < dimensions);
        if (bIsValidhistoryPixel)
        {
            motionReprojectedTip[currentPixelIndex] = motionHalfTipCaliberated;
            motionReprojectedFull[currentPixelIndex] = motionFullCaliberated;
        }
    }
}
