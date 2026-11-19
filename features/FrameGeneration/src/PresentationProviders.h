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
			kFSRG,
			kFSR4
		};

		StreamlinePresentation(
			Streamline& a_runtime, Method a_method) noexcept;

		[[nodiscard]] const char* Name() const noexcept override;
		[[nodiscard]] render::temporal::ProviderResult CreatePresentation(
			const render::temporal::PresentationCreateContext& a_context,
			IDXGISwapChain4** a_swapChain) override;
		[[nodiscard]] render::temporal::ProviderResult ReleasePresentation(
			winrt::com_ptr<IDXGISwapChain4>& a_swapChain) noexcept override;
		[[nodiscard]] render::temporal::ProviderResult
		CreateDisplayResources(std::uint32_t a_width, std::uint32_t a_height,
			const render::temporal::ColorContract& a_color,
			std::uint32_t a_bufferCount) override;
		[[nodiscard]] render::temporal::ProviderResult PrepareFrame(
			const render::temporal::FrameGenerationRequest& a_request) override;
		[[nodiscard]] render::temporal::ProviderResult Pause() override;
		[[nodiscard]] render::temporal::PresentStatus
		CollectPresentStatus(UINT a_syncInterval, UINT a_presentFlags,
			HRESULT a_presentResult) override;
		[[nodiscard]] render::temporal::ProviderResult
		DestroyAfterDrain() noexcept override;
		[[nodiscard]] render::temporal::ProviderResult
		ValidateConfiguration(
			const render::temporal::FrameGenerationConfiguration&
				a_configuration) const override;
		[[nodiscard]] bool PresentsSceneOnlyBackBuffer() const noexcept override;
		[[nodiscard]] bool IsAvailable() const noexcept override;
		[[nodiscard]] bool IsReady() const noexcept override;

	private:
		[[nodiscard]] render::temporal::ProviderResult SetPresentationActive(bool a_active);

		Streamline& _runtime;
		Method _method;
		winrt::com_ptr<ID3D12CommandQueue> _presentQueue;
		std::uint32_t _width = 0;
		std::uint32_t _height = 0;
		std::uint32_t _bufferCount = 0;
		bool _enabled = false;
		bool _ready = false;
		bool _presentationActive = false;
		render::temporal::FrameGenerationConfiguration _configuration;
		render::temporal::ColorContract _color = render::temporal::kFo4PostTonemapSdrContract;
		float _peakLuminanceNits = 0.0f;
	};

}  // namespace cs::features
