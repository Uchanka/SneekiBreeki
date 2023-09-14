#include "mtss_common.hlsli"

//------------------------------------------------------- PARAMETERS
Texture2D<float2> currMotionVector;

//Texture2D<float> depthTextureTip;
Texture2D<float> depthTextureTop;

RWTexture2D<uint> motionReprojFullX;
RWTexture2D<uint> motionReprojFullY;
RWTexture2D<uint> motionReprojHalfTipX;
RWTexture2D<uint> motionReprojHalfTipY;
RWTexture2D<uint> motionReprojHalfTopX;
RWTexture2D<uint> motionReprojHalfTopY;

cbuffer shaderConsts : register(b0)
{
    float4x4 prevClipToClip;
    float4x4 clipToPrevClip;
    
    uint2 dimensions;
    float2 tipTopDistance;
    float2 viewportSize;
    float2 viewportInv;
};

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
    float2 motionVector = currMotionVector.SampleLevel(bilinearClampedSampler, viewportUV, 0) * viewportInv;

    const float distanceFull = tipTopDistance.x + tipTopDistance.y;
    const float distanceHalfTip = tipTopDistance.x;
    const float distanceHalfTop = tipTopDistance.y;
	
    float2 velocityCombined = motionVector.xy;

    float2 fullTranslation = velocityCombined * distanceFull;
    float2 fullTracedScreenPos = screenPos + fullTranslation;
    int2 fullTracedIndex = floor(fullTracedScreenPos * viewportSize);
    float2 fullTracedFloatCenter = float2(fullTracedIndex) + float2(0.5f, 0.5f);
    float2 fullTracedPos = fullTracedFloatCenter * viewportInv;
    float2 samplePosFull = fullTracedPos - fullTranslation;
    float2 sampleUVFull = samplePosFull;
    sampleUVFull = clamp(sampleUVFull, float2(0.0f, 0.0f), float2(1.0f, 1.0f));
    float fullDepth = depthTextureTop.SampleLevel(bilinearClampedSampler, sampleUVFull, 0);
    uint fullDepthAsUIntHigh19 = compressDepth(fullDepth);


    float2 halfTipTranslation = velocityCombined * distanceHalfTip;
    float2 halfTipTracedScreenPos = screenPos + fullTranslation - halfTipTranslation;
    int2 halfTipTracedIndex = floor(halfTipTracedScreenPos * viewportSize);
    float2 halfTipTracedFloatCenter = float2(halfTipTracedIndex) + float2(0.5f, 0.5f);
    float2 halfTipTracedPos = halfTipTracedFloatCenter * viewportInv;
    float2 samplePosHalfTip = halfTipTracedPos + halfTipTranslation;
    float2 sampleUVHalfTip = samplePosHalfTip;
    sampleUVHalfTip = clamp(sampleUVHalfTip, float2(0.0f, 0.0f), float2(1.0f, 1.0f));
    float halfTipDepth = depthTextureTop.SampleLevel(bilinearClampedSampler, sampleUVHalfTip, 0);
    uint halfTipDepthAsUIntHigh19 = compressDepth(halfTipDepth);


    float2 halfTopTranslation = velocityCombined * distanceHalfTop;
    float2 halfTopTracedScreenPos = screenPos + halfTopTranslation;
    int2 halfTopTracedIndex = floor(halfTopTracedScreenPos * viewportSize);
    float2 halfTopTracedFloatCenter = float2(halfTopTracedIndex) + float2(0.5f, 0.5f);	
    float2 halfTopTracedPos = halfTopTracedFloatCenter * viewportInv;
    float2 samplePosHalfTop = halfTopTracedPos - halfTopTranslation;
    float2 sampleUVHalfTop = samplePosHalfTop;
    sampleUVHalfTop = clamp(sampleUVHalfTop, float2(0.0f, 0.0f), float2(1.0f, 1.0f));
    float halfTopDepth = depthTextureTop.SampleLevel(bilinearClampedSampler, sampleUVHalfTop, 0);
    uint halfTopDepthAsUIntHigh19 = compressDepth(halfTopDepth);
	
    uint packedAsUINTHigh19FullX = fullDepthAsUIntHigh19 | (currentPixelIndex.x & IndexLast13DigitsMask);
    uint packedAsUINTHigh19FullY = fullDepthAsUIntHigh19 | (currentPixelIndex.y & IndexLast13DigitsMask);
    uint packedAsUINTHigh19HalfTipX = halfTipDepthAsUIntHigh19 | (currentPixelIndex.x & IndexLast13DigitsMask);
    uint packedAsUINTHigh19HalfTipY = halfTipDepthAsUIntHigh19 | (currentPixelIndex.y & IndexLast13DigitsMask);
    uint packedAsUINTHigh19HalfTopX = halfTopDepthAsUIntHigh19 | (currentPixelIndex.x & IndexLast13DigitsMask);
    uint packedAsUINTHigh19HalfTopY = halfTopDepthAsUIntHigh19 | (currentPixelIndex.y & IndexLast13DigitsMask);
	
	{
        bool bIsValidFullPixel = all(fullTracedIndex < int2(dimensions)) && all(fullTracedIndex >= int2(0, 0));
        if (bIsValidFullPixel)
        {
            uint originalValX;
            uint originalValY;

            InterlockedMax(motionReprojFullX[fullTracedIndex], packedAsUINTHigh19FullX, originalValX);
            InterlockedMax(motionReprojFullY[fullTracedIndex], packedAsUINTHigh19FullY, originalValY);
        }
        bool bIsValidHalfTipPixel = all(halfTipTracedIndex < int2(dimensions)) && all(halfTipTracedIndex >= int2(0, 0));
        if (bIsValidHalfTipPixel)
        {
            uint originalValX;
            uint originalValY;

            InterlockedMax(motionReprojHalfTipX[halfTipTracedIndex], packedAsUINTHigh19HalfTipX, originalValX);
            InterlockedMax(motionReprojHalfTipY[halfTipTracedIndex], packedAsUINTHigh19HalfTipY, originalValY);
        }
        bool bIsValidHalfTopPixel = all(halfTopTracedIndex < int2(dimensions)) && all(halfTopTracedIndex >= int2(0, 0));
        if (bIsValidHalfTopPixel)
        {
            uint originalValX;
            uint originalValY;

            InterlockedMax(motionReprojHalfTopX[halfTopTracedIndex], packedAsUINTHigh19HalfTopX, originalValX);
            InterlockedMax(motionReprojHalfTopY[halfTopTracedIndex], packedAsUINTHigh19HalfTopY, originalValY);
        }
    }
}