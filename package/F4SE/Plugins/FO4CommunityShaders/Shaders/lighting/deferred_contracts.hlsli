// SPDX-License-Identifier: GPL-3.0-or-later WITH FO4-CS-Modding-Exception
//
// Shared per-frame contract for FO4 deferred-lighting pipeline reconstructions.
//
// Each deferred-pipeline PS in this directory binds CB12 as a per-frame
// constant block with an identical schema at slots [0..27]: ViewRotation,
// Projection, PrevFrame_ViewProj, ViewToWorld, WorldToView, depth recip,
// Far/Near reproject matrices. The same schema also holds for replacement
// shaders that participate in the deferred pipeline.
//
// This header expresses the shared portion as a macro that PSes can drop
// into their cbuffer declaration. The macro is byte-identical (in fxc
// output) to the prior inline declaration, so adopting it produces no
// regression in compiled bytecode.
//
// Audit / evidence: see `.agents/cb-schema-audit-findings.md` for the full
// per-slot cross-consistency table and runtime evidence citations.

#ifndef DEFERRED_CONTRACTS_HLSLI_INCLUDED
#define DEFERRED_CONTRACTS_HLSLI_INCLUDED

// ============================================================================
// CB12[0..27]: shared per-frame block.
//
// Field layout (per runtime evidence, FO4_frame5407.rdc + FO4_frame9483.rdc):
//   [0..2]   ViewRotation rows (orthonormal 3x3, world -> view)
//   [3]      ViewMatrix_row3 (homogeneous identity (0,0,0,1))
//   [4..7]   Projection rows; [5].z TAA-patched mid-frame
//   [8..10]  PrevFrame_ViewProj; [9] TAA-patched
//   [11]     duplicate of [2]
//   [12..14] ViewToWorld rows (transpose of [0..2])
//   [15]     ViewToWorld_row3 (homogeneous identity continuation)
//   [16..18] WorldToView extended block (camera_pos partial in .w); [18] TAA
//   [19]     Depth reciprocal params (near recip + depth-range recip)
//   [20..23] Far reprojection matrix (selected when depth >= 0.01)
//   [24..27] Near reprojection matrix (selected when depth < 0.01)
//
// Field-naming choice (important for fxc round-trip):
//   PSes that have not assigned per-row names to the [0..19] block declare
//   it as `cb12_pad_0_19[20]` (a 20-element array). This is the historical
//   pattern in `bsdf_light_deferred.hlsl` and `vls_slice_scatter.hlsl`. The
//   macro below preserves that exact declaration so fxc bytecode (including
//   the RDEF reflection chunk) is byte-identical to the prior inline form.
//
//   PSes that name additional rows individually (ambient names
//   ViewToWorld_row0/1/2; composite repurposes slot [14] as a fog-plane
//   accessor; prepass uses a wider [0..29] pad block) cannot adopt this
//   exact macro without changing their reflection metadata. They keep their
//   inline shape. The schema documented here is still authoritative for
//   them; only the textual sharing is deferred.
//
// Slot-access convenience macros (CB12_VIEW_TO_WORLD_ROW0(pad), etc.) are
// intentionally NOT provided. New replacement shaders that need named
// per-row access should write their own inline declaration matching this
// schema rather than index into a padding array.
// ============================================================================

#define DEFERRED_PERFRAME_CB12_SHARED_BLOCK \
    float4 cb12_pad_0_19[20]; \
    float4 FarReproj_row0; \
    float4 FarReproj_row1; \
    float4 FarReproj_row2; \
    float4 FarReproj_row3; \
    float4 NearReproj_row0; \
    float4 NearReproj_row1; \
    float4 NearReproj_row2; \
    float4 NearReproj_row3

// ============================================================================
// CB2[0]: ScreenSize convention for full-screen passes.
//
// Layout: .xy = RcpFrameDim (1/width, 1/height); .zw = FrameDim (width, height)
// when populated for full-screen passes. Some per-call uses populate only .xy.
//
// Consistent across ambient / bsdf-dir / bsdf-pt / composite (all 4
// full-screen PSes here). VLS slice uses the same shape but at CB0[0]
// rather than CB2[0]. Prepass (geometry pass) does NOT have ScreenSize at
// CB2[0]; CB2 there carries per-call material data instead.
//
// Replacement-shader authors writing full-screen quad passes should
// declare `float4 ScreenSize : packoffset(c0);` (or equivalent) at CB2[0].
// ============================================================================

// Octahedral normal decode is NOT factored here. The local implementations
// in ambient and bsdf (both branches) use the FO4-specific encoding form
// (`enc * 4 - 2` with `sqrt(1 - encDotEnc * 0.25)`), not the Cigolle et al
// JCGT octahedral form. De-duplicating into this header would change fxc
// reflection for at least one of the consuming PSes (the inline forms
// differ in trivial ways that affect register naming). Replacement authors
// should mirror the existing local helper from the closest analog PS.

#endif  // DEFERRED_CONTRACTS_HLSLI_INCLUDED
