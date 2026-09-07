#pragma once

#include "Render/TemporalProvider.h"

namespace cs::features
{
	class FidelityFX;
	class Streamline;

	class FidelityFXSuperResolution final :
		public render::temporal::ISuperResolutionProvider
	{
	public:
		explicit FidelityFXSuperResolution(FidelityFX& a_runtime) noexcept;

		[[nodiscard]] const char* Name() const noexcept override;
		[[nodiscard]] render::temporal::ProviderResult Initialize(
			const render::temporal::SuperResolutionInitContext& a_context) override;
		[[nodiscard]] render::temporal::SuperResolutionSizeResult QueryRenderSize(
			const render::temporal::SuperResolutionSizeRequest& a_request) override;
		[[nodiscard]] render::temporal::ProviderResult Record(
			const render::temporal::SuperResolutionRequest& a_request) override;
		void DestroyAfterDrain() noexcept override;

	private:
		FidelityFX& _runtime;
		render::temporal::SuperResolutionSizeCache _sizeCache;
	};

	class StreamlineSuperResolution final :
		public render::temporal::ISuperResolutionProvider
	{
	public:
		explicit StreamlineSuperResolution(Streamline& a_runtime) noexcept;

		[[nodiscard]] const char* Name() const noexcept override;
		[[nodiscard]] render::temporal::ProviderResult Initialize(
			const render::temporal::SuperResolutionInitContext& a_context) override;
		[[nodiscard]] render::temporal::SuperResolutionSizeResult QueryRenderSize(
			const render::temporal::SuperResolutionSizeRequest& a_request) override;
		[[nodiscard]] render::temporal::ProviderResult Record(
			const render::temporal::SuperResolutionRequest& a_request) override;
		void DestroyAfterDrain() noexcept override;

	private:
		Streamline& _runtime;
		render::temporal::SuperResolutionSizeCache _sizeCache;
	};
}
