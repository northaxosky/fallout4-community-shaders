#pragma once

#include <d3d11.h>
#include <dxgi1_6.h>
#include <winrt/base.h>

#include <span>

namespace cs::render::hdr
{
	// Display state the HDR Display feature publishes for the presenter.
	struct OutputSettings
	{
		bool enabled = false;
		float paperWhite = 203.0f;
		float peakNits = 1000.0f;
		float uiBrightness = 1.0f;
	};

	// The presenter's fp16 scene texture, drawn into while the interface is split.
	struct SceneTarget
	{
		ID3D11Texture2D* texture = nullptr;
		ID3D11ShaderResourceView* srv = nullptr;
		ID3D11RenderTargetView* rtv = nullptr;

		[[nodiscard]] explicit operator bool() const noexcept { return texture && srv && rtv; }
	};

	// The container stays HDR-capable when disabled; only the color space changes.
	[[nodiscard]] constexpr DXGI_COLOR_SPACE_TYPE ColorSpace(const OutputSettings& a_settings) noexcept
	{
		return a_settings.enabled ? DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 : DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
	}

	// Encodes the gamma 2.2 scene, where 1.0 is paper white, for the container.
	class Compose
	{
	public:
		// Compiles the compose shaders on the device; false if any cannot.
		[[nodiscard]] bool Initialize(ID3D11Device& a_device);
		void Reset() noexcept;
		[[nodiscard]] bool IsReady() const noexcept { return _shader && _constants; }

		// With a_interface it is composited at uiBrightness; else a_scene is the frame.
		void Dispatch(ID3D11DeviceContext& a_context, ID3D11ShaderResourceView& a_scene,
			ID3D11ShaderResourceView* a_interface, ID3D11UnorderedAccessView& a_output,
			UINT a_width, UINT a_height, const OutputSettings& a_settings);

		// Frame generation blends in the output encoding; the UI is encoded in place.
		void EncodeInterface(ID3D11DeviceContext& a_context, ID3D11UnorderedAccessView& a_interface,
			UINT a_width, UINT a_height, const OutputSettings& a_settings);

		// Blends the encoded interface over the encoded HUD-less frame.
		void CompositeInterface(ID3D11DeviceContext& a_context, ID3D11ShaderResourceView& a_hudless,
			ID3D11ShaderResourceView& a_interface, ID3D11UnorderedAccessView& a_output,
			UINT a_width, UINT a_height);

	private:
		[[nodiscard]] bool UpdateConstants(ID3D11DeviceContext& a_context, const OutputSettings& a_settings, bool a_skipInterface);
		void Run(ID3D11DeviceContext& a_context, ID3D11ComputeShader& a_shader,
			std::span<ID3D11ShaderResourceView* const> a_inputs, ID3D11UnorderedAccessView& a_output,
			UINT a_width, UINT a_height);

		winrt::com_ptr<ID3D11ComputeShader> _shader;
		winrt::com_ptr<ID3D11ComputeShader> _encodeInterface;
		winrt::com_ptr<ID3D11ComputeShader> _compositeInterface;
		winrt::com_ptr<ID3D11Buffer> _constants;
	};
}
