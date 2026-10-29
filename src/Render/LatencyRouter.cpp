#include "Render/LatencyRouter.h"

#include "Log.h"

namespace cs::render::temporal
{
	namespace
	{
		auto* L = cs::log::Get("cs.render.latency");
	}

	LatencyRouter::LatencyRouter(std::initializer_list<Entry> a_backends) :
		_backends(a_backends)
	{}

	ProviderResult LatencyRouter::Configure(const LatencyOptions& a_options)
	{
		auto selected = kNoBackend;
		ProviderResult result{ .code = ProviderResultCode::kUnavailable };
		for (std::size_t i = 0; i < _backends.size(); ++i) {
			auto& provider = *_backends[i].provider;
			const auto configured = provider.Configure(a_options);
			const auto diagnostics = provider.GetLatencyDiagnostics();
			if (diagnostics.lowLatencyAvailable) {
				selected = i;
				result = configured;
				break;
			}
		}
		const auto previous = _selected.exchange(selected, std::memory_order_acq_rel);
		if (previous == selected) {
			return result;
		}
		// A superseded backend must not keep pacing frames.
		if (previous != kNoBackend) {
			(void)_backends[previous].provider->Configure({});
		}
		L->info("Latency backend: {}",
			Name(selected == kNoBackend ? LatencyBackend::kNone : _backends[selected].backend));
		return result;
	}

	ProviderResult LatencyRouter::Sleep(std::uint32_t a_frame)
	{
		const auto selected = _selected.load(std::memory_order_acquire);
		if (selected == kNoBackend) {
			return { .code = ProviderResultCode::kUnavailable };
		}
		return _backends[selected].provider->Sleep(a_frame);
	}

	ProviderResult LatencyRouter::SetLatencyMarker(
		LatencyMarker a_marker, std::uint32_t a_frame)
	{
		ProviderResult result{ .code = ProviderResultCode::kUnavailable };
		for (const auto& entry : _backends) {
			if (!entry.provider->IsAvailable()) {
				continue;
			}
			auto delivered = entry.provider->SetLatencyMarker(a_marker, a_frame);
			if (result.code != ProviderResultCode::kFailure) {
				result = std::move(delivered);
			}
		}
		return result;
	}

	LatencyDiagnostics LatencyRouter::GetLatencyDiagnostics() const noexcept
	{
		const auto selected = _selected.load(std::memory_order_acquire);
		if (selected != kNoBackend) {
			return _backends[selected].provider->GetLatencyDiagnostics();
		}
		LatencyDiagnostics diagnostics;
		for (const auto& entry : _backends) {
			const auto reason = entry.provider->GetLatencyDiagnostics().unavailableReason;
			if (reason != LatencyUnavailableReason::kNone) {
				diagnostics.unavailableReason = reason;
				break;
			}
		}
		return diagnostics;
	}

	bool LatencyRouter::IsAvailable() const noexcept
	{
		for (const auto& entry : _backends) {
			if (entry.provider->IsAvailable()) {
				return true;
			}
		}
		return false;
	}

	void LatencyRouter::BindPresentation(const PresentationBinding& a_binding) noexcept
	{
		for (const auto& entry : _backends) {
			entry.provider->BindPresentation(a_binding);
		}
	}

	void LatencyRouter::UnbindPresentation() noexcept
	{
		for (const auto& entry : _backends) {
			entry.provider->UnbindPresentation();
		}
	}

	void LatencyRouter::QuarantinePresentation() noexcept
	{
		for (const auto& entry : _backends) {
			entry.provider->QuarantinePresentation();
		}
	}

	LatencyBackend LatencyRouter::Selected() const noexcept
	{
		const auto selected = _selected.load(std::memory_order_acquire);
		return selected == kNoBackend ? LatencyBackend::kNone : _backends[selected].backend;
	}

	std::vector<LatencyBackendStatus> LatencyRouter::BackendStatus() const
	{
		std::vector<LatencyBackendStatus> status;
		status.reserve(_backends.size());
		for (const auto& entry : _backends) {
			status.push_back({ entry.backend, entry.provider->GetLatencyDiagnostics() });
		}
		return status;
	}
}
