// Copyright (c) 2023 Moore Threads Technology Co. Ltd. All rights reserved.
#include "mtss_common.hlsli"

Texture2D<float3> colorTextureTip;
Texture2D<float3> colorTextureTop;
Texture2D<float> depthTextureTip;
Texture2D<float> depthTextureTop;

Texture2D<float4> reprojectedTip;
Texture2D<float4> reprojectedTop;

RWTexture2D<float4> outputTexture;

cbuffer shaderConsts : register(b0)
{
    uint2 dimensions;
    float2 smoothing;
    float2 viewportSize;
    float2 viewportInv;
};

#define TILE_SIZE 8

[shader("compute")]
[numthreads(TILE_SIZE, TILE_SIZE, 1)]
void main(uint2 groupId : SV_GroupID, uint2 localId : SV_GroupThreadID, uint groupThreadIndex : SV_GroupIndex)
{
    uint2 dispatchThreadId = localId + groupId * uint2(TILE_SIZE, TILE_SIZE);
    int2 currentPixelIndex = dispatchThreadId;
	/*float2 viewportUV = (float2(currentPixelIndex) + 0.5f) * HistoryInfo_ViewportSizeInverse;
	float2 screenPos = ViewportUVToScreenPos(viewportUV);*/
	mtss_float4 tip = reprojectedTip[currentPixelIndex];
	mtss_float4 top = reprojectedTop[currentPixelIndex];
	
	mtss_float3 tipSample = tip.rgb;
	mtss_float tipDepth = tip.a;
	mtss_float3 topSample = top.rgb;
	mtss_float topDepth = top.a;
	
    int isTipVisible = (all(tipSample == mtss_float3(0.0f, 0.0f, 0.0f)) && tipDepth == 0.0f) ? 0 : 1;
    int isTopVisible = (all(topSample == mtss_float3(0.0f, 0.0f, 0.0f)) && topDepth == 0.0f) ? 0 : 1;
	mtss_float3 finalSample = mtss_float3(0.0f, 0.0f, 0.0f);
    if (isTipVisible == 1 && isTopVisible == 1)
    {
        if (tipDepth < topDepth)
        {
            finalSample = topSample;
        }
        else
        {
            finalSample = tipSample;
        }
		//finalSample = mtss_float3(1.0f, 0.0f, 0.0f);
    }
    else if (isTipVisible == 1)
    {
        finalSample = tipSample;
		//finalSample = mtss_float3(0.0f, 1.0f, 0.0f);
    }
    else if (isTopVisible == 1)
    {
        finalSample = topSample;
		//finalSample = mtss_float3(0.0f, 0.0f, 1.0f);
    }
    else
    {
		mtss_float tipDepthDist = depthTextureTip[currentPixelIndex];
		mtss_float topDepthDist = depthTextureTop[currentPixelIndex];
		mtss_float3 tipColorValue = colorTextureTip[currentPixelIndex];
		mtss_float3 topColorValue = colorTextureTop[currentPixelIndex];
			
		mtss_float depthAlpha = topDepthDist * SafeRcp(tipDepthDist + topDepthDist);
        finalSample = lerp(tipColorValue, topColorValue, depthAlpha);
		//finalSample = mtss_float3(0.0f, 1.0f, 1.0f);
    }
	
	{
        bool bIsValidhistoryPixel = all(uint2(currentPixelIndex) < dimensions);
        if (bIsValidhistoryPixel)
        {
            outputTexture[currentPixelIndex] = float4(finalSample, 1.0f);
        }
    }
}