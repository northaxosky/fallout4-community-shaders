#pragma once

#include "Render/TemporalProvider.h"

namespace cs::features
{
	class Streamline;

	class StreamlinePresentation final : public render::temporal::IFrameGenerationProvider
	{
	public:
		enum class Method
		{
			kDLSSG,
			kFSRG
		};

		StreamlinePresentation(
			Streamline& a_runtime, Method a_method) noexcept;

		[[nodiscard]] const char* Name() const noexcept override;
		[[nodiscard]] render::temporal::ProviderResult
		PrepareDevice(ID3D12Device** a_device) override;
		[[nodiscard]] render::temporal::ProviderResult
		PrepareFactory(IDXGIFactory4** a_factory) override;
		[[nodiscard]] render::temporal::ProviderResult CreatePresentation(
			const render::temporal::PresentationCreateContext& a_context,
			IDXGISwapChain4** a_swapChain) override;
		[[nodiscard]] render::temporal::ProviderResult
		SetPresentationActive(bool a_active) override;
		[[nodiscard]] render::temporal::ProviderResult
		CreateDisplayResources(std::uint32_t a_width, std::uint32_t a_height,
			DXGI_FORMAT a_format,
			std::uint32_t a_bufferCount) override;
		[[nodiscard]] render::temporal::ProviderResult PrepareFrame(
			const render::temporal::FrameGenerationRequest& a_request) override;
		[[nodiscard]] render::temporal::ProviderResult CancelFrame(
			const render::temporal::FrameGenerationRequest& a_request) override;
		[[nodiscard]] render::temporal::ProviderResult
		SetGenerationEnabled(bool a_enabled) override;
		[[nodiscard]] render::temporal::ProviderResult
		CollectPresentStatus(UINT a_presentFlags, HRESULT a_presentResult) override;
		[[nodiscard]] std::optional<std::uint32_t>
		ConsumeGeneratedFrameCount() noexcept override;
		[[nodiscard]] std::optional<std::uint32_t>
		ConsumePresentedFrameCount() noexcept override;
		[[nodiscard]] std::optional<render::temporal::GpuCompletionDependency>
		ConsumePresentInputCompletionDependency() noexcept override;
		[[nodiscard]] render::temporal::ProviderResult
		Sleep(std::uint32_t a_frame) override;
		[[nodiscard]] render::temporal::ProviderResult
		SetLatencyMarker(render::temporal::LatencyMarker a_marker,
			std::uint32_t a_frame) override;
		[[nodiscard]] render::temporal::ProviderResult Quiesce() override;
		[[nodiscard]] render::temporal::ProviderResult
		ReleaseDisplayResources() noexcept override;
		[[nodiscard]] render::temporal::ProviderResult
		DestroyAfterDrain() noexcept override;
		[[nodiscard]] bool IsAvailable() const noexcept override;
		[[nodiscard]] bool IsReady() const noexcept override;

	private:
		Streamline& _runtime;
		Method _method;
		std::uint32_t _width = 0;
		std::uint32_t _height = 0;
		std::uint32_t _bufferCount = 0;
		bool _enabled = false;
		bool _ready = false;
		bool _presentationActive = false;
	};

}  // namespace cs::features
