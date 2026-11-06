// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 northaxosky

// Multiplies the target through blending: dst * Scale.
cbuffer SeedScale : register(b0)
{
	float4 Scale;
};

float4 main(float4 position : SV_POSITION) : SV_Target0
{
	return float4(Scale.xxx, 1.0);
}
