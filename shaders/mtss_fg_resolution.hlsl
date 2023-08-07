Texture2D<float4> colorTextureTip;
Texture2D<float4> colorTextureTop;
Texture2D<float> depthTextureTip;
Texture2D<float> depthTextureTop;

Texture2D<float4> reprojectedTip;
Texture2D<float4> reprojectedTop;

RWTexture2D<float4> outputTexture;

#define TILE_SIZE 8

[shader("compute")]
[numthreads(TILE_SIZE, TILE_SIZE, 1)]
void main(uint2 groupId : SV_GroupID, uint2 localId : SV_GroupThreadID, uint groupThreadIndex : SV_GroupIndex)
{
    uint2 dispatchThreadId = localId + groupId * uint2(TILE_SIZE, TILE_SIZE);
    int2 currentPixelIndex = dispatchThreadId;

    outputTexture[currentPixelIndex] = float4(0.0, 0.0, 0.0, 1.0);
}