#pragma once

#include "Render/FrameBuffer.h"

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11ShaderResourceView;

namespace cs::render
{
	void InitializeCanonicalDepth(ID3D11Device* a_device);
	void UpdateCanonicalDepth(ID3D11DeviceContext* a_context, const engine::WorldCameraRecord& a_camera);
	// Canonical depth of the main depth target after upscaling; null if missing.
	[[nodiscard]] ID3D11ShaderResourceView* UpdateOutputCanonicalDepth(ID3D11DeviceContext* a_context, const engine::WorldCameraRecord& a_camera);
	[[nodiscard]] ID3D11ShaderResourceView* GetCanonicalSceneDepthSRV() noexcept;
}
