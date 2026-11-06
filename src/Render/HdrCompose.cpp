#include "Render/HdrCompose.h"

#include "Log.h"
#include "Render/Annotation.h"
#include "Render/RendererContext.h"
#include "Render/ShaderStage.h"
#include "Render/SharedData.h"
#include "Utils/ShaderCompile.h"

namespace cs::render::hdr
{
	namespace
	{
		auto* L = cs::log::Get("cs.render.hdrcompose");

		// HDROutputCS PerFrame; the unused tail serves the UI and AutoHDR paths.
		struct alignas(16) PerFrame
		{
			float enableHDR, paperWhite, peakNits, skipUIComposite;
			float uiBrightness, isSceneLinear, isMainOrLoadingMenu, fgTweenMenuMidAlphaBoost;
			float previewSDR, applyAutoHDR, pad0, pad1;
		};
		static_assert(sizeof(PerFrame) == 48);
	}

	bool Compose::Initialize(ID3D11Device& a_device)
	{
		Reset();
		// Compiled directly: the shader cache and engine device are not up at chain creation.
		std::string error;
		const auto blob = util::CompileShaderToBlob(L"Data\\Shaders\\HDRDisplay\\HDROutputCS.hlsl", {}, "cs_5_0", "main", &error);
		if (!blob) {
			L->error("HDROutputCS failed to compile: {}", error);
			return false;
		}
		D3D11_BUFFER_DESC desc{};
		desc.ByteWidth = sizeof(PerFrame);
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		const HRESULT shader = a_device.CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, _shader.put());
		if (FAILED(shader) || FAILED(a_device.CreateBuffer(&desc, nullptr, _constants.put()))) {
			L->error("HDR compose resources were rejected by the device: {:#010x}", static_cast<std::uint32_t>(shader));
			Reset();
			return false;
		}
		annotation::SetName(_shader.get(), "Render/HdrCompose.CS");
		annotation::SetName(_constants.get(), "Render/HdrCompose.Constants");
		return true;
	}

	void Compose::Reset() noexcept
	{
		_shader = nullptr;
		_constants = nullptr;
	}

	void Compose::Dispatch(ID3D11DeviceContext& a_context, ID3D11ShaderResourceView& a_scene,
		ID3D11ShaderResourceView* a_interface, ID3D11UnorderedAccessView& a_output,
		UINT a_width, UINT a_height, const OutputSettings& a_settings)
	{
		D3D11_MAPPED_SUBRESOURCE mapped{};
		if (FAILED(a_context.Map(_constants.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
			return;
		*static_cast<PerFrame*>(mapped.pData) = {
			.enableHDR = a_settings.enabled ? 1.0f : 0.0f,
			.paperWhite = a_settings.paperWhite,
			.peakNits = a_settings.peakNits,
			.skipUIComposite = a_interface ? 0.0f : 1.0f,
			.uiBrightness = a_settings.uiBrightness
		};
		a_context.Unmap(_constants.get(), 0);

		annotation::ScopedEvent annotationScope("Render::HdrCompose", false);
		// The inputs may still be bound as the engine's targets.
		engine::ComputeOMScope scope(&a_context);
		auto* constants = _constants.get();
		auto* output = &a_output;
		ID3D11ShaderResourceView* inputs[]{ &a_scene, a_interface ? a_interface : &a_scene };
		a_context.CSSetConstantBuffers(0, 1, &constants);
		a_context.CSSetShaderResources(0, 2, inputs);
		a_context.CSSetUnorderedAccessViews(0, 1, &output, nullptr);
		a_context.CSSetShader(_shader.get(), nullptr, 0);
		BindSharedData(&a_context, engine::ShaderStage::kCompute);
		a_context.Dispatch((a_width + 7) / 8, (a_height + 7) / 8, 1);
	}
}
