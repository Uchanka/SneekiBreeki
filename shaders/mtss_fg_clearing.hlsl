
RWTexture2D<float4> reprojectedTip;
RWTexture2D<float4> reprojectedTop;

#define TILE_SIZE 8

[shader("compute")]
[numthreads(TILE_SIZE, TILE_SIZE, 1)]
void main(uint2 groupId : SV_GroupID, uint2 localId : SV_GroupThreadID, uint groupThreadIndex : SV_GroupIndex)
{
    
}