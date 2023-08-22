// Copyright (c) 2023 Moore Threads Technology Co. Ltd. All rights reserved.
#include "mtss_common.hlsli"

//------------------------------------------------------- PARAMETERS
Texture2D<mtss_float4> motionVectorFiner;

RWTexture2D<mtss_float4> motionVectorCoarser;
RWTexture2D<mtss_float> motionReliability; //This is coarse too

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
    int2 coarserPixelIndex = dispatchThreadId;
	
    int2 finerPixelUpperLeft = 2 * coarserPixelIndex;
	mtss_float4 filteredVector = 0.0f;
	mtss_float perPixelWeight = 0.0f;
	{
        for (int i = 0; i < subsampleCount4PointTian; ++i)
        {
            int2 finerIndex = finerPixelUpperLeft + subsamplePixelOffset4PointTian[i];
			mtss_float4 finerVector = motionVectorFiner[finerIndex];
			mtss_float validity = all(finerVector == 0.0f) ? 0.0f : 1.0f;
            filteredVector += finerVector;
            perPixelWeight += validity;
        }
		mtss_float normalization = SafeRcp(mtss_float(subsampleCount4PointTian));
        filteredVector *= normalization;
        perPixelWeight *= normalization;
    }
	
	{
        bool bIsValidhistoryPixel = all(uint2(coarserPixelIndex) < coarserDimension);
        if (bIsValidhistoryPixel)
        {
            motionVectorCoarser[coarserPixelIndex] = filteredVector;
            motionReliability[coarserPixelIndex] = perPixelWeight;
        }
    }
}
