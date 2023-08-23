// Copyright (c) 2023 Moore Threads Technology Co. Ltd. All rights reserved.
#include "mtss_common.hlsli"

//------------------------------------------------------- PARAMETERS
RWTexture2D<uint> motionReprojTipX;
RWTexture2D<uint> motionReprojTipY;
RWTexture2D<uint> motionReprojTopX;
RWTexture2D<uint> motionReprojTopY;

Texture2D<float2> motionUnprojected;
RWTexture2D<float4> motionReprojected;

cbuffer shaderConsts : register(b0)
{
    uint2 dimensions;
    float2 tipTopDistance;
    float2 viewportSize;
    float2 viewportInv;
}

SamplerState bilinearMirroredSampler : register(s0);

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
#ifdef UNREAL_ENGINE_COORDINATES
    float2 screenPos = ViewportUVToScreenPos(viewportUV);
#endif
#ifdef NVRHI_DONUT_COORDINATES
    float2 screenPos = viewportUV;
#endif
	
    const float distanceTip = tipTopDistance.x;
    const float distanceTop = tipTopDistance.y;
	
    uint tipX = motionReprojTipX[currentPixelIndex];
    uint tipY = motionReprojTipY[currentPixelIndex];
    int2 tipIndex = int2(tipX & IndexLast13DigitsMask, tipY & IndexLast13DigitsMask);
    bool bIsTipUnwritten = any(tipIndex == UnwrittenIndexIndicator);
#ifdef UNREAL_ENGINE_COORDINATES
    float2 motionVectorTip = motionUnprojected[tipIndex];
    float2 samplePosTip = screenPos - motionVectorTip * distanceTip;
    float2 motionCaliberatedUVTip = ScreenPosToViewportUV(samplePosTip);
    motionCaliberatedUVTip = clamp(motionCaliberatedUVTip, float2(0.0f, 0.0f), float2(1.0f, 1.0f));
#endif
#ifdef NVRHI_DONUT_COORDINATES
    float2 motionVectorTip = motionUnprojected[tipIndex] * viewportInv;
    float2 samplePosTip = screenPos + motionVectorTip * distanceTip;
    float2 motionCaliberatedUVTip = samplePosTip;
    motionCaliberatedUVTip = clamp(motionCaliberatedUVTip, float2(0.0f, 0.0f), float2(1.0f, 1.0f));
#endif

	float2 motionTipCaliberated = motionUnprojected.SampleLevel(bilinearMirroredSampler, motionCaliberatedUVTip, 0) * viewportInv;
    if (bIsTipUnwritten)
    {
        motionTipCaliberated = float2(ImpossibleMotionVecValue, ImpossibleMotionVecValue);
    }
	
    uint topX = motionReprojTopX[currentPixelIndex];
    uint topY = motionReprojTopY[currentPixelIndex];
    int2 topIndex = int2(topX & IndexLast13DigitsMask, topY & IndexLast13DigitsMask);
    bool bIsTopUnwritten = any(topIndex == UnwrittenIndexIndicator);
#ifdef UNREAL_ENGINE_COORDINATES
    float2 motionVectorTop = motionUnprojected[topIndex];
    float2 samplePosTop = screenPos + motionVectorTop * distanceTop;
    float2 motionCaliberatedUVTop = ScreenPosToViewportUV(samplePosTop);
    motionCaliberatedUVTop = clamp(motionCaliberatedUVTop, float2(0.0f, 0.0f), float2(1.0f, 1.0f));
#endif
#ifdef NVRHI_DONUT_COORDINATES
    float2 motionVectorTop = motionUnprojected[topIndex] * viewportInv;
    float2 samplePosTop = screenPos - motionVectorTop * distanceTop;
    float2 motionCaliberatedUVTop = samplePosTop;
    motionCaliberatedUVTop = clamp(motionCaliberatedUVTop, float2(0.0f, 0.0f), float2(1.0f, 1.0f));
#endif

    float2 motionTopCaliberated = motionUnprojected.SampleLevel(bilinearMirroredSampler, motionCaliberatedUVTop, 0) * viewportInv;
    if (bIsTopUnwritten)
    {
        motionTopCaliberated = float2(ImpossibleMotionVecValue, ImpossibleMotionVecValue);
    }
	
	{
        bool bIsValidhistoryPixel = all(uint2(currentPixelIndex) < dimensions);
        if (bIsValidhistoryPixel)
        {
            motionReprojected[currentPixelIndex] = float4(motionTopCaliberated, motionTipCaliberated);
        }
    }
}
