#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <string_view>
#include <vector>

#include "Render/TemporalProvider.h"

namespace cs::render::temporal
{
	enum class LatencyBackend : std::uint8_t
	{
		kNone,
		kReflex,
		kAntiLag2
	};

	[[nodiscard]] constexpr std::string_view Name(LatencyBackend a_backend) noexcept
	{
		switch (a_backend) {
		case LatencyBackend::kReflex:
			return "NVIDIA Reflex";
		case LatencyBackend::kAntiLag2:
			return "AMD Radeon Anti-Lag 2";
		case LatencyBackend::kNone:
			break;
		}
		return "none";
	}

	struct LatencyBackendStatus
	{
		LatencyBackend backend = LatencyBackend::kNone;
		LatencyDiagnostics diagnostics;
	};

	[[nodiscard]] inline const LatencyDiagnostics& FindDiagnostics(
		std::span<const LatencyBackendStatus> a_backends,
		LatencyBackend a_backend) noexcept
	{
		static const LatencyDiagnostics none;
		for (const auto& status : a_backends) {
			if (status.backend == a_backend) {
				return status.diagnostics;
			}
		}
		return none;
	}

	// Routes to the first backend whose driver reports low latency.
	class LatencyRouter final : public ILatencyProvider
	{
	public:
		struct Entry
		{
			LatencyBackend backend = LatencyBackend::kNone;
			ILatencyProvider* provider = nullptr;
		};

		// Order is priority; the list is fixed for the router's lifetime.
		LatencyRouter(std::initializer_list<Entry> a_backends);

		[[nodiscard]] ProviderResult Configure(const LatencyOptions& a_options) override;
		[[nodiscard]] ProviderResult Sleep(std::uint32_t a_frame) override;
		[[nodiscard]] ProviderResult
		SetLatencyMarker(LatencyMarker a_marker, std::uint32_t a_frame) override;
		[[nodiscard]] LatencyDiagnostics GetLatencyDiagnostics() const noexcept override;
		[[nodiscard]] bool IsAvailable() const noexcept override;
		void BindPresentation(const PresentationBinding& a_binding) noexcept override;
		void UnbindPresentation() noexcept override;
		void QuarantinePresentation() noexcept override;

		[[nodiscard]] LatencyBackend Selected() const noexcept;
		[[nodiscard]] std::vector<LatencyBackendStatus> BackendStatus() const;

	private:
		static constexpr std::size_t kNoBackend = static_cast<std::size_t>(-1);

		const std::vector<Entry> _backends;
		std::atomic<std::size_t> _selected{ kNoBackend };
	};
}
