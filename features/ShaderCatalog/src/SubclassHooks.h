#pragma once

namespace cs::features::catalog::subclass_hooks
{
	// Patches vtable slot 0x0B (BSShader::ReloadShaders) on each known concrete BSShader
	// subclass. Each thunk pushes a TLS Scope naming the subclass, then chains to the
	// original (shared base) implementation, which loads the per-fxp technique permutations
	// and ultimately calls ID3D11Device::CreatePixelShader. Our device-vtable hook reads the
	// TLS context to attribute the resulting catalog row.
	//
	// Idempotent: guarded by a process-wide once-flag.
	void InstallAll();

	// Diagnostics surface for the feature panel.
	struct InstallStats
	{
		unsigned attempted = 0;
		unsigned succeeded = 0;
		unsigned failed    = 0;
	};
	InstallStats GetInstallStats();
}
