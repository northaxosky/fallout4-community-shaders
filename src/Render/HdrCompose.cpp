#include "Render/HdrCompose.h"

#include "Log.h"
#include "Render/Annotation.h"
#include "Render/RendererContext.h"
#include "Render/ShaderStage.h"
#include "Render/SharedData.h"
#include "Utils/ShaderCompile.h"

#include <format>

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

		winrt::com_ptr<ID3D11ComputeShader> CompileCompute(ID3D11Device& a_device, const wchar_t* a_path, const char* a_name)
		{
			// Compiled directly: the shader cache and engine device are not up at chain creation.
			std::string error;
			const auto blob = util::CompileShaderToBlob(a_path, {}, "cs_5_0", "main", &error);
			if (!blob) {
				L->error("{} failed to compile: {}", a_name, error);
				return nullptr;
			}
			winrt::com_ptr<ID3D11ComputeShader> shader;
			const HRESULT result = a_device.CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, shader.put());
			if (FAILED(result)) {
				L->error("{} was rejected by the device: {:#010x}", a_name, static_cast<std::uint32_t>(result));
				return nullptr;
			}
			annotation::SetName(shader.get(), std::format("Render/HdrCompose.{}", a_name));
			return shader;
		}
	}

	bool Compose::Initialize(ID3D11Device& a_device)
	{
		Reset();
		D3D11_BUFFER_DESC desc{};
		desc.ByteWidth = sizeof(PerFrame);
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		_shader = CompileCompute(a_device, L"Data\\Shaders\\HDRDisplay\\HDROutputCS.hlsl", "HDROutputCS");
		_encodeInterface = CompileCompute(a_device, L"Data\\Shaders\\HDRDisplay\\UIBrightnessCS.hlsl", "UIBrightnessCS");
		_compositeInterface = CompileCompute(a_device, L"Data\\Shaders\\HDRDisplay\\InterfaceCompositePQCS.hlsl", "InterfaceCompositePQCS");
		if (!_shader || !_encodeInterface || !_compositeInterface || FAILED(a_device.CreateBuffer(&desc, nullptr, _constants.put()))) {
			Reset();
			return false;
		}
		annotation::SetName(_constants.get(), "Render/HdrCompose.Constants");
		return true;
	}

	void Compose::Reset() noexcept
	{
		_shader = nullptr;
		_encodeInterface = nullptr;
		_compositeInterface = nullptr;
		_constants = nullptr;
	}

	bool Compose::UpdateConstants(ID3D11DeviceContext& a_context, const OutputSettings& a_settings, bool a_skipInterface)
	{
		D3D11_MAPPED_SUBRESOURCE mapped{};
		if (FAILED(a_context.Map(_constants.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
			return false;
		*static_cast<PerFrame*>(mapped.pData) = {
			.enableHDR = a_settings.enabled ? 1.0f : 0.0f,
			.paperWhite = a_settings.paperWhite,
			.peakNits = a_settings.peakNits,
			.skipUIComposite = a_skipInterface ? 1.0f : 0.0f,
			.uiBrightness = a_settings.uiBrightness
		};
		a_context.Unmap(_constants.get(), 0);
		return true;
	}

	void Compose::Run(ID3D11DeviceContext& a_context, ID3D11ComputeShader& a_shader,
		std::span<ID3D11ShaderResourceView* const> a_inputs, ID3D11UnorderedAccessView& a_output, UINT a_width, UINT a_height)
	{
		// The inputs may still be bound as the engine's targets.
		engine::ComputeOMScope scope(&a_context);
		auto* constants = _constants.get();
		auto* output = &a_output;
		a_context.CSSetConstantBuffers(0, 1, &constants);
		if (!a_inputs.empty())
			a_context.CSSetShaderResources(0, static_cast<UINT>(a_inputs.size()), a_inputs.data());
		a_context.CSSetUnorderedAccessViews(0, 1, &output, nullptr);
		a_context.CSSetShader(&a_shader, nullptr, 0);
		BindSharedData(&a_context, engine::ShaderStage::kCompute);
		a_context.Dispatch((a_width + 7) / 8, (a_height + 7) / 8, 1);
	}

	void Compose::Dispatch(ID3D11DeviceContext& a_context, ID3D11ShaderResourceView& a_scene,
		ID3D11ShaderResourceView* a_interface, ID3D11UnorderedAccessView& a_output,
		UINT a_width, UINT a_height, const OutputSettings& a_settings)
	{
		if (!UpdateConstants(a_context, a_settings, !a_interface))
			return;
		annotation::ScopedEvent annotationScope("Render::HdrCompose", false);
		ID3D11ShaderResourceView* inputs[]{ &a_scene, a_interface ? a_interface : &a_scene };
		Run(a_context, *_shader, inputs, a_output, a_width, a_height);
	}

	void Compose::EncodeInterface(ID3D11DeviceContext& a_context, ID3D11UnorderedAccessView& a_interface,
		UINT a_width, UINT a_height, const OutputSettings& a_settings)
	{
		if (!UpdateConstants(a_context, a_settings, true))
			return;
		annotation::ScopedEvent annotationScope("Render::HdrCompose::EncodeInterface", false);
		Run(a_context, *_encodeInterface, {}, a_interface, a_width, a_height);
	}

	void Compose::CompositeInterface(ID3D11DeviceContext& a_context, ID3D11ShaderResourceView& a_hudless,
		ID3D11ShaderResourceView& a_interface, ID3D11UnorderedAccessView& a_output, UINT a_width, UINT a_height)
	{
		annotation::ScopedEvent annotationScope("Render::HdrCompose::CompositeInterface", false);
		ID3D11ShaderResourceView* inputs[]{ &a_hudless, &a_interface };
		Run(a_context, *_compositeInterface, inputs, a_output, a_width, a_height);
	}
}
