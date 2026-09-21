#if !defined(DFTILEDLIGHTING_TILE_CULL_GROUP_DIM) || DFTILEDLIGHTING_TILE_CULL_GROUP_DIM != 10
#error "DFTILEDLIGHTING_TILE_CULL_GROUP_DIM must be 10 for section-12 key 3"
#endif

cbuffer TileCullParameters : register(b0)
{
    float4 DepthClamp;
    float4 SlopeOrigin;
    float4 CullCounts;
    float4 SlopeStep;
};

struct TileDepthBounds
{
    float Minimum;
    float Maximum;
};

struct LightRecord
{
    float Unread0;
    float4 PositionRadius;
    float4 Unread20;
    float3 Unread36;
};

struct TileLightList
{
    uint Count;
    uint Indices[127];
};

StructuredBuffer<TileDepthBounds> TileBounds : register(t5);
StructuredBuffer<LightRecord> Lights : register(t6);
RWStructuredBuffer<TileLightList> TileLists : register(u7);

groupshared uint VisibleCount;

[numthreads(DFTILEDLIGHTING_TILE_CULL_GROUP_DIM, DFTILEDLIGHTING_TILE_CULL_GROUP_DIM, 1)]
void main(
    uint groupIndex : SV_GroupIndex,
    uint3 groupId : SV_GroupID,
    uint3 groupThreadId : SV_GroupThreadID)
{
    if (groupIndex == 0)
    {
        VisibleCount = 0;
    }
    GroupMemoryBarrierWithGroupSync();

    uint2 tileCount = (uint2)CullCounts.zw;
    int tileIndex = groupId.y * tileCount.x + groupId.x;
    int lightIndex =
        groupThreadId.y * DFTILEDLIGHTING_TILE_CULL_GROUP_DIM + groupThreadId.x;

    if (float(lightIndex) < CullCounts.x)
    {
        float4 light = Lights[lightIndex].PositionRadius;
        TileDepthBounds bounds = TileBounds[tileIndex];
        float nearBound = max(bounds.Minimum, DepthClamp.x);
        float farBound = min(bounds.Maximum, DepthClamp.y);
        bool visible =
            (light.z - nearBound >= -light.w) &&
            (farBound - light.z >= -light.w);

        [branch]
        if (visible)
        {
            int row = tileCount.y - groupId.y - 1;
            int column = groupId.x;
            float columnEdge = float(column);
            float4 slope;
            slope.x = columnEdge * SlopeStep.z + SlopeOrigin.x;
            slope.y = (columnEdge + 1.0) * SlopeStep.z + SlopeOrigin.x;
            float rowEdge = float(row);
            slope.z = (rowEdge + 1.0) * SlopeStep.w + SlopeOrigin.w;
            slope.w = rowEdge * SlopeStep.w + SlopeOrigin.w;
            float4 scale = rsqrt(slope * slope + 1.0);
            float4 planeZScale;
            planeZScale.xw = scale.xw;
            planeZScale.y = 1.0;
            float4 planeZ = slope * planeZScale.xyyw;
            planeZScale.xw = -1.0;
            planeZScale.yz = scale.yz;
            planeZ = planeZ.xzyw * planeZScale.xzyw;
            float4 planeX =
                scale * float4(1.0, -1.0, -1.0, 1.0);
            float2 leftPlane = float2(planeX.x, planeZ.x);
            float2 rightPlane = float2(planeX.y, planeZ.z);
            float2 topPlane = float2(planeX.z, planeZ.y);
            float2 bottomPlane = float2(planeX.w, planeZ.w);

            [branch]
            if (visible && dot(leftPlane, light.xz) < -light.w)
            {
                visible = false;
            }
            [branch]
            if (visible && dot(rightPlane, light.xz) < -light.w)
            {
                visible = false;
            }
            [branch]
            if (visible && dot(topPlane, light.yz) < -light.w)
            {
                visible = false;
            }
            [branch]
            if (visible && dot(bottomPlane, light.yz) < -light.w)
            {
                visible = false;
            }
        }
        else
        {
            visible = false;
        }

        if (visible)
        {
            uint slot;
            InterlockedAdd(VisibleCount, 1, slot);
            TileLists[tileIndex].Indices[slot] = lightIndex;
        }
    }

    GroupMemoryBarrierWithGroupSync();
    if (groupIndex == 0)
    {
        TileLists[tileIndex].Count = VisibleCount;
    }
}
