// Copyright (c) 2023 Moore Threads Technology Co. Ltd. All rights reserved.
#include "mtss_common.hlsli"

//------------------------------------------------------- PARAMETERS
Texture2D<mtss_float4> motionVectorFiner;
Texture2D<mtss_float4> motionVectorCoarser;
Texture2D<mtss_float> motionReliabilityFiner;
Texture2D<mtss_float> motionReliabilityCoarser;

RWTexture2D<mtss_float4> motionVectorFinerUAV;

cbuffer shaderConsts : register(b0)
{
    uint2 finerDimension;
    uint2 coarserDimension;
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
	
	mtss_float finerReliability = motionReliabilityFiner[finerPixelIndex];
	mtss_float coarserReliability = motionReliabilityCoarser[coarserPixelIndex];
	
	mtss_float4 selectedVector = 0.0f;
    if (finerReliability == 0.0f)
    {
        selectedVector = motionVectorCoarser[coarserPixelIndex];
    }
    else
    {
        selectedVector = motionVectorFiner[finerPixelIndex];
    }
	
	{
        bool bIsValidhistoryPixel = all(uint2(finerPixelIndex) < finerDimension);
        if (bIsValidhistoryPixel)
        {
            motionVectorFinerUAV[finerPixelIndex] = selectedVector;
        }
    }
}
