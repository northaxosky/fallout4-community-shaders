#pragma once

#include <d3d11.h>

#include "Engine.h"

namespace cs::features::ssgi::Util
{
	using RenderTarget = cs::engine::RenderTarget;
	using DepthStencilTarget = cs::engine::DepthStencilTarget;

	[[nodiscard]] inline RE::BSGraphics::State* State_GetSingleton()
	{
		static REL::Relocation<RE::BSGraphics::State*> singleton{ REL::ID({ 600795, 2704621, 2704621 }) };
		return singleton.get();
	}

	[[nodiscard]] inline ID3D11Device* GetD3DDevice()
	{
		auto* data = RE::BSGraphics::GetRendererData();
		if (!data || !data->device) return nullptr;
		return reinterpret_cast<ID3D11Device*>(data->device);
	}
}
