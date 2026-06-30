// SPDX-License-Identifier: GPL-3.0-or-later WITH FO4-CS-Modding-Exception
// Shared per-frame contracts for FO4 deferred-lighting reconstructions.
// CB12 macro preserves fxc RDEF reflection by keeping the historical cb12_pad_0_19[20] shape.
// Evidence: `.agents/cb-schema-audit-findings.md` has the per-slot consistency table.

#ifndef DEFERRED_CONTRACTS_HLSLI_INCLUDED
#define DEFERRED_CONTRACTS_HLSLI_INCLUDED

// CB12[0..27]: [0..2] ViewRotation, [3] identity, [4..7] Projection, [8..10] PrevFrame_ViewProj, [11] dup, [12..15] ViewToWorld, [16..18] WorldToView, [19] depth reciprocals, [20..27] Far/Near reproj.
// Keep cb12_pad_0_19[20] for fxc RDEF stability; named-row shaders keep inline CBs to preserve reflection.
// Do not add slot-access macros; new shaders needing names should declare matching inline rows.

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

// Fullscreen ScreenSize convention: .xy = reciprocal dims, .zw = dims when populated.
// Ambient/BSDF/composite use CB2[0]; VLS uses CB0[0]; prepass CB2 is material data, not ScreenSize.

// Do not factor octa decode here: FO4 uses `enc * 4 - 2`, and sharing it would perturb fxc reflection/register names.

#endif  // DEFERRED_CONTRACTS_HLSLI_INCLUDED
