// SPDX-License-Identifier: GPL-3.0-or-later WITH FO4-CS-Modding-Exception
#ifndef DEFERRED_CONTRACTS_HLSLI_INCLUDED
#define DEFERRED_CONTRACTS_HLSLI_INCLUDED

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

#endif
