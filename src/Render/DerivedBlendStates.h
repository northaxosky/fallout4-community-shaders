#pragma once

#include <d3d11.h>
#include <winrt/base.h>

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cs::render
{
	// Derived blend states, built once per (source state, policy).
	class DerivedBlendStateCache
	{
	public:
		struct Census
		{
			std::string description;
			std::uint32_t policy = 0;
			bool derived = false;
			std::uint64_t uses = 0;
		};

		// Returns a_source itself when a_derive yields no descriptor.
		template <class Derive>
		[[nodiscard]] ID3D11BlendState* Resolve(ID3D11BlendState* a_source, std::uint32_t a_policy, Derive&& a_derive)
		{
			const std::scoped_lock lock{ _lock };
			auto found = _entries.find({ a_source, a_policy });
			if (found == _entries.end()) {
				D3D11_BLEND_DESC desc{};
				a_source->GetDesc(&desc);
				found = _entries.emplace(Key{ a_source, a_policy }, Create(a_source, desc, a_derive(desc))).first;
			}
			++found->second.uses;
			auto* derived = found->second.derived.get();
			return derived ? derived : a_source;
		}

		[[nodiscard]] bool IsDerived(ID3D11BlendState* a_state) const;
		[[nodiscard]] std::vector<Census> TakeCensus() const;

	private:
		struct Key
		{
			ID3D11BlendState* source;
			std::uint32_t policy;
			bool operator==(const Key&) const = default;
		};

		struct KeyHash
		{
			std::size_t operator()(const Key& a_key) const noexcept
			{
				return std::hash<const void*>{}(a_key.source) * 131U + a_key.policy;
			}
		};

		struct Entry
		{
			winrt::com_ptr<ID3D11BlendState> source;   // Pins the cache key.
			winrt::com_ptr<ID3D11BlendState> derived;  // Null when the source needs no change.
			std::string description;
			std::uint64_t uses = 0;
		};

		Entry Create(ID3D11BlendState* a_source, const D3D11_BLEND_DESC& a_sourceDesc, const std::optional<D3D11_BLEND_DESC>& a_derived);

		mutable std::mutex _lock;
		std::unordered_map<Key, Entry, KeyHash> _entries;
		std::unordered_set<const ID3D11BlendState*> _derivedStates;
	};

	[[nodiscard]] std::string DescribeBlendState(const D3D11_BLEND_DESC& a_desc);
}
