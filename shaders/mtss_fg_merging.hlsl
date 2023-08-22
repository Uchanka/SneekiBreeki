// Copyright (c) 2023 Moore Threads Technology Co. Ltd. All rights reserved.
#include "mtss_common.hlsli"

//------------------------------------------------------- PARAMETERS
Texture2D<uint> motionReprojTipX;
Texture2D<uint> motionReprojTipY;
Texture2D<uint> motionReprojTopX;
Texture2D<uint> motionReprojTopY;

Texture2D<mtss_float2> motionUnprojected;
RWTexture2D<mtss_float2> motionReprojected;

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
    float2 viewportUV = pixelCenter * HistoryInfo_ViewportSizeInverse;
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

	mtss_float2 motionTipCaliberated = motionUnprojected.SampleLevel(bilinearMirroredSampler, motionCaliberatedUVTip, 0);
    if (bIsTipUnwritten)
    {
        motionTipCaliberated = mtss_float2(ImpossibleMotionVecValue, ImpossibleMotionVecValue);
    }
	
    uint topX = motionReprojTopX[currentPixelIndex];
    uint topY = motionReprojTopY[currentPixelIndex];
    int2 topIndex = int2(topX & IndexLast13DigitsMask, topY & IndexLast13DigitsMask);
    bool bIsTopUnwritten = any(topIndex == UnwrittenIndexIndicator);
#ifdef UNREAL_ENGINE_COORDINATES
    float2 motionVectorTop = motionUnprojected[topIndex];
    float2 samplePosTop = screenPos + motionVectorTop * distanceTop;
    float2 motionCaliberatedUVTop = ScreenPosToViewportUV(samplePosTop);
    motionCaliberatedUVTop = clamp(motionCaliberatedUVTop, PrevHistoryInfo_UVViewportBilinearMin, PrevHistoryInfo_UVViewportBilinearMax);
#endif
#ifdef NVRHI_DONUT_COORDINATES
    float2 motionVectorTop = motionUnprojected[topIndex] * viewportInv;
    float2 samplePosTop = screenPos - motionVectorTop * distanceTop;
    float2 motionCaliberatedUVTop = samplePosTop;
    motionCaliberatedUVTop = clamp(motionCaliberatedUVTop, float2(0.0f, 0.0f), float2(1.0f, 1.0f));
#endif

    float2 motionTopCaliberated = motionUnprojected.SampleLevel(GlobalBilinearClampedSampler, motionCaliberatedUVTop, 0).xy;
    if (bIsTopUnwritten)
    {
        motionTopCaliberated = mtss_float2(ImpossibleMotionVecValue, ImpossibleMotionVecValue);
    }
	
    ISOLATE
	{
        bool bIsValidhistoryPixel = all(uint2(currentPixelIndex) < dimensions);
        if (bIsValidhistoryPixel)
        {
            motionReprojected[currentPixelIndex] = mtss_float4(motionTopCaliberated, motionTipCaliberated);
        }
    }
}
