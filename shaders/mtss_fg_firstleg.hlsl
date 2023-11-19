// Copyright (c) 2023 Moore Threads Technology Co. Ltd. All rights reserved.
#include "mtss_common.hlsli"

//------------------------------------------------------- PARAMETERS
Texture2D<float2> motionVectorFiner;

RWTexture2D<float2> motionVectorCoarser;
RWTexture2D<float> motionReliability; //This is coarse too

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
    int2 coarserPixelIndex = dispatchThreadId;
	
    int2 finerPixelUpperLeft = 2 * coarserPixelIndex;
	float2 filteredVector = 0.0f;
	float perPixelWeight = 0.0f;
	{
        for (int i = 0; i < subsampleCount4PointTian; ++i)
        {
            int2 finerIndex = finerPixelUpperLeft + subsamplePixelOffset4PointTian[i];
			float2 finerVector = motionVectorFiner[finerIndex];
            float validity = all(abs(finerVector) < (1.0f / float2(FinerDimension))) ? 0.0f : 1.0f;
            if (any(finerVector >= ImpossibleMotionValue))
            {
                validity = 0.0f;
                finerVector -= float2(ImpossibleMotionOffset, ImpossibleMotionOffset);
            }
            if (all(abs(finerVector) < (1.0f / float2(FinerDimension))))
            {
                validity = 0.0f;
                finerVector = float2(0.0f, 0.0f);
            }
            filteredVector += finerVector;
            perPixelWeight += validity;
        }
		float normalization = SafeRcp(float(subsampleCount4PointTian));
        filteredVector *= normalization;
        perPixelWeight *= normalization;
    }
	
	{
        bool bIsValidhistoryPixel = all(uint2(coarserPixelIndex) < CoarserDimension);
        if (bIsValidhistoryPixel)
        {
            motionVectorCoarser[coarserPixelIndex] = filteredVector;
            motionReliability[coarserPixelIndex] = perPixelWeight;
        }
    }
}
