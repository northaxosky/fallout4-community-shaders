// SPDX-License-Identifier: GPL-3.0-or-later WITH FO4-CS-Modding-Exception
// Exact AE 1.11.221 BSDFPrePassShader LOD_LANDSCAPE vertex family.
//
// Proven ABI over 14 bodies / containers and 25 routes:
// - SV_POSITION is the current raster clip position.
// - TEXCOORD0..2 are the three view-space TBN transform rows consumed by the
//   deferred pixel shader's tangent-space normal reconstruction.
// - TEXCOORD3.xyz and TEXCOORD4.xyz are the current and previous frame-relative
//   positions; their w lanes carry base UV. The paired deferred PS applies its
//   own current/previous WorldToClip rows before writing the motion vector.
//   There is no separate previous-clip output from this VS.
// - Non-LANDSCAPE routes add TEXCOORD9 as transformed XY. The paired
//   non-BONE_TINTING PS uses it for LOD color/normal noise; BONE_TINTING keeps
//   the ABI lane but reports it unused. The LANDSCAPE route instead adds
//   TEXCOORD6..8 for LOD albedo UV, layer weights, world XY, and terrain fade.
// - VC routes add COLOR0 after the source RGB is raised to 2.2. BONE_TINTING
//   and instanced routes retain their native COLOR1/COLOR2 extensions.
//
// The core SV_POSITION/TEXCOORD0..4 contract is the same contract used by near
// deferred geometry. LOD is additive: it contributes TEXCOORD9 or the terrain
// TEXCOORD6..8 payload and may lower model Z inside the blend bounds.
//
// The signatures and dataflow above are byte-proven. "TBN", "LOD noise",
// "terrain fade", and "motion vector" are interpretations from the paired
// producer sources. Runtime ownership of those values remains unverified.
// Constant-buffer indices below are scoped only to these 25 routes.

#if !defined(LOD_LANDSCAPE) || LOD_LANDSCAPE != 1
#  error "LodLandscapeVertex.hlsli requires LOD_LANDSCAPE=1 (raw technique bit 0x00000200)"
#endif
#if !defined(TEXTURE) || TEXTURE != 1
#  error "every native LOD_LANDSCAPE vertex compile class carries TEXTURE"
#endif
#if COMBINED || DISMEMBERMENT || DISMEMBERMENT_MEATCUFF || EYE || FACE || GRASS \
    || MERGE_INSTANCED || PIPBOY_SCREEN || SPLINE || TESSELLATE_DISP_HEIGHT \
    || TREE_ANIM
#  error "macro set is outside the native LOD_LANDSCAPE vertex population"
#endif

#define FO4_LOD_STANDARD_BASIS \
    (NORMALS && BINORMAL_TANGENT && !MODELSPACENORMALS)
#define FO4_LOD_STANDARD_ROUTE \
    (FO4_LOD_STANDARD_BASIS && ( \
        (LANDSCAPE && VC && !SKINNED && !INSTANCED && !LOD_OBJECT_INSTANCED \
            && !BONE_TINTING && !CLIP_VOLUME) \
        || (LOD_OBJECT_INSTANCED && VC && !INSTANCED && !LANDSCAPE \
            && !BONE_TINTING && !CLIP_VOLUME) \
        || (BONE_TINTING && !SKINNED && !INSTANCED && !LANDSCAPE \
            && !LOD_OBJECT_INSTANCED && !CLIP_VOLUME) \
        || (!LANDSCAPE && !LOD_OBJECT_INSTANCED && !BONE_TINTING \
            && !CLIP_VOLUME && (!INSTANCED || !SKINNED || VC))))
#define FO4_LOD_MODELSPACE_ROUTE \
    (MODELSPACENORMALS && !NORMALS && !BINORMAL_TANGENT && !VC && !SKINNED \
        && !INSTANCED && !LANDSCAPE && !LOD_OBJECT_INSTANCED && !BONE_TINTING)

#if !FO4_LOD_STANDARD_ROUTE && !FO4_LOD_MODELSPACE_ROUTE
#  error "macro set is not one of the 14 native LOD_LANDSCAPE vertex compile classes"
#endif

#undef FO4_LOD_MODELSPACE_ROUTE
#undef FO4_LOD_STANDARD_ROUTE
#undef FO4_LOD_STANDARD_BASIS

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

#if LOD_LANDSCAPE || CLIP_VOLUME
cbuffer PerGeometry_CB0 : register(b0)
{
#if LOD_LANDSCAPE
    // xy is the blend centre, zw the half extent that admits the drop.
    float4 cb0_idx0_blend_bounds;
#endif
#if CLIP_VOLUME
    // The volume keeps its own rows, so a landscape call still owns the bounds row above it.
    float4 cb0_clip_centre;
    float4 cb0_clip_extent;
#endif
};
#endif

cbuffer PerMaterial_CB1 : register(b1)
{
    float4 cb1_idx0_texcoord_scale_bias;
};

#if BONE_TINTING && SKINNED
// A skinned tint call rebinds the unused world rows as the palette's per-influence bit masks.
cbuffer PerCall_CB2 : register(b2)
{
    uint4 cb2_idx0_3_bone_tint_mask[4];
};
#elif SKINNED && (GRASS || TREE_ANIM || SPLINE)
// A skinned wind call takes its transform from the palette, so its own rows start the buffer.
cbuffer PerCall_CB2 : register(b2)
{
#if GRASS
    // xyz scales the blade against its instance lane.
    float4 cb2_grass_scale;
    // z starts the distance fade and w spans it.
    float4 cb2_grass_fade;
    float4 cb2_grass_pad;
#endif
    // x aims the gust and z and w phase the previous and current wind.
    float4 cb2_wind_phase;
    // xy bound the gust and z is the wind clock.
    float4 cb2_wind_range;
#if GRASS
    // xy centre a collider, z weights its push and w is its radius.
    float4 cb2_grass_collision[4];
#endif
#if TREE_ANIM || SPLINE
    // x is the span the sway measures against, z and w scale it.
    float4 cb2_wind_shape;
#endif
};
#else
cbuffer PerCall_CB2 : register(b2)
{
    float4 cb2_idx0_world_row0;
    float4 cb2_idx1_world_row1;
    float4 cb2_idx2_world_row2;
    float4 cb2_idx3_world_row3;
    float4 cb2_idx4_previous_world_row0;
    float4 cb2_idx5_previous_world_row1;
    float4 cb2_idx6_previous_world_row2;
#if LANDSCAPE || PIPBOY_SCREEN || MERGE_INSTANCED || TREE_ANIM || SPLINE || GRASS
    float4 cb2_pad_7;
#endif
#if GRASS
    // xyz scales the blade against its instance lane.
    float4 cb2_grass_scale;
    // z starts the distance fade and w spans it.
    float4 cb2_grass_fade;
    float4 cb2_grass_pad;
#endif
#if GRASS || TREE_ANIM || SPLINE
    // x aims the sway, y weights it, and z and w phase the previous and current wind.
    float4 cb2_wind_phase;
    // xy bound the sway amplitude and z is the wind clock.
    float4 cb2_wind_range;
#endif
#if GRASS
    // xy centre a collider, z weights its push and w is its radius.
    float4 cb2_grass_collision[4];
#endif
#if TREE_ANIM || SPLINE
    // x is the span the sway measures against, z and w scale it.
    float4 cb2_wind_shape;
#endif
#if LANDSCAPE
    // A terrain call fades against the same rows it transforms with, so xy sheets and zw centres.
    float4 cb2_idx8_terrain_offset_and_fade_centre;
#endif
#if PIPBOY_SCREEN
    float4 cb2_idx8_screen_row0;
    float4 cb2_idx9_screen_row1;
    float4 cb2_idx10_screen_row2;
#endif
#if MERGE_INSTANCED
    // z offsets the packed texcoord inside one merged vertex.
    uint4 cb2_idx8_merge_offsets;
    // x strides a merged vertex, y counts an object's triangles and z is the first merged vertex.
    uint4 cb2_idx9_merge_span;
#endif
};

// A cut call rebinds the first two world rows as the cut plane's point and axis.
#define CUT_POINT cb2_idx0_world_row0
#define CUT_AXIS cb2_idx1_world_row1
#endif

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

// A terrain patch arrives either as a structured quad the instance indexes or as plain attributes.
#define STRUCTURED_TERRAIN (LANDSCAPE && INSTANCED)
#define ATTRIBUTE_TERRAIN (LANDSCAPE && !INSTANCED)

// A terrain call spends its LOD axis on the patch fade, so only a non-terrain call drops height.
#define LOD_HEIGHT_DROP (LOD_LANDSCAPE && !LANDSCAPE)

#if STRUCTURED_TERRAIN
cbuffer PerInstance_CB13 : register(b13)
{
    float4 cb13_terrain_quad[100];
};

struct TerrainQuad
{
    float pad0;
    float3 origin;
    float2 texcoord_offset;
    int variant;
    float4 pad1;
    float4 pad2;
    float4 pad3;
};

struct TerrainVertex
{
    float height;
    float3 color;
    float3 normal;
    float3 tangent;
    float3 binormal;
    float3 blend;
    float alpha;
};

StructuredBuffer<TerrainQuad> g_TerrainQuads : register(t4);
StructuredBuffer<TerrainVertex> g_TerrainVertices : register(t5);
#elif INSTANCED
// Nine rows per instance: the world transform, five reserved rows and the texture scale-bias.
struct InstanceTransform
{
    float4 row0;
    float4 row1;
    float4 row2;
    float4 pad3;
    float4 pad4;
    float4 pad5;
    float4 texcoord_scale_bias;
    float4 pad7;
    float4 pad8;
};

cbuffer PerInstance_CB13 : register(b13)
{
    InstanceTransform cb13_instance[455];
};
#endif

#if COMBINED
struct CombinedTransform
{
    float4 row0;
    float4 row1;
    float4 row2;
    float4 previous_row0;
    float4 previous_row1;
    float4 previous_row2;
    float pad;
};

StructuredBuffer<CombinedTransform> g_CombinedTransforms : register(t6);
#endif

#if LOD_OBJECT_INSTANCED
// A LOD object instance places a model frame: xyz translates, w scales and the quaternion turns.
struct LodObjectTransform
{
    float4 translation_and_scale;
    float4 rotation;
};

StructuredBuffer<LodObjectTransform> g_LodObjectTransforms : register(t6);
StructuredBuffer<uint> g_LodObjectRemap : register(t7);
#endif

#if MERGE_INSTANCED
// A merged draw fetches its own vertices: t4 packs them, t6 and t7 index them, t8 places them.
ByteAddressBuffer g_MergedVertices : register(t4);
ByteAddressBuffer g_MergedIndices : register(t6);
ByteAddressBuffer g_MergedObjects : register(t7);

struct MergedTransform
{
    float4 row0;
    float4 row1;
    float4 row2;
    float4 row3;
    // x tints the merged vertex colour before it is linearised.
    float4 tint;
};

StructuredBuffer<MergedTransform> g_MergedTransforms : register(t8);

// A merged index list packs two entries per dword, so an odd entry takes the high half.
uint LoadMergedIndex(ByteAddressBuffer buffer, uint entry)
{
    uint odd = entry & 1;
    uint packed = buffer.Load(2 * entry - 2 * odd);
    return (packed & 0xFFFF) * (1 - odd) + (packed >> 16) * odd;
}

// The merged stream is half precision, so every dword it holds carries two lanes.
float2 UnpackHalf2(uint packed)
{
    return float2(f16tof32(packed), f16tof32(packed >> 16));
}

// A merged byte lane is unsigned; a direction lane doubles it back into a signed one.
float UnpackUnormByte(uint packed, uint shift)
{
    return ((packed >> shift) & 0xFF) / 255.0;
}

float4 UnpackUnorm4(uint packed)
{
    return float4(UnpackUnormByte(packed, 0), UnpackUnormByte(packed, 8),
        UnpackUnormByte(packed, 16), UnpackUnormByte(packed, 24));
}

float UnpackSignedByte(uint packed, uint shift)
{
    return UnpackUnormByte(packed, shift) * 2.0 - 1.0;
}

float3 UnpackSigned3(uint packed)
{
    return float3(UnpackSignedByte(packed, 0), UnpackSignedByte(packed, 8),
        UnpackSignedByte(packed, 16));
}

float4 LoadMergedPosition(uint address)
{
    uint2 packed = g_MergedVertices.Load2(address);
    return float4(UnpackHalf2(packed.x), UnpackHalf2(packed.y));
}
#endif

// A merged draw feeds itself, so the pipeline hands it nothing but the vertex id.
#define ATTRIBUTE_VERTEX (!MERGE_INSTANCED)

// A wind regime bends the model frame itself, so it brings the attributes that frame needs.
#define WIND_FRAME (GRASS || SPLINE || TREE_ANIM)

struct VertexInput
{
#if ATTRIBUTE_VERTEX
    float4 position : POSITION0;
#if TEXTURE
    float2 texcoord : TEXCOORD0;
#endif
#if NORMALS || WIND_FRAME
    float4 normal : NORMAL0;
#endif
#if BINORMAL_TANGENT || WIND_FRAME
    float4 binormal : BINORMAL0;
#endif
#if VC || TREE_ANIM || SPLINE
    float4 color : COLOR0;
#endif
#if SKINNED
    float4 blend_weight : BLENDWEIGHT0;
    float4 blend_indices : BLENDINDICES0;
#endif
#if LANDSCAPE
    float4 terrain : TEXCOORD3;
#endif
#if EYE
    float eye_index : TEXCOORD2;
#endif
#if GRASS
    float4 grass_origin : TEXCOORD4;
    float4 grass_row0 : TEXCOORD5;
    float4 grass_row1 : TEXCOORD6;
    // x closes the instance basis; y drives the per-blade scale.
    float4 grass_row2 : TEXCOORD7;
#endif
#if INSTANCED || LOD_OBJECT_INSTANCED
    uint instance_id : SV_InstanceID;
#endif
#endif
#if STRUCTURED_TERRAIN || MERGE_INSTANCED
    uint vertex_id : SV_VertexID;
#endif
};

// An ablated attribute leaves the signature, so its consumers read a neutral constant instead.
#if TEXTURE && ATTRIBUTE_VERTEX
#define INPUT_TEXCOORD input.texcoord
#else
#define INPUT_TEXCOORD float2(0.0, 0.0)
#endif

#if (NORMALS || WIND_FRAME) && ATTRIBUTE_VERTEX
#define INPUT_NORMAL input.normal
#else
#define INPUT_NORMAL float4(0.0, 0.0, 0.0, 0.0)
#endif

#if (BINORMAL_TANGENT || WIND_FRAME) && ATTRIBUTE_VERTEX
#define INPUT_BINORMAL input.binormal
#else
#define INPUT_BINORMAL float4(0.0, 0.0, 0.0, 0.0)
#endif

#if EYE
#define INPUT_EYE_INDEX input.eye_index
#else
#define INPUT_EYE_INDEX 0.0
#endif

#if INSTANCED
#define INPUT_INSTANCE_ID input.instance_id
#else
#define INPUT_INSTANCE_ID 0
#endif

// A merged draw carries its colour in its own stream; every other call reads the attribute.
#if MERGE_INSTANCED
#define VERTEX_COLOR mergedColor
#else
#define VERTEX_COLOR input.color
#endif

#if TESSELLATE_DISP_HEIGHT
struct VertexOutput
{
    float4 position : POSITION0;
    float3 normal : NORMAL0;
    float3 binormal : BINORMAL0;
    float3 tangent : TANGENT0;
    float2 texcoord : TEXCOORD0;
#if EYE && DISMEMBERMENT
    float2 dismember : TEXCOORD7;
#endif
#if VC
    float4 color : COLOR0;
#endif
    float4 position1 : POSITION1;
    float4 position2 : POSITION2;
};
#else
struct VertexOutput
{
    float4 position : SV_POSITION;
    float3 tangentRow0 : TEXCOORD0;
#if FACE
    // The face lane packs into the spare component of the first tangent row.
    float eyeIndexSquare : TEXCOORD6;
#endif
    float3 tangentRow1 : TEXCOORD1;
    float3 tangentRow2 : TEXCOORD2;
    float4 currentPositionAndU : TEXCOORD3;
    float4 previousPositionAndV : TEXCOORD4;
#if VC
    float4 color : COLOR0;
#endif
#if BONE_TINTING
    float4 boneTint : COLOR1;
#endif
#if INSTANCED || COMBINED
    uint instanceIndex : COLOR2;
#endif
#if LOD_HEIGHT_DROP
    float2 blendCoord : TEXCOORD9;
#endif
#if DISMEMBERMENT_MEATCUFF
    float2 dismember : TEXCOORD6;
    float3 cuffRadial : TEXCOORD7;
    float3 cuffAxis : TEXCOORD8;
    float3 cuffNormal : TEXCOORD9;
#endif
#if LANDSCAPE
    float2 terrainTexcoord : TEXCOORD6;
    float4 terrainBlend : TEXCOORD7;
    float3 terrainFade : TEXCOORD8;
#endif
#if PIPBOY_SCREEN
    float3 screenPosition : TEXCOORD6;
#endif
#if CLIP_VOLUME
    float clipDistance : SV_ClipDistance;
#endif
};
#endif

// The native folds the frame offset lanes into the first three row translations.
float3 TransformPosition(float4 row0, float4 row1, float4 row2, float3 offset, float4 position)
{
    float3 translation = float3(row0.w - offset.x, row1.w - offset.y, row2.w - offset.z);
    return float3(
        dot(float4(row0.xyz, translation.x), position),
        dot(float4(row1.xyz, translation.y), position),
        dot(float4(row2.xyz, translation.z), position));
}

// A blade offsets both frames at once, so the second transform takes its rows already folded.
float3 TransformRows(float4 row0, float4 row1, float4 row2, float3 translation, float4 position)
{
    return float3(
        dot(float4(row0.xyz, translation.x), position),
        dot(float4(row1.xyz, translation.y), position),
        dot(float4(row2.xyz, translation.z), position));
}

#if LANDSCAPE
// The terrain patch is axis aligned, so the transform above collapses to a two term axis dot.
float3 OffsetPosition(float4 position, float3 offset)
{
    return float3(
        dot(float2(1.0, 1.0), float2(position.x, -offset.x)),
        dot(float2(1.0, 1.0), float2(position.y, -offset.y)),
        dot(float2(1.0, 1.0), float2(position.z, -offset.z)));
}
#endif

// Keep the cbuffer row first so fxc emits the native dp4 operand order.
float3 ConcatenateRow(float4 row, float4 column0, float4 column1, float4 column2)
{
    return float3(dot(row, column0), dot(row, column1), dot(row, column2));
}

float3 ConcatenateRow(float4 row, float3 column0, float3 column1, float3 column2)
{
    return float3(dot(row.xyz, column0), dot(row.xyz, column1), dot(row.xyz, column2));
}

// Keep the cbuffer row first so fxc emits the native dp4 operand order.
float3 ProjectRows(float4 row0, float4 row1, float4 row2, float4 position)
{
    return float3(dot(row0, position), dot(row1, position), dot(row2, position));
}

float3 ProjectRows(float3 row0, float3 row1, float3 row2, float3 direction)
{
    return float3(dot(row0, direction), dot(row1, direction), dot(row2, direction));
}

// A cleared texture attribute drops its scale-bias with it, so the lane leaves as a literal zero.
float2 ScaleTexcoord(float2 texcoord, float4 scaleBias)
{
#if TEXTURE
    return texcoord * scaleBias.zw + scaleBias.xy;
#else
    return float2(0.0, 0.0);
#endif
}

// Keep the projected row first so fxc emits the native dp3 operand order.
float3 ProjectBasis(float3 row, float3 tangent, float3 binormal, float3 normal)
{
    return float3(dot(row, tangent), dot(row, binormal), dot(row, normal));
}

#if MERGE_INSTANCED
// The merged object owns a 4x4 whose upper 3x3 turns a model direction into the merged frame.
float3 TurnMerged(MergedTransform transform, float3 direction)
{
    return ProjectBasis(direction,
        float3(transform.row0.x, transform.row1.x, transform.row2.x),
        float3(transform.row0.y, transform.row1.y, transform.row2.y),
        float3(transform.row0.z, transform.row1.z, transform.row2.z));
}

// The last row of that 4x4 translates the turned position and its w scales it.
float3 PlaceMerged(MergedTransform transform, float3 position)
{
    return transform.row3.w * TurnMerged(transform, position) + transform.row3.xyz;
}
#endif

#if LOD_OBJECT_INSTANCED
// Rodrigues on a unit quaternion: the doubled cross turns a model direction into instance space.
float3 RotateByQuaternion(float4 rotation, float3 direction)
{
    float3 turn = 2.0 * cross(rotation.xyz, direction);
    return direction + rotation.w * turn + cross(rotation.xyz, turn);
}
#endif

#if GRASS
// A blade leans on a gust whose shape folds a doubled sine into a cosine ripple.
float GrassGust(float angle, float halfSpan)
{
    float phase;
    float ripple;
    sincos(angle, phase, ripple);
    float2 wave = sin(phase * float2(3.1415927, 6.2831853));
    float gust = (wave.x + wave.y) * 0.3 + cos(ripple * 3.1415927) * 0.2 + 1.0;
    return gust * halfSpan + cb2_wind_range.x;
}

// Each collider pushes the blade sideways in clip space only, so the world lane keeps its place.
float3 CollideGrass(float3 world, float contact)
{
    float2 pushed = world.xy;
    [loop] for (uint collider = 0; collider < 4; ++collider)
    {
        float4 sphere = cb2_grass_collision[collider];
        float3 delta = float3(pushed, world.z) - float3(sphere.xy, sphere.w * 0.17 + world.z);
        float reach = length(delta);
        float bite = max(sphere.w - reach, 0.0);
        float fade = saturate(reach / (0.33 * sphere.w));
        float2 direction = normalize(delta).xy;
        float falloff = 1.0 - fade;
        pushed += contact * (direction * (bite * falloff)) * sphere.z;
    }
    return float3(pushed, world.z);
}
#endif

#if TREE_ANIM || SPLINE
// Half the sway band, shared by every gust the wind rows drive.
float WindHalfRange()
{
    return (cb2_wind_range.y - cb2_wind_range.x) * 0.5;
}
#endif

#if SPLINE
// The drift folds a slow beat of the instance seed into the gust before the throw.
float SplineDrift(float seed, float beat, float clock)
{
    return seed * sin(seed * beat) / clock;
}
#endif

#if TREE_ANIM
// A trunk leans with its height ratio, and the doubled sine shapes each gust.
float3 TreeThrow(float ratio, float seed, float wind)
{
    float2 gust;
#if SKINNED
    // A skinned trunk brings no seed, so the gust folds the wind term ahead of the height lean.
    gust = sin(sin(seed + wind + ratio * -1.5) * float2(3.1415927, 6.2831853));
#else
    gust = sin(sin(ratio * -1.5 + seed + wind) * float2(3.1415927, 6.2831853));
#endif
    float3 swing = ((gust.x * 0.3 + gust.y + 2.0) * WindHalfRange()
        + cb2_wind_range.x) * ratio;
    return swing / 3.0;
}

// The trunk keeps its length as it leans, so the bend renormalises the offset from its base.
float3 BendTree(float3 origin, float3 delta, float distance, float3 lean)
{
    return normalize(delta + lean) * distance + origin;
}
#endif

#if LANDSCAPE
// The fade measures the patch position, so it is taken before the frame offset.
float TerrainFade(float4 position)
{
    float2 delta = cb2_idx8_terrain_offset_and_fade_centre.zw
        - float2(dot(cb2_idx0_world_row0, position), dot(cb2_idx1_world_row1, position));
    return 1.0 - saturate((9625.6 - length(delta)) / 2662.4);
}
#endif

#if SKINNED
// A palette entry is three rows; the native subtracts the frame offset from every row it blends.
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

// Keep the frame vector first so fxc emits the native dp3 operand order.
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

#if BONE_TINTING && SKINNED
// A palette row admits an influence when the row owns its index group and holds its index bit.
float4 BoneTintRow(uint4 group, uint4 owner, uint4 bit, uint4 mask, float4 weight)
{
    return (group == owner) ? ((bit & mask) ? weight : 0.0) : 0.0;
}
#endif

// A wind regime already spent the vertex alpha on its sway, so that lane leaves opaque.
#if TREE_ANIM || SPLINE
#define OUTPUT_ALPHA 1.0
#else
#define OUTPUT_ALPHA VERTEX_COLOR.a
#endif

VertexOutput main(VertexInput input)
{
    VertexOutput output;

#if STRUCTURED_TERRAIN
    int quadIndex = (int)cb13_terrain_quad[INPUT_INSTANCE_ID].x;
    int gridIndex = (int)input.vertex_id;
    int gridColumn = gridIndex % 17;
    int gridRow = (gridIndex - gridColumn) / 17;
    float2 gridCell = float2(gridColumn, gridRow);
    // Order is load-bearing: the scaled cell claims the first converted lane pair.
    float2 gridTexcoord = gridCell * 0.75;
    float2 gridLocal = gridCell * 128.0 - 2048.0;
    TerrainQuad quad = g_TerrainQuads[quadIndex];
    TerrainVertex corner = g_TerrainVertices[quadIndex * 289 + gridIndex];
    float4 position = float4(float3(gridLocal, corner.height) + quad.origin, 1.0);
    float fade = TerrainFade(position);
#elif LOD_OBJECT_INSTANCED
    LodObjectTransform lodObject = g_LodObjectTransforms[g_LodObjectRemap[input.instance_id]];
    float4 position = float4(RotateByQuaternion(lodObject.rotation, input.position.xyz)
        * lodObject.translation_and_scale.w + lodObject.translation_and_scale.xyz, 1.0);
#elif MERGE_INSTANCED
    uint mergedVertex = input.vertex_id + cb2_idx9_merge_span.z;
    uint mergedAddress = LoadMergedIndex(g_MergedIndices, mergedVertex)
        * cb2_idx9_merge_span.x;
    float4 mergedPosition = LoadMergedPosition(mergedAddress);
    uint mergedColorPacked = g_MergedVertices.Load(mergedAddress + cb2_idx8_merge_offsets.w);
    float4 mergedColor = UnpackUnorm4(mergedColorPacked);
    // The object lookup only places the vertex, so it follows the stream the vertex came from.
    MergedTransform merged = g_MergedTransforms[LoadMergedIndex(g_MergedObjects,
        mergedVertex / (cb2_idx9_merge_span.y * 3))];
    // An ablated direction axis drops the stream lane it reads, leaving a neutral packed word.
#if NORMALS
    uint mergedNormalPacked = g_MergedVertices.Load(mergedAddress + cb2_idx8_merge_offsets.x);
#else
    uint mergedNormalPacked = 0;
#endif
#if BINORMAL_TANGENT
    uint mergedBinormalPacked = g_MergedVertices.Load(mergedAddress + cb2_idx8_merge_offsets.y);
#else
    uint mergedBinormalPacked = 0;
#endif
    float2 mergedTexcoord =
        UnpackHalf2(g_MergedVertices.Load(mergedAddress + cb2_idx8_merge_offsets.z));
    float4 position = float4(PlaceMerged(merged, mergedPosition.xyz), 1.0);
#else
    float4 position = float4(input.position.xyz, 1.0);
#endif

#if STRUCTURED_TERRAIN
    float3 normal = corner.normal;
    float3 binormal = corner.binormal;
    float3 tangent = corner.tangent;
#elif MODELSPACENORMALS
    float3 normal = float3(0.0, 0.0, 1.0);
    float3 binormal = float3(0.0, 1.0, 0.0);
    float3 tangent = float3(1.0, 0.0, 0.0);
#elif MERGE_INSTANCED
    float3 normal = TurnMerged(merged, UnpackSigned3(mergedNormalPacked));
    float3 binormal = TurnMerged(merged, UnpackSigned3(mergedBinormalPacked));
    float3 tangent = TurnMerged(merged, float3(mergedPosition.w,
        UnpackSignedByte(mergedNormalPacked, 24),
        UnpackSignedByte(mergedBinormalPacked, 24)));
#elif GRASS
    // A blade unpacks its whole frame before it normalises, so the rows arrive together.
    float3 bladeNormal = INPUT_NORMAL.xyz * 2.0 - 1.0;
    float3 bladeBinormal = INPUT_BINORMAL.xyz * 2.0 - 1.0;
    float3 normal = normalize(bladeNormal);
    float3 binormal = normalize(bladeBinormal);
    float3 tangent = normalize(float3(input.position.w, INPUT_NORMAL.w * 2.0 - 1.0,
        INPUT_BINORMAL.w * 2.0 - 1.0));
#else
    float3 normal = normalize(INPUT_NORMAL.xyz * 2.0 - 1.0);
    float3 binormal = normalize(INPUT_BINORMAL.xyz * 2.0 - 1.0);
    float3 tangent = normalize(float3(input.position.w, INPUT_NORMAL.w * 2.0 - 1.0,
        INPUT_BINORMAL.w * 2.0 - 1.0));
#endif

// A blade owns its texture lane before it leans, so the scale-bias is taken with the basis.
#define GRASS_TEXCOORD (GRASS && !INSTANCED && !MERGE_INSTANCED)

#if GRASS_TEXCOORD
    float2 texcoord = ScaleTexcoord(INPUT_TEXCOORD, cb1_idx0_texcoord_scale_bias);
#endif
#if GRASS
    // The blade carries its own basis: two attribute rows and a third from their spare lanes.
    float3 grassRow2 = float3(input.grass_row2.x, input.grass_row0.w, input.grass_row1.w);
    float3 grassPlaced = ProjectRows(input.grass_row0.xyz, input.grass_row1.xyz, grassRow2,
        (input.grass_row2.y * cb2_grass_scale.xyz + 1.0) * input.position.xyz);
    normal = ProjectRows(input.grass_row0.xyz, input.grass_row1.xyz, grassRow2, normal);
    binormal = ProjectRows(input.grass_row0.xyz, input.grass_row1.xyz, grassRow2, binormal);
    tangent = ProjectRows(input.grass_row0.xyz, input.grass_row1.xyz, grassRow2, tangent);
#if VC
    float2 grassAngle = float2(cb2_wind_phase.w, cb2_wind_phase.z)
        - (input.grass_origin.x + input.grass_origin.y) * 0.0078125;
    float grassSpan = (cb2_wind_range.y - cb2_wind_range.x) * 0.5;
    grassAngle *= cb2_wind_range.z;
    float grassSweep = GrassGust(grassAngle.x, grassSpan)
        * (VERTEX_COLOR.a * VERTEX_COLOR.a * 0.5);
    float3 grassLean;
    sincos(cb2_wind_phase.x, grassLean.y, grassLean.x);
    grassLean.z = 0.0;
    float3 grassGust = grassLean * grassSweep;
#else
    float3 grassGust = float3(0.0, 0.0, 0.0);
#endif
    normal = normalize(normal + grassGust);
    binormal = normalize(binormal + grassGust);
    tangent = normalize(tangent + grassGust);
    position.xyz = grassPlaced + grassGust + input.grass_origin.xyz;
#if VC && !(TREE_ANIM || SPLINE)
    // The previous gust only shapes the previous frame, so it is taken after the blade lands.
    float3 grassPreviousGust = grassLean * (GrassGust(grassAngle.y, grassSpan)
        * (VERTEX_COLOR.a * VERTEX_COLOR.a * 0.5));
#elif !(TREE_ANIM || SPLINE)
    float3 grassPreviousGust = float3(0.0, 0.0, 0.0);
#endif
#if TREE_ANIM || SPLINE
    // A blade under another wind regime carries one gust, so both frames leave from it.
    float4 previousPosition = float4(position.xyz, 1.0);
#else
    float4 previousPosition =
        float4(grassPlaced + (input.grass_origin.xyz + grassPreviousGust), 1.0);
#endif
#if VC
    // A blade tints with its instance lane, so the colour is taken where the blade lands.
    float3 grassColor = input.color.rgb * input.grass_origin.w;
#endif
#endif

#if LOD_OBJECT_INSTANCED
    normal = RotateByQuaternion(lodObject.rotation, normal);
    binormal = RotateByQuaternion(lodObject.rotation, binormal);
    tangent = RotateByQuaternion(lodObject.rotation, tangent);
#endif

#if TREE_ANIM && !SKINNED
    // The trunk leans about both frame origins, so the two row translations land side by side.
    float3 treeOrigin = float3(cb2_idx0_world_row0.w - cb12_idx35_world_offset.x,
        cb2_idx1_world_row1.w - cb12_idx35_world_offset.y,
        cb2_idx2_world_row2.w - cb12_idx35_world_offset.z);
    float3 previousOrigin = float3(
        cb2_idx4_previous_world_row0.w - cb12_idx36_previous_world_offset.x,
        cb2_idx5_previous_world_row1.w - cb12_idx36_previous_world_offset.y,
        cb2_idx6_previous_world_row2.w - cb12_idx36_previous_world_offset.z);
#endif

#if TREE_ANIM
#if GRASS
    // A blade already scales its own wind rows, so a grassy trunk takes the shape lane raw.
    float windStrength = cb2_wind_shape.w;
    float windClock = cb2_wind_range.z * windStrength;
#else
    // The trunk takes its strength and its leaf scale from one shape pair.
    float2 windScale = cb2_wind_shape.wz * float2(20.0, 0.035);
    float windStrength = windScale.x;
    float windClock = windStrength * cb2_wind_range.z * 5.0;
#endif
#if SKINNED
    // A skinned trunk stands on the palette, not on a world row, so it seeds from the origin.
    float windSeed = 0.0;
#else
    float windSeed = (cb2_idx0_world_row0.w - cb12_idx35_world_offset.x)
        + (cb2_idx1_world_row1.w - cb12_idx35_world_offset.y)
        + (cb2_idx2_world_row2.w - cb12_idx35_world_offset.z)
        + cb12_idx35_world_offset.x + cb12_idx35_world_offset.y + cb12_idx35_world_offset.z;
#endif
    // The frame offset cancels out of the seed, so a tree keeps its phase as the origin moves.
    float windPhase = normal.x + normal.y + normal.z;
    windPhase = windPhase * 3.0 + windSeed;
    float2 windBeat = float2(cb2_wind_phase.w, cb2_wind_phase.z);
    float2 leafAngle = windBeat * windClock + windPhase;
#if GRASS
    float leafScale = cb2_wind_shape.z * 0.035;
#else
    float leafScale = windScale.y;
#endif
    float2 leafGust = sin(leafAngle) * WindHalfRange() + cb2_wind_range.x;
    float2 leafSway = leafGust * leafScale;
    float windWeight = saturate(VERTEX_COLOR.a);
#if !GRASS && !SKINNED
    // A bare trunk keeps its previous frame where the leaf found it, so the sway lands later.
    float4 previousPosition = float4(position.xyz, 1.0);
#endif
    position.xyz = normal * leafSway.x * windWeight + position.xyz;
#if SKINNED
    // The palette blends an already bent vertex, so the trunk leans about the model origin.
    float treeRatio = (position.x + position.y + position.z) / cb2_wind_shape.x;
    float2 windTime = windBeat * cb2_wind_range.z;
    float3 windLean;
    sincos(cb2_wind_phase.x, windLean.y, windLean.x);
    windLean.z = 0.0;
    position.xyz = BendTree(float3(0.0, 0.0, 0.0), position.xyz, length(position.xyz),
        (TreeThrow(treeRatio, windSeed, windTime.x * windStrength) * windLean) * windWeight);
#endif
#endif

#if INSTANCED && !LANDSCAPE
    InstanceTransform instance = cb13_instance[input.instance_id];
#endif

#if COMBINED
    uint transformIndex = (uint)INPUT_EYE_INDEX;
#if !SKINNED
    CombinedTransform transform = g_CombinedTransforms[transformIndex];
#endif
#endif

#if LOD_HEIGHT_DROP
// A skinned vertex has no single world row set, so its drop reads model space instead.
#if SKINNED
    float3 blendPosition = position.xyz;
#elif INSTANCED
    float3 blendPosition = TransformPosition(instance.row0, instance.row1, instance.row2,
        cb12_idx35_world_offset.xyz, position);
#else
    float3 blendPosition = TransformPosition(cb2_idx0_world_row0, cb2_idx1_world_row1,
        cb2_idx2_world_row2, cb12_idx35_world_offset.xyz, position);
#endif
    output.blendCoord = blendPosition.xy;
#if !CLIP_VOLUME
    if (all(abs(blendPosition.xy - cb0_idx0_blend_bounds.xy) < cb0_idx0_blend_bounds.zw))
    {
        // The near-zero height weight is the native's: it keeps the drop constant.
        position.z = position.z - (blendPosition.z * 0.000000001 + 230.0);
    }
#endif
#endif

#if SPLINE && !TREE_ANIM
    // A spline sways both frames together, so the two swayed row translations land side by side.
    float3 splineOrigin = float3(cb2_idx0_world_row0.w - cb12_idx35_world_offset.x,
        cb2_idx1_world_row1.w - cb12_idx35_world_offset.y,
        cb2_idx2_world_row2.w - cb12_idx35_world_offset.z);
    float3 previousOrigin = float3(
        cb2_idx4_previous_world_row0.w - cb12_idx36_previous_world_offset.x,
        cb2_idx5_previous_world_row1.w - cb12_idx36_previous_world_offset.y,
        cb2_idx6_previous_world_row2.w - cb12_idx36_previous_world_offset.z);
    float splineRange = cb2_wind_range.y - cb2_wind_range.x;
    float2 splineBeat = float2(cb2_wind_phase.w, cb2_wind_phase.z) * 5.0;
    float splineSquare = splineRange * splineRange;
    float splineGust = splineSquare * 0.002;
    float3 splineClock = float3(cb2_wind_range.z, cb2_wind_range.z,
        cb2_wind_range.x) * float3(10.0, -40.0, 0.25);
    float splineSeed =
        dot(splineOrigin + cb12_idx35_world_offset.xyz, float3(1.0, 1.0, 1.0)) * 0.001;
    float splineDrift =
        SplineDrift(splineSeed, splineBeat.x, splineClock.x) * 3.1415927 + splineSeed;
    float splineWeight = cb2_wind_phase.y * VERTEX_COLOR.a + 1.0;
    float splineAngle = splineWeight * splineDrift + splineClock.y;
    splineAngle = splineClock.x * splineBeat.x + splineAngle;
    float splineAmp = (splineGust * sin(splineAngle) + splineClock.z) * VERTEX_COLOR.a;
    float2 splineLean;
    sincos(cb2_wind_phase.x, splineLean.y, splineLean.x);
    float2 splineSway = float2(splineLean.x * splineAmp + splineOrigin.x,
        splineLean.y * splineAmp + splineOrigin.y);
    float previousSeed =
        dot(previousOrigin + cb12_idx36_previous_world_offset.xyz, float3(1.0, 1.0, 1.0)) * 0.001;
    float previousDrift =
        SplineDrift(previousSeed, splineBeat.y, splineClock.x) * 3.1415927 + previousSeed;
    float previousAngle = splineWeight * previousDrift + splineClock.y;
    previousAngle = splineClock.x * splineBeat.x + previousAngle;
    float previousAmp = (splineGust * sin(previousAngle) + splineClock.z) * VERTEX_COLOR.a;
    float2 previousSway = float2(splineLean.x * previousAmp + previousOrigin.x,
        splineLean.y * previousAmp + previousOrigin.y);
#endif

#if GRASS && !SKINNED
    // The blade offsets both frames together, so the two row translations land side by side.
    float3 grassTranslation = float3(
        cb2_idx0_world_row0.w - cb12_idx35_world_offset.x,
        cb2_idx1_world_row1.w - cb12_idx35_world_offset.y,
        cb2_idx2_world_row2.w - cb12_idx35_world_offset.z);
    float3 grassPreviousTranslation = float3(
        cb2_idx4_previous_world_row0.w - cb12_idx36_previous_world_offset.x,
        cb2_idx5_previous_world_row1.w - cb12_idx36_previous_world_offset.y,
        cb2_idx6_previous_world_row2.w - cb12_idx36_previous_world_offset.z);
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
#elif TESSELLATE_DISP_HEIGHT
    float4 world = position;
#elif STRUCTURED_TERRAIN
    float4 world = float4(OffsetPosition(position, cb12_idx35_world_offset.xyz), 1.0);
#elif TREE_ANIM
    float4 worldRow0 = cb2_idx0_world_row0;
    float4 worldRow1 = cb2_idx1_world_row1;
    float4 worldRow2 = cb2_idx2_world_row2;
    float4 worldRow3 = cb2_idx3_world_row3;
    // The depth lane rides with the other three row dots, so it is taken before the bend.
    float3 treePlaced = TransformPosition(worldRow0, worldRow1, worldRow2,
        cb12_idx35_world_offset.xyz, position);
    float treeDepth = dot(worldRow3, position);
    float3 treeDelta = treePlaced - treeOrigin;
    float treeDistance = length(treeDelta);
    float treeRatio = (treeDelta.x + treeDelta.y + treeDelta.z) / cb2_wind_shape.x;
    float2 windTime = windBeat * cb2_wind_range.z;
    // The flat lane keeps the lean a triple, so the sway never leaves the ground plane.
    float3 windLean;
    sincos(cb2_wind_phase.x, windLean.y, windLean.x);
    windLean.z = 0.0;
    float4 world = float4(BendTree(treeOrigin, treeDelta, treeDistance,
        (TreeThrow(treeRatio, windSeed, windTime.x * windStrength) * windLean) * windWeight),
        treeDepth);
#elif SPLINE
    float4 worldRow0 = cb2_idx0_world_row0;
    float4 worldRow1 = cb2_idx1_world_row1;
    float4 worldRow2 = cb2_idx2_world_row2;
    float4 worldRow3 = cb2_idx3_world_row3;
    float4 world = float4(ProjectRows(
        float4(worldRow0.xyz, splineSway.x),
        float4(worldRow1.xyz, splineSway.y),
        float4(worldRow2.xyz, splineOrigin.z), position),
        dot(worldRow3, position));
#elif INSTANCED
    float4 worldRow0 = instance.row0;
    float4 worldRow1 = instance.row1;
    float4 worldRow2 = instance.row2;
    float4 world = float4(TransformPosition(worldRow0, worldRow1, worldRow2,
        cb12_idx35_world_offset.xyz, position), 1.0);
#elif COMBINED
    float4 worldRow0 = transform.row0;
    float4 worldRow1 = transform.row1;
    float4 worldRow2 = transform.row2;
    float4 world = float4(TransformPosition(worldRow0, worldRow1, worldRow2,
        cb12_idx35_world_offset.xyz, position), 1.0);
#elif GRASS
    float4 worldRow0 = cb2_idx0_world_row0;
    float4 worldRow1 = cb2_idx1_world_row1;
    float4 worldRow2 = cb2_idx2_world_row2;
    float4 worldRow3 = cb2_idx3_world_row3;
    float4 world = float4(TransformRows(worldRow0, worldRow1, worldRow2,
        grassTranslation, position), dot(worldRow3, position));
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
#elif TESSELLATE_DISP_HEIGHT
    float3 previous = position.xyz;
#elif STRUCTURED_TERRAIN
    float3 previous = OffsetPosition(position, cb12_idx36_previous_world_offset.xyz);
#elif TREE_ANIM
    // The previous frame sways only where it is projected, so the leaf lane is taken here.
    previousPosition.xyz = normal * leafSway.y * windWeight + previousPosition.xyz;
    float3 previousDelta = TransformPosition(cb2_idx4_previous_world_row0,
        cb2_idx5_previous_world_row1, cb2_idx6_previous_world_row2,
        cb12_idx36_previous_world_offset.xyz, previousPosition) - previousOrigin;
    float previousRatio =
        (previousDelta.x + previousDelta.y + previousDelta.z) / cb2_wind_shape.x;
    float3 previous = BendTree(previousOrigin, previousDelta, treeDistance,
        (windLean * TreeThrow(previousRatio, windSeed, windTime.y * windStrength))
            * windWeight);
#elif SPLINE
    float3 previous = ProjectRows(
        float4(cb2_idx4_previous_world_row0.xyz, previousSway.x),
        float4(cb2_idx5_previous_world_row1.xyz, previousSway.y),
        float4(cb2_idx6_previous_world_row2.xyz, previousOrigin.z), position);
#elif GRASS
    float3 previous = TransformRows(cb2_idx4_previous_world_row0,
        cb2_idx5_previous_world_row1, cb2_idx6_previous_world_row2,
        grassPreviousTranslation, previousPosition);
#elif INSTANCED
    float3 previous = TransformPosition(worldRow0, worldRow1, worldRow2,
        cb12_idx36_previous_world_offset.xyz, position);
#elif COMBINED
    float3 previous = TransformPosition(transform.previous_row0, transform.previous_row1,
        transform.previous_row2, cb12_idx36_previous_world_offset.xyz, position);
#else
    float3 previous = TransformPosition(cb2_idx4_previous_world_row0,
        cb2_idx5_previous_world_row1, cb2_idx6_previous_world_row2,
        cb12_idx36_previous_world_offset.xyz, position);
#endif

#if SKINNED
    float3 worldNormal = normalize(SkinDirection(normal, skin));
    float3 worldBinormal = normalize(SkinDirection(binormal, skin));
    float3 worldTangent = normalize(SkinDirection(tangent, skin));
#elif TESSELLATE_DISP_HEIGHT
    float3 worldNormal = normal;
    float3 worldBinormal = binormal;
    float3 worldTangent = tangent;
#elif STRUCTURED_TERRAIN
    float3 column0 = float3(1.0, 0.0, 0.0);
    float3 column1 = float3(0.0, 1.0, 0.0);
    float3 column2 = float3(0.0, 0.0, 1.0);
#elif INSTANCED || COMBINED
    float3 column0 = float3(worldRow0.x, worldRow1.x, worldRow2.x);
    float3 column1 = float3(worldRow0.y, worldRow1.y, worldRow2.y);
    float3 column2 = float3(worldRow0.z, worldRow1.z, worldRow2.z);
#else
    float4 column0 = float4(worldRow0.x, worldRow1.x, worldRow2.x, worldRow3.x);
    float4 column1 = float4(worldRow0.y, worldRow1.y, worldRow2.y, worldRow3.y);
    float4 column2 = float4(worldRow0.z, worldRow1.z, worldRow2.z, worldRow3.z);
#endif

#if TESSELLATE_DISP_HEIGHT
    output.position = world;
    output.normal = worldNormal;
    output.binormal = worldBinormal;
    output.tangent = worldTangent;
#if EYE && DISMEMBERMENT
    output.dismember = float2(input.eye_index * CUT_AXIS.w, 0.0);
#endif
    output.texcoord = ScaleTexcoord(INPUT_TEXCOORD, cb1_idx0_texcoord_scale_bias);
#if VC
    output.color = float4(pow(input.color.rgb, 2.2), input.color.a);
#endif
    output.position1 = world;
    output.position2 = float4(previous, 1.0);
#else
#if GRASS && VC
    // The colliders only push the blade in clip space, so the world lane keeps its own place.
    float4 projected =
        float4(CollideGrass(world.xyz, saturate(ceil(VERTEX_COLOR.a))), world.w);
#else
    float4 projected = world;
#endif
    output.position.x = dot(cb12_idx8_transform_row0, projected);
    output.position.y = dot(cb12_idx9_transform_row1, projected);
    output.position.z = dot(cb12_idx10_transform_row2, projected);
    output.position.w = dot(cb12_idx11_transform_row3, projected);
#if GRASS && VC
    // A blade fades out with distance, so its alpha lane carries the fade instead.
    output.color = float4(pow(grassColor, 2.2),
        1.0 - saturate((length(projected.xyz) - cb2_grass_fade.z) / cb2_grass_fade.w));
#endif
    output.currentPositionAndU.xyz = world.xyz;
#if CLIP_VOLUME
    output.clipDistance =
        length((world.xyz - cb0_clip_centre.xyz) / cb0_clip_extent.xyz) - 1.0;
#endif
#if FACE
    output.eyeIndexSquare = INPUT_EYE_INDEX * INPUT_EYE_INDEX;
#endif
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
#if STRUCTURED_TERRAIN
    float2 texcoord = gridTexcoord;
#elif MERGE_INSTANCED
    float2 texcoord = ScaleTexcoord(mergedTexcoord, cb1_idx0_texcoord_scale_bias);
#elif INSTANCED
    float2 texcoord = ScaleTexcoord(INPUT_TEXCOORD, instance.texcoord_scale_bias);
#elif !GRASS_TEXCOORD
    float2 texcoord = ScaleTexcoord(INPUT_TEXCOORD, cb1_idx0_texcoord_scale_bias);
#endif
    output.currentPositionAndU.w = texcoord.x;
    output.previousPositionAndV.w = texcoord.y;
    output.previousPositionAndV.xyz = previous;
#if STRUCTURED_TERRAIN
#if VC
    output.color = float4(pow(corner.color, 2.2), 1.0);
#endif
#if INSTANCED || COMBINED
    output.instanceIndex = quadIndex;
#endif
#else
#if VC
#if MERGE_INSTANCED
    output.color = float4(pow(merged.tint.x * mergedColor.rgb, 2.2), OUTPUT_ALPHA);
#elif GRASS
    // The blade already wrote its faded colour with the clip position.
#else
    output.color = float4(pow(input.color.rgb, 2.2), OUTPUT_ALPHA);
#endif
#endif
#if BONE_TINTING
#if SKINNED
    uint4 tintIndex = (uint4)(input.blend_indices * 255.01);
    uint4 tintBit = 1u << tintIndex;
    uint4 tintGroup = tintIndex & 96;
    float4 tintLane =
        BoneTintRow(tintGroup, 0, tintBit, cb2_idx0_3_bone_tint_mask[0], input.blend_weight)
        + BoneTintRow(tintGroup, 32, tintBit, cb2_idx0_3_bone_tint_mask[1], input.blend_weight)
        + BoneTintRow(tintGroup, 64, tintBit, cb2_idx0_3_bone_tint_mask[2], input.blend_weight)
        + BoneTintRow(tintGroup, 96, tintBit, cb2_idx0_3_bone_tint_mask[3], input.blend_weight);
    float tintWeight = tintLane.x + tintLane.y + tintLane.z + tintLane.w;
#else
    float tintWeight = 1.0;
#endif
    output.boneTint = float4(1.0, 1.0, 1.0, tintWeight);
#endif
#if DISMEMBERMENT_MEATCUFF
    float cutDepth = INPUT_EYE_INDEX * CUT_AXIS.w;
    output.dismember = float2(cutDepth, 0.0);
#if MODELSPACENORMALS
    // The cuff frame is model space, so only a model-space cell resolves the cut plane.
    float3 cutOrigin = CUT_POINT.xyz + CUT_AXIS.xyz * -cutDepth;
    float3 cuffRadial = normalize(input.position.xyz - cutOrigin);
    float3 cuffAxis = normalize(CUT_AXIS.xyz - cutOrigin);
#elif SKINNED
    float3 cuffRadial = worldNormal;
    float3 cuffAxis = float3(0.0, 0.0, 0.0);
#else
    float3 cuffRadial = normal;
    float3 cuffAxis = float3(0.0, 0.0, 0.0);
#endif
    output.cuffRadial = cuffRadial;
    output.cuffAxis = cuffAxis;
    output.cuffNormal = cross(cuffRadial, cuffAxis);
#endif
#if INSTANCED
    output.instanceIndex = input.instance_id;
#elif COMBINED
    output.instanceIndex = transformIndex;
#endif
#if PIPBOY_SCREEN
    output.screenPosition = ProjectRows(cb2_idx8_screen_row0, cb2_idx9_screen_row1,
        cb2_idx10_screen_row2, position);
#endif
#endif
#if LANDSCAPE
#if STRUCTURED_TERRAIN
    float2 terrainOffset = quad.texcoord_offset;
#else
    float2 terrainOffset = cb2_idx8_terrain_offset_and_fade_centre.xy;
#endif
    float2 terrainTexcoord = INPUT_TEXCOORD * (1.0 / 48.0) + terrainOffset;
    output.terrainTexcoord = float2(terrainTexcoord.x, 1.0 - terrainTexcoord.y);
#if STRUCTURED_TERRAIN
    output.terrainBlend = float4(corner.blend, corner.alpha);
    output.terrainFade = float3(world.xy, quad.variant == -1 ? 0.0 : fade);
#else
    output.terrainBlend = input.terrain;
#if LOD_LANDSCAPE
    output.terrainFade = float3(world.xy, TerrainFade(position));
#else
    output.terrainFade = float3(world.xy, 0.0);
#endif
#endif
#endif
#endif
    return output;
}
