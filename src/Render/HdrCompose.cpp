#include "Render/HdrCompose.h"

#include "Render/Annotation.h"
#include "Render/RendererContext.h"
#include "Render/ShaderStage.h"
#include "Render/SharedData.h"

namespace cs::render::hdr
{
	namespace
	{
		// HDROutputCS PerFrame; the unused tail serves the UI and AutoHDR paths.
		struct alignas(16) PerFrame
		{
			float enableHDR, paperWhite, peakNits, skipUIComposite;
			float uiBrightness, isSceneLinear, isMainOrLoadingMenu, fgTweenMenuMidAlphaBoost;
			float previewSDR, applyAutoHDR, pad0, pad1;
		};
		static_assert(sizeof(PerFrame) == 48);
	}

	bool Compose::Initialize(ID3D11ComputeShader* a_shader)
	{
		Reset();
		if (!a_shader)
			return false;
		winrt::com_ptr<ID3D11Device> device;
		a_shader->GetDevice(device.put());
		D3D11_BUFFER_DESC desc{};
		desc.ByteWidth = sizeof(PerFrame);
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		if (!device || FAILED(device->CreateBuffer(&desc, nullptr, _constants.put())))
			return false;
		annotation::SetName(_constants.get(), "Render/HdrCompose.Constants");
		_shader.copy_from(a_shader);
		return true;
	}

	void Compose::Reset() noexcept
	{
		_shader = nullptr;
		_constants = nullptr;
	}

	void Compose::Dispatch(ID3D11DeviceContext& a_context, ID3D11ShaderResourceView& a_scene,
		ID3D11UnorderedAccessView& a_output, UINT a_width, UINT a_height, const OutputSettings& a_settings)
	{
		D3D11_MAPPED_SUBRESOURCE mapped{};
		if (FAILED(a_context.Map(_constants.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
			return;
		*static_cast<PerFrame*>(mapped.pData) = {
			.enableHDR = a_settings.enabled ? 1.0f : 0.0f,
			.paperWhite = a_settings.paperWhite,
			.peakNits = a_settings.peakNits,
			.skipUIComposite = 1.0f,
			.uiBrightness = a_settings.uiBrightness
		};
		a_context.Unmap(_constants.get(), 0);

		annotation::ScopedEvent annotationScope("Render::HdrCompose", false);
		// The scene is the proxy, which may still be bound as the engine's target.
		engine::ComputeOMScope scope(&a_context);
		auto* constants = _constants.get();
		auto* output = &a_output;
		ID3D11ShaderResourceView* inputs[]{ &a_scene, &a_scene };
		a_context.CSSetConstantBuffers(0, 1, &constants);
		a_context.CSSetShaderResources(0, 2, inputs);
		a_context.CSSetUnorderedAccessViews(0, 1, &output, nullptr);
		a_context.CSSetShader(_shader.get(), nullptr, 0);
		BindSharedData(&a_context, engine::ShaderStage::kCompute);
		a_context.Dispatch((a_width + 7) / 8, (a_height + 7) / 8, 1);
	}
}
