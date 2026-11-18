#include "Render/DerivedBlendStates.h"

#include "Log.h"

#include <format>

namespace cs::render
{
	namespace
	{
		auto* L = cs::log::Get("cs.render.derivedblend");
	}

	DerivedBlendStateCache::Entry DerivedBlendStateCache::Create(
		ID3D11BlendState* a_source, const D3D11_BLEND_DESC& a_sourceDesc, const std::optional<D3D11_BLEND_DESC>& a_derived)
	{
		Entry entry;
		entry.source.copy_from(a_source);
		entry.description = DescribeBlendState(a_sourceDesc);
		if (!a_derived)
			return entry;
		winrt::com_ptr<ID3D11Device> device;
		a_source->GetDevice(device.put());
		if (FAILED(device->CreateBlendState(&*a_derived, entry.derived.put()))) {
			L->error("Could not create the derived blend state for {}.", static_cast<const void*>(a_source));
			return entry;
		}
		// D3D11 returns the same object for an identical descriptor.
		if (entry.derived.get() == a_source)
			entry.derived = nullptr;
		else
			_derivedStates.insert(entry.derived.get());
		return entry;
	}

	bool DerivedBlendStateCache::IsDerived(ID3D11BlendState* a_state) const
	{
		const std::scoped_lock lock{ _lock };
		return _derivedStates.contains(a_state);
	}

	std::vector<DerivedBlendStateCache::Census> DerivedBlendStateCache::TakeCensus() const
	{
		const std::scoped_lock lock{ _lock };
		std::vector<Census> census;
		census.reserve(_entries.size());
		for (const auto& [key, entry] : _entries)
			census.push_back({ entry.description, key.policy, entry.derived != nullptr, entry.uses });
		return census;
	}

	std::string DescribeBlendState(const D3D11_BLEND_DESC& a_desc)
	{
		auto text = std::format("independent={} a2c={}", a_desc.IndependentBlendEnable != FALSE, a_desc.AlphaToCoverageEnable != FALSE);
		const int targets = a_desc.IndependentBlendEnable ? 8 : 1;
		for (int slot = 0; slot < targets; ++slot) {
			const auto& target = a_desc.RenderTarget[slot];
			text += std::format(" rt{}=mask{:X}", slot, target.RenderTargetWriteMask);
			if (target.BlendEnable)
				text += std::format(",blend{}/{}", static_cast<int>(target.SrcBlend), static_cast<int>(target.DestBlend));
		}
		return text;
	}
}
