
Texture2D<float4> colorTextureTip;
Texture2D<float4> colorTextureTop;
Texture2D<float> depthTextureTip;
Texture2D<float> depthTextureTop;

Texture2D<float4> motionVector;

RWTexture2D<float4> reprojectedTip;
RWTexture2D<float4> reprojectedTop;

cbuffer shaderConsts : register(b0)
{
    float4x4 prevClipToClip;
    float4x4 clipToPrevClip;
};

#define TILE_SIZE 8

[shader("compute")]
[numthreads(TILE_SIZE, TILE_SIZE, 1)]
void main(uint2 groupId : SV_GroupID, uint2 localId : SV_GroupThreadID, uint groupThreadIndex : SV_GroupIndex)
{
    
}