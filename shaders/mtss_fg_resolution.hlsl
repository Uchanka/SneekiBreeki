// Copyright (c) 2023 Moore Threads Technology Co. Ltd. All rights reserved.
#include "mtss_common.hlsli"

Texture2D<float3> colorTextureTip;
Texture2D<float> depthTextureTip;
Texture2D<float3> colorTextureTop;
Texture2D<float> depthTextureTop;

Texture2D<float4> motionUnprojected;
Texture2D<float4> motionReprojected;

RWTexture2D<float4> outputTexture;

cbuffer shaderConsts : register(b0)
{
    uint2 dimensions;
    float2 tipTopDistance;
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
	mtss_float4 motionVector = motionReprojected[currentPixelIndex];
#endif
#ifdef NVRHI_DONUT_COORDINATES
    mtss_float4 motionVector = motionReprojected[currentPixelIndex] * float4(viewportInv, viewportInv);
#endif
    float2 velocityTipCombined = motionVector.zw;
    float2 velocityTopCombined = motionVector.xy;
	
    int isTipVisible = any(velocityTipCombined == ImpossibleMotionVecValue) ? 0 : 1;
    int isTopVisible = any(velocityTopCombined == ImpossibleMotionVecValue) ? 0 : 1;
    
    const float distanceTip = tipTopDistance.x;
    const float distanceTop = tipTopDistance.y;
	
    float2 tipTranslation = velocityTipCombined * distanceTip;
    float2 topTranslation = velocityTopCombined * distanceTop;
	
#ifdef UNREAL_ENGINE_COORDINATES
    float2 tipTracedScreenPos = screenPos - tipTranslation;
    float2 topTracedScreenPos = screenPos + topTranslation;
#endif
#ifdef NVRHI_DONUT_COORDINATES
    float2 tipTracedScreenPos = screenPos + tipTranslation;
    float2 topTracedScreenPos = screenPos - topTranslation;
#endif

#ifdef UNREAL_ENGINE_COORDINATES
    float2 sampleUVTip = ScreenPosToViewportUV(tipTracedScreenPos);
    sampleUVTip = clamp(sampleUVTip, float2(0.0f, 0.0f), float2(1.0f, 1.0f));
    float2 sampleUVTop = ScreenPosToViewportUV(topTracedScreenPos);
    sampleUVTop = clamp(sampleUVTop, float2(0.0f, 0.0f), float2(1.0f, 1.0f));
#endif
#ifdef NVRHI_DONUT_COORDINATES
    float2 sampleUVTip = tipTracedScreenPos;
    sampleUVTip = clamp(sampleUVTip, float2(0.0f, 0.0f), float2(1.0f, 1.0f));
    float2 sampleUVTop = topTracedScreenPos;
    sampleUVTop = clamp(sampleUVTop, float2(0.0f, 0.0f), float2(1.0f, 1.0f));
#endif
	
	float3 tipSample = colorTextureTip.SampleLevel(bilinearMirroredSampler, sampleUVTip, 0);
    float tipDepth = depthTextureTip.SampleLevel(bilinearMirroredSampler, sampleUVTip, 0);
    float3 topSample = colorTextureTop.SampleLevel(bilinearMirroredSampler, sampleUVTop, 0);
    float topDepth = depthTextureTop.SampleLevel(bilinearMirroredSampler, sampleUVTop, 0);
	
    float3 finalSample = float3(0.0f, 0.0f, 0.0f);
    if (isTipVisible == 1 && isTopVisible == 1)
    {
        finalSample = tipDepth > topDepth ? tipSample : topSample;
    }
    else if (isTipVisible == 1)
    {
        finalSample = tipSample;
    }
    else if (isTopVisible == 1)
    {
        finalSample = topSample;
    }
    else
    {
		float tipDepthDist = depthTextureTip[currentPixelIndex];
		float topDepthDist = depthTextureTop[currentPixelIndex];
        float3 tipColorValue = colorTextureTip[currentPixelIndex];
        float3 topColorValue = colorTextureTop[currentPixelIndex];
		
        finalSample = tipDepthDist > topDepthDist ? tipColorValue : topColorValue;
    }
	
	{
        bool bIsValidhistoryPixel = all(uint2(currentPixelIndex) < viewportSize);
        if (bIsValidhistoryPixel)
        {
            outputTexture[currentPixelIndex] = float4(tipSample, 1.0f);
        }
    }
}