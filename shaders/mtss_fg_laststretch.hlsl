// Copyright (c) 2023 Moore Threads Technology Co. Ltd. All rights reserved.
#include "mtss_common.hlsli"

//------------------------------------------------------- PARAMETERS
Texture2D<float2> motionVectorFiner;
Texture2D<float2> motionVectorCoarser;
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
	
	float2 finerVector = motionVectorFiner[finerPixelIndex];
    float finerReliability = all(abs(finerVector) < (1.0f / float2(FinerDimension))) ? 0.0f : 1.0f;
    if (any(finerVector >= ImpossibleMotionValue))
    {
        finerReliability = 0.0f;
    }
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
	
    float2 populistVotedVector = 0.0f;
	{
        for (int i = 0; i < subsampleCount4PointTian; ++i)
        {
            int2 finerIndex = finerPixelIndex + subsamplePixelOffset4PointTian[i];
            float2 finerVector = motionVectorFiner[finerIndex];
            populistVotedVector += finerVector;
        }
        float normalization = SafeRcp(float(subsampleCount4PointTian));
        populistVotedVector *= normalization;
    }
	
    if (any(populistVotedVector >= ImpossibleMotionOffset / float(subsampleCount4PointTian) * 3.0f))
    {
        selectedVector += float2(ImpossibleMotionOffset, ImpossibleMotionOffset);
    }
	
	{
		bool bIsValidhistoryPixel = all(uint2(finerPixelIndex) < FinerDimension);
		if (bIsValidhistoryPixel)
		{
			motionVectorFinerUAV[finerPixelIndex] = selectedVector;
		}
	}
}
