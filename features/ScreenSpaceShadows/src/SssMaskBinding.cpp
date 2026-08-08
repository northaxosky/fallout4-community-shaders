#include "SssMaskBinding.h"

namespace cs::features::sss_mask_binding
{
	namespace
	{
		enum class ExistingBinding : std::uint8_t
		{
			kNone,
			kOwned,
			kForeign
		};

		bool SetAndVerify(
			const Api& a_api,
			std::uint32_t a_slot,
			ID3D11ShaderResourceView* a_view) noexcept
		{
			a_api.set(a_api.context, a_slot, a_view);
			ID3D11ShaderResourceView* verified = nullptr;
			a_api.get(a_api.context, a_slot, &verified);
			const bool valid = verified == a_view && verified != nullptr;
			if (verified)
				verified->Release();
			return valid;
		}

		ExistingBinding Probe(
			const Api& a_api,
			std::uint32_t a_slot,
			ID3D11ShaderResourceView* a_realMask,
			ID3D11ShaderResourceView* a_whiteFallback) noexcept
		{
			if (!a_api.context || !a_api.get)
				return ExistingBinding::kNone;
			ID3D11ShaderResourceView* current = nullptr;
			a_api.get(a_api.context, a_slot, &current);
			if (!current)
				return ExistingBinding::kNone;
			const bool owned =
				current == a_realMask || current == a_whiteFallback;
			current->Release();
			return owned
				? ExistingBinding::kOwned
				: ExistingBinding::kForeign;
		}

		void RestoreNull(
			const Api& a_api,
			std::uint32_t a_slot) noexcept
		{
			if (a_api.context && a_api.set)
				a_api.set(a_api.context, a_slot, nullptr);
		}
	}

	Result Bind(
		const Api& a_api,
		std::uint32_t a_slot,
		ID3D11ShaderResourceView* a_realMask,
		Extent a_realMaskExtent,
		bool a_realMaskReady,
		ID3D11ShaderResourceView* a_whiteFallback,
		Extent a_whiteFallbackExtent,
		Extent a_requiredExtent) noexcept
	{
		Result result;
		if (!a_api.context
			|| !a_api.get
			|| !a_api.set
			|| a_requiredExtent.width == 0
			|| a_requiredExtent.height == 0) {
			return result;
		}

		if (Probe(
				a_api,
				a_slot,
				a_realMask,
				a_whiteFallback)
			== ExistingBinding::kForeign) {
			return result;
		}

		auto* selected =
			a_realMaskReady
				&& a_realMask
				&& Covers(a_realMaskExtent, a_requiredExtent)
			? a_realMask
			: a_whiteFallback
					&& Covers(
						a_whiteFallbackExtent,
						a_requiredExtent)
				? a_whiteFallback
				: nullptr;
		result.source =
			!selected
				? Source::kNone
				: selected == a_realMask
					? Source::kRealMask
					: Source::kWhiteFallback;
		if (!selected)
			return result;

		result.validBinding = SetAndVerify(a_api, a_slot, selected);
		if (!result.validBinding
			&& selected != a_whiteFallback
			&& a_whiteFallback
			&& Covers(
				a_whiteFallbackExtent,
				a_requiredExtent)) {
			result.source = Source::kWhiteFallback;
			result.validBinding =
				SetAndVerify(a_api, a_slot, a_whiteFallback);
		}
		return result;
	}

	bool RestoreNullIfOwned(
		const Api& a_api,
		std::uint32_t a_slot,
		ID3D11ShaderResourceView* a_realMask,
		ID3D11ShaderResourceView* a_whiteFallback) noexcept
	{
		if (Probe(
				a_api,
				a_slot,
				a_realMask,
				a_whiteFallback)
			!= ExistingBinding::kOwned) {
			return false;
		}
		RestoreNull(a_api, a_slot);
		return true;
	}
}
