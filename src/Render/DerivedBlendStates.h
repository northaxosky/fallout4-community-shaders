#pragma once

#include <d3d11.h>
#include <winrt/base.h>

#include <mutex>
#include <unordered_map>

namespace cs::engine
{
	// Edits a copy of the source desc; false leaves the source state in place.
	using BlendDescMutator = bool (*)(D3D11_BLEND_DESC& a_desc);

	// Blend states derived from engine ones, built once per source state.
	class DerivedBlendStates
	{
	public:
		DerivedBlendStates(BlendDescMutator a_mutate, const char* a_name) noexcept :
			_mutate(a_mutate), _name(a_name) {}

		// The derived state, or a_source when the mutator keeps it.
		[[nodiscard]] ID3D11BlendState* Get(ID3D11BlendState* a_source);

	private:
		struct Entry
		{
			winrt::com_ptr<ID3D11BlendState> source;   // Pins the cache key.
			winrt::com_ptr<ID3D11BlendState> derived;  // Null when the source is kept.
		};

		[[nodiscard]] Entry Build(ID3D11BlendState* a_source) const;

		BlendDescMutator _mutate;
		const char* _name;
		std::mutex _lock;
		std::unordered_map<ID3D11BlendState*, Entry> _entries;
	};
}
