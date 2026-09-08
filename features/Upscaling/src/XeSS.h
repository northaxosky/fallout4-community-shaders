#pragma once

#include <cstdint>
#include <optional>

#include <d3d11_4.h>
#include <d3d12.h>
#include <winrt/base.h>

#include <xess.h>
#include <xess_d3d11.h>
#include <xess_d3d12.h>

#include "Render/TemporalProvider.h"

namespace cs::features
{
	class DX12SwapChain;

	class XeSSSuperResolution final :
		public render::temporal::ISuperResolutionProvider
	{
	public:
		static constexpr const wchar_t* PluginDir =
			L"Data\\Shaders\\Upscaling\\XeSS";

		~XeSSSuperResolution() override;

		[[nodiscard]] const char* Name() const noexcept override;
		[[nodiscard]] render::temporal::ProviderResult Initialize(
			const render::temporal::SuperResolutionInitContext& a_context) override;
		[[nodiscard]] render::temporal::SuperResolutionSizeResult QueryRenderSize(
			const render::temporal::SuperResolutionSizeRequest& a_request) override;
		[[nodiscard]] render::temporal::ProviderResult Preflight(
			ID3D11Device* a_conversionDevice,
			std::uint32_t a_renderWidth,
			std::uint32_t a_renderHeight,
			std::uint32_t a_outputWidth,
			std::uint32_t a_outputHeight,
			std::uint32_t a_qualityMode,
			bool a_contextDrained);
		[[nodiscard]] render::temporal::ProviderResult Record(
			const render::temporal::SuperResolutionRequest& a_request) override;
		void DestroyAfterDrain() noexcept override;
		[[nodiscard]] bool IsD3D12() const noexcept { return _d3d12; }
		[[nodiscard]] bool RequiresContextReinitialization(
			std::uint32_t a_outputWidth,
			std::uint32_t a_outputHeight,
			std::uint32_t a_qualityMode) const noexcept;

	private:
		friend class DX12SwapChain;
		using CreateD3D11Context =
			xess_result_t (*)(ID3D11Device*, xess_context_handle_t*);
		using InitD3D11 =
			xess_result_t (*)(xess_context_handle_t, const xess_d3d11_init_params_t*);
		using ExecuteD3D11 =
			xess_result_t (*)(xess_context_handle_t, const xess_d3d11_execute_params_t*);
		using CreateD3D12Context =
			xess_result_t (*)(ID3D12Device*, xess_context_handle_t*);
		using InitD3D12 =
			xess_result_t (*)(xess_context_handle_t, const xess_d3d12_init_params_t*);
		using ExecuteD3D12 =
			xess_result_t (*)(xess_context_handle_t, ID3D12GraphicsCommandList*, const xess_d3d12_execute_params_t*);
		using DestroyContext = xess_result_t (*)(xess_context_handle_t);
		using SetVelocityScale =
			xess_result_t (*)(xess_context_handle_t, float, float);
		using GetOptimalInputResolution = xess_result_t (*)(
			xess_context_handle_t,
			const xess_2d_t*,
			xess_quality_settings_t,
			xess_2d_t*,
			xess_2d_t*,
			xess_2d_t*);

		[[nodiscard]] bool LoadD3D11();
		[[nodiscard]] bool LoadD3D12();
		[[nodiscard]] bool IsIntelD3D11Device(ID3D11Device* a_device) const;
		[[nodiscard]] bool EnsureD3D11ConversionShaders(
			ID3D11Device* a_device);
		[[nodiscard]] bool EnsureNativeD3D11ConversionResources(
			ID3D11Device* a_device,
			std::uint32_t a_renderWidth,
			std::uint32_t a_renderHeight,
			std::uint32_t a_outputWidth,
			std::uint32_t a_outputHeight);
		[[nodiscard]] bool ConvertD3D11(
			ID3D11DeviceContext* a_context,
			ID3D11Resource* a_source,
			ID3D11ShaderResourceView* a_sourceSrv,
			ID3D11UnorderedAccessView* a_destination,
			ID3D11ComputeShader* a_shader,
			std::uint32_t a_width,
			std::uint32_t a_height);
		[[nodiscard]] static std::optional<xess_quality_settings_t> ToQuality(
			std::uint32_t a_quality);
		[[nodiscard]] render::temporal::ProviderResult EnsureCreatedContext();
		[[nodiscard]] render::temporal::ProviderResult EnsureContext(
			std::uint32_t a_outputWidth,
			std::uint32_t a_outputHeight,
			xess_quality_settings_t a_quality,
			bool a_contextDrained);

		HMODULE _module = nullptr;
		xess_context_handle_t _context = nullptr;
		CreateD3D11Context _createD3D11 = nullptr;
		InitD3D11 _initD3D11 = nullptr;
		ExecuteD3D11 _executeD3D11 = nullptr;
		CreateD3D12Context _createD3D12 = nullptr;
		InitD3D12 _initD3D12 = nullptr;
		ExecuteD3D12 _executeD3D12 = nullptr;
		DestroyContext _destroy = nullptr;
		SetVelocityScale _setVelocityScale = nullptr;
		GetOptimalInputResolution _getOptimalInputResolution = nullptr;
		bool _d3d12 = false;
		bool _contextInitialized = false;
		winrt::com_ptr<ID3D11Device> _device11;
		winrt::com_ptr<ID3D12Device> _device12;
		std::uint32_t _outputWidth = 0;
		std::uint32_t _outputHeight = 0;
		xess_quality_settings_t _quality = XESS_QUALITY_SETTING_QUALITY;
		render::temporal::SuperResolutionSizeCache _sizeCache;

		winrt::com_ptr<ID3D11ComputeShader> _decodeShader;
		winrt::com_ptr<ID3D11ComputeShader> _encodeShader;
		winrt::com_ptr<ID3D11Texture2D> _linearInput;
		winrt::com_ptr<ID3D11ShaderResourceView> _linearInputSrv;
		winrt::com_ptr<ID3D11UnorderedAccessView> _linearInputUav;
		winrt::com_ptr<ID3D11Texture2D> _linearOutput;
		winrt::com_ptr<ID3D11ShaderResourceView> _linearOutputSrv;
		winrt::com_ptr<ID3D11UnorderedAccessView> _linearOutputUav;
	};
}
