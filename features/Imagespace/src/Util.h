#pragma once

#include <d3d11.h>
#include <utility>
#include <vector>

namespace cs::features::imagespace::Util
{
	enum class RenderTarget
	{
		kFrameBuffer = 0,
	};

	[[nodiscard]] inline RE::BSGraphics::State* State_GetSingleton()
	{
		static REL::Relocation<RE::BSGraphics::State*> singleton{ REL::ID({ 600795, 2704621, 2704621 }) };
		return singleton.get();
	}

	// Returns nullptr if the renderer's device isn't available yet.
	[[nodiscard]] inline ID3D11Device* GetD3DDevice()
	{
		auto* data = RE::BSGraphics::GetRendererData();
		if (!data || !data->device)
			return nullptr;
		return reinterpret_cast<ID3D11Device*>(data->device);
	}

	ID3D11DeviceChild* CompileShader(const wchar_t* a_filePath,
		const std::vector<std::pair<const char*, const char*>>& a_defines,
		const char* a_programType,
		const char* a_program = "main");
}
