#include "Render/GBufferViews.h"

#include "Render/Engine.h"
#include "Render/ShaderInjection.h"
#include "Render/SubstrateSlots.h"

#include <array>

namespace cs::engine
{
	void BindGBufferViews(ID3D11DeviceContext* a_context) noexcept
	{
		using enum RE::BSGraphics::RenderTargetID;
		const std::array<ID3D11ShaderResourceView*, render::kGBufferViewCount> views{
			GetRenderTargetSRV(kGBufferAlbedoSpecular),
			GetRenderTargetSRV(kGBufferNormal),
			GetRenderTargetSRV(kGBufferAmbient),
			GetRenderTargetSRV(kGBufferEmissive)
		};
		BindInjectionShaderResources(a_context, render::kGBufferAlbedoSlot, render::kGBufferViewCount, views.data());
	}
}
