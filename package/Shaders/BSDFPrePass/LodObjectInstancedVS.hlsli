// SPDX-License-Identifier: GPL-3.0-or-later WITH FO4-CS-Modding-Exception
// AE 1.11.221 BSDFPrePassShader LOD_OBJECT_INSTANCED vertex family.
//
// This producer fragment is intentionally not wired into the consumer root. It covers
// only the VS population selected by raw technique bit 0x00000800. The paired generic
// PS remains unverified until a live draw capture identifies it.
// This is identity infrastructure, not a gamma or LOD-blending feature.

#if !LOD_OBJECT_INSTANCED
#  error "this source requires the LOD_OBJECT_INSTANCED vertex selector"
#endif
#if !TEXTURE || !NORMALS || !BINORMAL_TANGENT
#  error "every native LOD_OBJECT_INSTANCED vertex route carries texture and tangent-frame inputs"
#endif
#if BONE_TINTING || CLIP_VOLUME || COMBINED || DISMEMBERMENT || DISMEMBERMENT_MEATCUFF \
    || EYE || FACE || GRASS || INSTANCED || LANDSCAPE || MERGE_INSTANCED \
    || MODELSPACENORMALS || PIPBOY_SCREEN || SPLINE || TESSELLATE_DISP_HEIGHT \
    || TREE_ANIM
#  error "an unsupported BSDFPrePass vertex axis reached the LOD object fragment"
#endif
#if VC != LOD_LANDSCAPE
#  error "the native LOD object family crosses VC and LOD_LANDSCAPE together"
#endif
#if SKINNED && !VC
#  error "the native skinned LOD object route also carries VC and LOD_LANDSCAPE"
#endif

cbuffer PerFrame_CB12 : register(b12)
{
    float4 cb12_idx0_view_row0;
    float4 cb12_idx1_view_row1;
    float4 cb12_idx2_view_row2;
    float4 cb12_pad_3_7[5];
    float4 cb12_idx8_transform_row0;
    float4 cb12_idx9_transform_row1;
    float4 cb12_idx10_transform_row2;
    float4 cb12_idx11_transform_row3;
    float4 cb12_pad_12_34[23];
    float4 cb12_idx35_world_offset;
    float4 cb12_idx36_previous_world_offset;
};

#if LOD_LANDSCAPE
cbuffer PerGeometry_CB0 : register(b0)
{
    float4 cb0_idx0_blend_bounds;
};
#endif

cbuffer PerMaterial_CB1 : register(b1)
{
    float4 cb1_idx0_texcoord_scale_bias;
};

cbuffer PerCall_CB2 : register(b2)
{
    float4 cb2_idx0_world_row0;
    float4 cb2_idx1_world_row1;
    float4 cb2_idx2_world_row2;
    float4 cb2_idx3_world_row3;
    float4 cb2_idx4_previous_world_row0;
    float4 cb2_idx5_previous_world_row1;
    float4 cb2_idx6_previous_world_row2;
};

#if SKINNED
cbuffer PerSkin_CB10 : register(b10)
{
    float4 cb10_bone_transform[180];
};

cbuffer PerSkin_CB9 : register(b9)
{
    float4 cb9_previous_bone_transform[180];
};
#endif

struct LodObjectTransform
{
    float4 translation_and_scale;
    float4 rotation;
};

StructuredBuffer<LodObjectTransform> g_LodObjectTransforms : register(t6);
StructuredBuffer<uint> g_LodObjectRemap : register(t7);

struct VertexInput
{
    float4 position : POSITION0;
    float2 texcoord : TEXCOORD0;
    float4 normal : NORMAL0;
    float4 binormal : BINORMAL0;
#if VC
    float4 color : COLOR0;
#endif
#if SKINNED
    float4 blend_weight : BLENDWEIGHT0;
    float4 blend_indices : BLENDINDICES0;
#endif
    uint instance_id : SV_InstanceID;
};

struct VertexOutput
{
    float4 position : SV_POSITION;
    float3 tangentRow0 : TEXCOORD0;
    float3 tangentRow1 : TEXCOORD1;
    float3 tangentRow2 : TEXCOORD2;
    float4 currentPositionAndU : TEXCOORD3;
    float4 previousPositionAndV : TEXCOORD4;
#if VC
    float4 color : COLOR0;
#endif
#if LOD_LANDSCAPE
    float2 blendCoord : TEXCOORD9;
#endif
};

#define INPUT_TEXCOORD input.texcoord
#define INPUT_NORMAL input.normal
#define INPUT_BINORMAL input.binormal
#define VERTEX_COLOR input.color
#define OUTPUT_ALPHA VERTEX_COLOR.a

float3 TransformPosition(float4 row0, float4 row1, float4 row2, float3 offset, float4 position)
{
    float3 translation = float3(row0.w - offset.x, row1.w - offset.y, row2.w - offset.z);
    return float3(
        dot(float4(row0.xyz, translation.x), position),
        dot(float4(row1.xyz, translation.y), position),
        dot(float4(row2.xyz, translation.z), position));
}

float3 ConcatenateRow(float4 row, float4 column0, float4 column1, float4 column2)
{
    return float3(dot(row, column0), dot(row, column1), dot(row, column2));
}

float2 ScaleTexcoord(float2 texcoord, float4 scaleBias)
{
    return texcoord * scaleBias.zw + scaleBias.xy;
}

float3 ProjectBasis(float3 row, float3 tangent, float3 binormal, float3 normal)
{
    return float3(dot(row, tangent), dot(row, binormal), dot(row, normal));
}

float3 RotateByQuaternion(float4 rotation, float3 direction)
{
    float3 turn = 2.0 * cross(rotation.xyz, direction);
    return direction + rotation.w * turn + cross(rotation.xyz, turn);
}

#if SKINNED
float3x4 BoneMatrix(float4 row0, float4 row1, float4 row2, float3 offset)
{
    return float3x4(
        row0 - float4(0.0, 0.0, 0.0, offset.x),
        row1 - float4(0.0, 0.0, 0.0, offset.y),
        row2 - float4(0.0, 0.0, 0.0, offset.z));
}

float3x4 CurrentBone(int index, float3 offset)
{
    return BoneMatrix(cb10_bone_transform[index], cb10_bone_transform[index + 1],
        cb10_bone_transform[index + 2], offset);
}

float3x4 PreviousBone(int index, float3 offset)
{
    return BoneMatrix(cb9_previous_bone_transform[index], cb9_previous_bone_transform[index + 1],
        cb9_previous_bone_transform[index + 2], offset);
}

float3 SkinDirection(float3 direction, float3x4 skin)
{
    return float3(dot(direction, skin[0].xyz), dot(direction, skin[1].xyz),
        dot(direction, skin[2].xyz));
}

float4 SkinPosition(float4 position, float3x4 skin)
{
    return float4(dot(position, skin[0]), dot(position, skin[1]), dot(position, skin[2]), 1.0);
}
#endif

VertexOutput main(VertexInput input)
{
    VertexOutput output;

    LodObjectTransform lodObject = g_LodObjectTransforms[g_LodObjectRemap[input.instance_id]];
    float4 position = float4(RotateByQuaternion(lodObject.rotation, input.position.xyz)
        * lodObject.translation_and_scale.w + lodObject.translation_and_scale.xyz, 1.0);

    float3 normal = normalize(INPUT_NORMAL.xyz * 2.0 - 1.0);
    float3 binormal = normalize(INPUT_BINORMAL.xyz * 2.0 - 1.0);
    float3 tangent = normalize(float3(input.position.w, INPUT_NORMAL.w * 2.0 - 1.0,
        INPUT_BINORMAL.w * 2.0 - 1.0));

    normal = RotateByQuaternion(lodObject.rotation, normal);
    binormal = RotateByQuaternion(lodObject.rotation, binormal);
    tangent = RotateByQuaternion(lodObject.rotation, tangent);

#if LOD_LANDSCAPE
#if SKINNED
    float3 blendPosition = position.xyz;
#else
    float3 blendPosition = TransformPosition(cb2_idx0_world_row0, cb2_idx1_world_row1,
        cb2_idx2_world_row2, cb12_idx35_world_offset.xyz, position);
#endif
    output.blendCoord = blendPosition.xy;
    if (all(abs(blendPosition.xy - cb0_idx0_blend_bounds.xy) < cb0_idx0_blend_bounds.zw))
    {
        position.z = position.z - (blendPosition.z * 0.000000001 + 230.0);
    }
#endif

#if SKINNED
    float3 weights = input.blend_weight.xyz;
    float lastWeight = 1.0 - saturate(weights.x + weights.y + weights.z);
    int4 bone = (int4)(input.blend_indices * 765.01);
    float3x4 skin = weights.x * CurrentBone(bone.x, cb12_idx35_world_offset.xyz)
        + weights.y * CurrentBone(bone.y, cb12_idx35_world_offset.xyz)
        + weights.z * CurrentBone(bone.z, cb12_idx35_world_offset.xyz)
        + lastWeight * CurrentBone(bone.w, cb12_idx35_world_offset.xyz);
    float4 world = SkinPosition(position, skin);
#else
    float4 worldRow0 = cb2_idx0_world_row0;
    float4 worldRow1 = cb2_idx1_world_row1;
    float4 worldRow2 = cb2_idx2_world_row2;
    float4 worldRow3 = cb2_idx3_world_row3;
    float4 world = float4(TransformPosition(worldRow0, worldRow1, worldRow2,
        cb12_idx35_world_offset.xyz, position), dot(worldRow3, position));
#endif

#if SKINNED
    float3x4 previousSkin = weights.x * PreviousBone(bone.x, cb12_idx36_previous_world_offset.xyz)
        + weights.y * PreviousBone(bone.y, cb12_idx36_previous_world_offset.xyz)
        + weights.z * PreviousBone(bone.z, cb12_idx36_previous_world_offset.xyz)
        + lastWeight * PreviousBone(bone.w, cb12_idx36_previous_world_offset.xyz);
    float3 previous = SkinPosition(position, previousSkin).xyz;
#else
    float3 previous = TransformPosition(cb2_idx4_previous_world_row0,
        cb2_idx5_previous_world_row1, cb2_idx6_previous_world_row2,
        cb12_idx36_previous_world_offset.xyz, position);
#endif

#if SKINNED
    float3 worldNormal = normalize(SkinDirection(normal, skin));
    float3 worldBinormal = normalize(SkinDirection(binormal, skin));
    float3 worldTangent = normalize(SkinDirection(tangent, skin));
#else
    float4 column0 = float4(worldRow0.x, worldRow1.x, worldRow2.x, worldRow3.x);
    float4 column1 = float4(worldRow0.y, worldRow1.y, worldRow2.y, worldRow3.y);
    float4 column2 = float4(worldRow0.z, worldRow1.z, worldRow2.z, worldRow3.z);
#endif

    float4 projected = world;
    output.position.x = dot(cb12_idx8_transform_row0, projected);
    output.position.y = dot(cb12_idx9_transform_row1, projected);
    output.position.z = dot(cb12_idx10_transform_row2, projected);
    output.position.w = dot(cb12_idx11_transform_row3, projected);
    output.currentPositionAndU.xyz = world.xyz;
#if SKINNED
    output.tangentRow0 = ProjectBasis(cb12_idx0_view_row0.xyz, worldTangent, worldBinormal,
        worldNormal);
    output.tangentRow1 = ProjectBasis(cb12_idx1_view_row1.xyz, worldTangent, worldBinormal,
        worldNormal);
    output.tangentRow2 = ProjectBasis(cb12_idx2_view_row2.xyz, worldTangent, worldBinormal,
        worldNormal);
#else
    float3 viewRow0 = ConcatenateRow(cb12_idx0_view_row0, column0, column1, column2);
    float3 viewRow1 = ConcatenateRow(cb12_idx1_view_row1, column0, column1, column2);
    float3 viewRow2 = ConcatenateRow(cb12_idx2_view_row2, column0, column1, column2);
    output.tangentRow0 = ProjectBasis(viewRow0, tangent, binormal, normal);
    output.tangentRow1 = ProjectBasis(viewRow1, tangent, binormal, normal);
    output.tangentRow2 = ProjectBasis(viewRow2, tangent, binormal, normal);
#endif
    float2 texcoord = ScaleTexcoord(INPUT_TEXCOORD, cb1_idx0_texcoord_scale_bias);
    output.currentPositionAndU.w = texcoord.x;
    output.previousPositionAndV.w = texcoord.y;
    output.previousPositionAndV.xyz = previous;
#if VC
    output.color = float4(pow(input.color.rgb, 2.2), OUTPUT_ALPHA);
#endif
    return output;
}
