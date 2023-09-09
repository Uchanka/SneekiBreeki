// Copyright (c) 2023 Moore Threads Technology Co. Ltd. All rights reserved.
#include "mtss_common.hlsli"

//------------------------------------------------------- PARAMETERS
Texture2D<float2> motionVectorFiner;
Texture2D<float2> motionVectorCoarser;
Texture2D<float> motionReliabilityFiner;
Texture2D<float> motionReliabilityCoarser;

RWTexture2D<float2> motionVectorFinerUAV;

cbuffer shaderConsts : register(b0)
{
    uint2 FinerDimension;
    uint2 CoarserDimension;
}

#define TILE_SIZE 8

//------------------------------------------------------- ENTRY POINT
[shader("compute")]
[numthreads(TILE_SIZE, TILE_SIZE, 1)]
void main(uint2 groupId : SV_GroupID, uint2 localId : SV_GroupThreadID, uint groupThreadIndex : SV_GroupIndex)
{
    uint2 dispatchThreadId = localId + groupId * uint2(TILE_SIZE, TILE_SIZE);
    int2 finerPixelIndex = dispatchThreadId;
    int2 coarserPixelIndex = finerPixelIndex / 2;
	
	float finerReliability = motionReliabilityFiner[finerPixelIndex];
	float coarserReliability = motionReliabilityCoarser[coarserPixelIndex];
	
	float2 selectedVector = 0.0f;
    if (finerReliability == 0.0f)
    {
        selectedVector = motionVectorCoarser[coarserPixelIndex];
    }
    else
    {
        selectedVector = motionVectorFiner[finerPixelIndex];
    }
	
	{
        bool bIsValidhistoryPixel = all(uint2(finerPixelIndex) < FinerDimension);
        if (bIsValidhistoryPixel)
        {
            motionVectorFinerUAV[finerPixelIndex] = selectedVector;
        }
    }
}
