// Copyright (c) 2023 Moore Threads Technology Co. Ltd. All rights reserved.
#include "mtss_common.hlsli"

Texture2D<float3> colorTextureTip;
Texture2D<float3> colorTextureTop;
Texture2D<float> depthTextureTip;
Texture2D<float> depthTextureTop;

Texture2D<float2> motionVector;

RWTexture2D<float4> reprojectedTip;
RWTexture2D<float4> reprojectedTop;

//#define UNREAL_ENGINE_COORDINATES
#define NVRHI_DONUT_COORDINATES

cbuffer shaderConsts : register(b0)
{
    float4x4 prevClipToClip;
    float4x4 clipToPrevClip;
    
    uint2 dimensions;
    float2 smoothing;
    float2 viewportSize;
    float2 viewportInv;
};

SamplerState bilinearMirroredSampler : register(s0);

#define TILE_SIZE 8

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
    
#ifdef UNREAL_ENGINE_COORDINATES
    float2 motionVectorDecoded = motionVector[currentPixelIndex];
    //float2 motionStaticTip = ComputeStaticVelocityTipTop(screenPos, depthTextureTip[currentPixelIndex], prevClipToClip);
    //float2 motionStaticTop = ComputeStaticVelocityTopTip(screenPos, depthTextureTop[currentPixelIndex], clipToPrevClip);
#endif
#ifdef NVRHI_DONUT_COORDINATES
    //Bruh... Streamline doesn't differentiate static and non-static velocities
    float2 motionVectorDecoded = motionVector[currentPixelIndex] * viewportInv;
#endif
    float2 velocityTipCombined = motionVectorDecoded;
    float2 velocityTopCombined = motionVectorDecoded;

    //What if we need to interpolate multiple frames?
    const float interpolatedFrames = 1.0f;
    const float currentInterpol = 0.0f;
    const float distanceTip = (currentInterpol + 1.0f) / (interpolatedFrames + 1.0f);
    const float distanceTop = 1.0f - distanceTip;
	
    float2 tipTranslation = velocityTipCombined * distanceTip;
    float2 topTranslation = velocityTopCombined * distanceTop;
	
#ifdef UNREAL_ENGINE_COORDINATES
    float2 tipTracedScreenPos = screenPos + tipTranslation;
    float2 topTracedScreenPos = screenPos - topTranslation;
	
    int2 tipTracedIndex = floor(ScreenPosToViewportUV(tipTracedScreenPos) * viewportSize);
    float2 tipTracedFloatCenter = float2(tipTracedIndex) + float2(0.5f, 0.5f);
    int2 topTracedIndex = floor(ScreenPosToViewportUV(topTracedScreenPos) * viewportSize);
    float2 topTracedFloatCenter = float2(topTracedIndex) + float2(0.5f, 0.5f);
	
    float2 tipTracedPos = ViewportUVToScreenPos(tipTracedFloatCenter * viewportInv);
    float2 topTracedPos = ViewportUVToScreenPos(topTracedFloatCenter * viewportInv);
    
    float2 samplePosTip = tipTracedPos - tipTranslation;
    float2 samplePosTop = topTracedPos + topTranslation;
	
    float2 sampleUVTip = ScreenPosToViewportUV(samplePosTip);
    sampleUVTip = clamp(sampleUVTip, float2(0.0f, 0.0f), float2(1.0f, 1.0f));
    float2 sampleUVTop = ScreenPosToViewportUV(samplePosTop);
    sampleUVTop = clamp(sampleUVTop, float2(0.0f, 0.0f), float2(1.0f, 1.0f));
#endif
#ifdef NVRHI_DONUT_COORDINATES
    float2 tipTracedScreenPos = screenPos - tipTranslation;
    float2 topTracedScreenPos = screenPos + topTranslation;
	
    int2 tipTracedIndex = floor(tipTracedScreenPos * viewportSize);
    float2 tipTracedFloatCenter = float2(tipTracedIndex) + float2(0.5f, 0.5f);
    int2 topTracedIndex = floor(topTracedScreenPos * viewportSize);
    float2 topTracedFloatCenter = float2(topTracedIndex) + float2(0.5f, 0.5f);
	
    float2 tipTracedPos = tipTracedFloatCenter * viewportInv;
    float2 topTracedPos = topTracedFloatCenter * viewportInv;
    
    float2 samplePosTip = tipTracedPos + tipTranslation;
    float2 samplePosTop = topTracedPos - topTranslation;
	
    float2 sampleUVTip = samplePosTip;
    sampleUVTip = clamp(sampleUVTip, float2(0.0f, 0.0f), float2(1.0f, 1.0f));
    float2 sampleUVTop = samplePosTop;
    sampleUVTop = clamp(sampleUVTop, float2(0.0f, 0.0f), float2(1.0f, 1.0f));
#endif
	
	mtss_float3 tipSample = colorTextureTip.SampleLevel(bilinearMirroredSampler, sampleUVTip, 0);
	mtss_float3 topSample = colorTextureTop.SampleLevel(bilinearMirroredSampler, sampleUVTop, 0);
	mtss_float tipDepth = depthTextureTip.SampleLevel(bilinearMirroredSampler, sampleUVTip, 0);
	mtss_float topDepth = depthTextureTop.SampleLevel(bilinearMirroredSampler, sampleUVTop, 0);
	
	{
        bool bIsValidTipPixel = all(tipTracedIndex < int2(dimensions)) && all(tipTracedIndex >= int2(0, 0));
        if (bIsValidTipPixel)
        {
            reprojectedTip[tipTracedIndex] = mtss_float4(tipSample, tipDepth);
        }
        bool bIsValidTopPixel = all(topTracedIndex < int2(dimensions)) && all(topTracedIndex >= int2(0, 0));
        if (bIsValidTopPixel)
        {
            reprojectedTop[topTracedIndex] = mtss_float4(topSample, topDepth);
        }
    }
}