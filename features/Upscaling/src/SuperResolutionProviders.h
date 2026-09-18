#pragma once

#include "Render/TemporalProvider.h"

namespace cs::features
{
	class Streamline;

	class StreamlineSuperResolution final :
		public render::temporal::ISuperResolutionProvider
	{
	public:
		enum class Method : std::uint8_t
		{
			kDLSS,
			kFSR3,
			kFSR4
		};

		StreamlineSuperResolution(
			Streamline& a_runtime, Method a_method) noexcept;

		[[nodiscard]] const char* Name() const noexcept override;
		[[nodiscard]] render::temporal::ProviderResult Initialize(
			const render::temporal::SuperResolutionInitContext& a_context) override;
		[[nodiscard]] render::temporal::SuperResolutionSizeResult QueryRenderSize(
			const render::temporal::SuperResolutionSizeRequest& a_request) override;
		[[nodiscard]] render::temporal::ProviderResult Record(
			const render::temporal::SuperResolutionRequest& a_request) override;
		[[nodiscard]] render::temporal::ProviderResult
		DestroyAfterDrain() noexcept override;

	private:
		Streamline& _runtime;
		Method _method;
		render::temporal::SuperResolutionSizeCache _sizeCache;
	};
}
