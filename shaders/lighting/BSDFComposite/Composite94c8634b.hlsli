// SPDX-License-Identifier: GPL-3.0-or-later
// Native provenance is AE 1.11.221 Shaders011.fxp offset 11570348 only; this source does not claim an OG or NG contract.
// Native DXBC: SHA-1 94c8634be6709fdba723d668852a2deadfa48ddd, SHA-256 f55510978fc86bf532abf529b6bcf695c9b0bbef78b251ceeaed01f7c6d9d247, 284 bytes, ps_5_0.
// Opaque archive shader-key aliases and macro sets are deliberately not attached
// here; derive aliases from the corrected receipt-backed ledger and do not assign
// raw-technique semantics to archive keys.
// STATUS: static complete-DXBC identity only; no execution or conformance claim.
// EXACT RESULT: fxc 10.0.26100.0 /T ps_5_0 /E main /O3 /Qstrip_reflect reproduces the native 284-byte DXBC byte-for-byte.

struct PS_INPUT
{
    float4 position : SV_POSITION;
};

struct PS_OUTPUT
{
    float4 color : SV_Target0;
    float4 secondary : SV_Target1;
};

PS_OUTPUT main(PS_INPUT input)
{
    PS_OUTPUT output;
    output.color = 0.0;
    output.secondary = 0.0;
    return output;
}
