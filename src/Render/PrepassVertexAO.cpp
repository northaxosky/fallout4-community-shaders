#include "Render/PrepassVertexAO.h"

#include "Render/DerivedBlendStates.h"
#include "Render/ShaderInjection.h"

namespace cs::engine
{
	namespace
	{
		// MRT4 alpha is 1 - vertexAO, which hair cannot carry.
		constexpr std::size_t kVertexAOTarget = 4;

		bool MaskVertexAOAlpha(D3D11_BLEND_DESC& a_desc)
		{
			if (!(a_desc.RenderTarget[kVertexAOTarget].RenderTargetWriteMask & D3D11_COLOR_WRITE_ENABLE_ALPHA))
				return false;
			// A non-independent state applies RT0's blend to every target.
			if (!a_desc.IndependentBlendEnable) {
				for (auto& target : a_desc.RenderTarget)
					target = a_desc.RenderTarget[0];
				a_desc.IndependentBlendEnable = TRUE;
			}
			a_desc.RenderTarget[kVertexAOTarget].RenderTargetWriteMask &= ~D3D11_COLOR_WRITE_ENABLE_ALPHA;
			return true;
		}

		DerivedBlendStates g_hairBlend{ &MaskVertexAOAlpha, "PrepassVertexAO/HairBlend" };

		// Stock blends alpha into MRT4.a, which holds 1 - vertexAO.
		void MaskHairAlpha(ID3D11DeviceContext* a_context) noexcept
		{
			constexpr auto kTarget = ShaderInjectionTarget::kDeferredPrepass;
			if (!ActiveShaderInjectionVariantHasDefine(kTarget, "BLEND") ||
				!ActiveShaderInjectionVariantHasDefine(kTarget, "HAIR"))
				return;
			winrt::com_ptr<ID3D11BlendState> current;
			float factor[4];
			UINT sampleMask;
			a_context->OMGetBlendState(current.put(), factor, &sampleMask);
			if (!current)
				return;
			auto* masked = g_hairBlend.Get(current.get());
			if (masked == current.get())
				return;
			CaptureShaderInjectionBlendState(a_context);
			a_context->OMSetBlendState(masked, factor, sampleMask);
		}
	}

	void AttachPrepassVertexAO(ShaderReplacementRegistration& a_registration)
	{
		if (a_registration.targetId != ShaderInjectionTarget::kDeferredPrepass)
			return;
		a_registration.bind = &MaskHairAlpha;
		// The define is compile-time, so a disabled feature still needs the mask.
		a_registration.bindWhileDisabled = true;
	}
}
