// Copyright (c) 2023 Moore Threads Technology Co. Ltd. All rights reserved.
#include "mtss_common.hlsli"

Texture2D<float3> colorTextureTip;
Texture2D<float> depthTextureTip;
Texture2D<float3> colorTextureTop;
Texture2D<float> depthTextureTop;

Texture2D<float2> currMotionUnprojected;

Texture2D<float2> motionReprojectedFull;
Texture2D<float2> motionReprojectedHalfTip;
Texture2D<float2> motionReprojectedHalfTop;

//Texture2D<float4> uiColorTexture;

RWTexture2D<float4> outputTexture;

cbuffer shaderConsts : register(b0)
{
    uint2 dimensions;
    float2 tipTopDistance;
    float2 viewportSize;
    float2 viewportInv;
};

SamplerState bilinearClampedSampler : register(s0);

#define TILE_SIZE 8

static float3 debugRed = float3(1.0f, 0.0f, 0.0f);
static float3 debugGreen = float3(0.0f, 1.0f, 0.0f);
static float3 debugBlue = float3(0.0f, 0.0f, 1.0f);
static float3 debugYellow = float3(1.0f, 1.0f, 0.0f);
static float3 debugMagenta = float3(1.0f, 0.0f, 1.0f);
static float3 debugCyan = float3(0.0f, 1.0f, 1.0f);

[shader("compute")]
[numthreads(TILE_SIZE, TILE_SIZE, 1)]
void main(uint2 groupId : SV_GroupID, uint2 localId : SV_GroupThreadID, uint groupThreadIndex : SV_GroupIndex)
{
    uint2 dispatchThreadId = localId + groupId * uint2(TILE_SIZE, TILE_SIZE);
    int2 currentPixelIndex = dispatchThreadId;
    float2 pixelCenter = float2(currentPixelIndex) + 0.5f;
    float2 viewportUV = pixelCenter * viewportInv;
    float2 screenPos = viewportUV;
    
    float2 velocityFull = motionReprojectedFull[currentPixelIndex];
    float2 velocityHalfTip = motionReprojectedHalfTip[currentPixelIndex];
    float2 velocityHalfTop = motionReprojectedHalfTop[currentPixelIndex];

    bool isFullWritten = all(abs(velocityFull) < viewportInv) ? false : true;
    bool isHalfTipWritten = all(abs(velocityHalfTip) < viewportInv) ? false : true;
    bool isHalfTopWritten = all(abs(velocityHalfTop) < viewportInv) ? false : true;

    bool isTopInvisible = any(velocityHalfTop >= ImpossibleMotionValue) ? true : false;
    bool isTopVisible = !isTopInvisible;

    if (isTopInvisible)
    {
        velocityHalfTop -= float2(ImpossibleMotionOffset, ImpossibleMotionOffset);
    }

    bool isTipInvisible = (!isHalfTipWritten && isFullWritten) ? true : false;
    bool isTipVisible = !isTipInvisible;

    const float distanceTip = tipTopDistance.x;
    const float distanceTop = tipTopDistance.y;

    float2 halfTipTranslation = distanceTip * velocityHalfTip;
    float2 halfTopTranslation = distanceTop * velocityHalfTop;

    float2 tipTracedScreenPos = screenPos + halfTipTranslation;
    float2 topTracedScreenPos = screenPos - halfTopTranslation;

    float2 sampleUVTip = tipTracedScreenPos;
    sampleUVTip = clamp(sampleUVTip, float2(0.0f, 0.0f), float2(1.0f, 1.0f));
    float2 sampleUVTop = topTracedScreenPos;
    sampleUVTop = clamp(sampleUVTop, float2(0.0f, 0.0f), float2(1.0f, 1.0f));
	
    float3 tipSample = colorTextureTip.SampleLevel(bilinearClampedSampler, sampleUVTip, 0);
    float tipDepth = depthTextureTip.SampleLevel(bilinearClampedSampler, sampleUVTip, 0);
    float3 topSample = colorTextureTop.SampleLevel(bilinearClampedSampler, sampleUVTop, 0);
    float topDepth = depthTextureTop.SampleLevel(bilinearClampedSampler, sampleUVTop, 0);
	
    float3 finalSample = float3(0.0f, 0.0f, 0.0f);
    if (isTopVisible)
    {
        finalSample = topSample;
#ifdef DEBUG_COLORS
        finalSample = debugYellow;
#endif
    }
    else if (isTipVisible)
    {
        finalSample = tipSample;
#ifdef DEBUG_COLORS
        finalSample = debugBlue;
#endif
    }
    else
    {
        float2 velocityAdvection = currMotionUnprojected[currentPixelIndex] * viewportInv;

        float2 advTipTranslation = distanceTip * velocityHalfTip;
        float2 advTopTranslation = distanceTop * velocityAdvection;

        float2 tipAdvectedScreenPos = screenPos + advTipTranslation;
        float2 topAdvectedScreenPos = screenPos - advTopTranslation;

        float2 sampleAdvUVTip = tipAdvectedScreenPos;
        sampleAdvUVTip = clamp(sampleAdvUVTip, float2(0.0f, 0.0f), float2(1.0f, 1.0f));
        float2 sampleAdvUVTop = topAdvectedScreenPos;
        sampleAdvUVTop = clamp(sampleAdvUVTop, float2(0.0f, 0.0f), float2(1.0f, 1.0f));

        float3 tipAdvSample = colorTextureTip.SampleLevel(bilinearClampedSampler, sampleAdvUVTip, 0);
        float tipAdvDepth = depthTextureTip.SampleLevel(bilinearClampedSampler, sampleAdvUVTip, 0);
        float3 topAdvSample = colorTextureTop.SampleLevel(bilinearClampedSampler, sampleAdvUVTop, 0);
        float topAdvDepth = depthTextureTop.SampleLevel(bilinearClampedSampler, sampleAdvUVTop, 0);

        float3 finalSampleAdv = lerp(tipAdvSample, topAdvSample, tipAdvDepth * SafeRcpRet1(tipAdvDepth + topAdvDepth));

        finalSample = finalSampleAdv;
#ifdef DEBUG_COLORS
        finalSample = debugCyan;
#endif
    }
	
	{
        bool bIsValidhistoryPixel = all(uint2(currentPixelIndex) < viewportSize);
        if (bIsValidhistoryPixel)
        {
            //float4 uiColorBlendingIn = uiColorTexture[currentPixelIndex];
            //float3 finalOutputColor = lerp(finalSample, uiColorBlendingIn.rgb, uiColorBlendingIn.a);
            outputTexture[currentPixelIndex] = float4(finalSample, 1.0f);
            //outputTexture[currentPixelIndex] = float4(motionUnprojected[currentPixelIndex], motionUnprojected[currentPixelIndex]);
            //outputTexture[currentPixelIndex] = float4(velocityTopCombined, velocityTipCombined) * 10.0f;
        }
    }
}