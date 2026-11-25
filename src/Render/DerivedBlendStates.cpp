#include "Render/DerivedBlendStates.h"

#include "Log.h"
#include "Render/Annotation.h"

namespace cs::engine
{
	namespace
	{
		auto* L = cs::log::Get("cs.render.derivedblendstates");
	}

	ID3D11BlendState* DerivedBlendStates::Get(ID3D11BlendState* a_source)
	{
		const std::scoped_lock lock{ _lock };
		auto found = _entries.find(a_source);
		if (found == _entries.end())
			found = _entries.emplace(a_source, Build(a_source)).first;
		auto* derived = found->second.derived.get();
		return derived ? derived : a_source;
	}

	DerivedBlendStates::Entry DerivedBlendStates::Build(ID3D11BlendState* a_source) const
	{
		Entry result;
		result.source.copy_from(a_source);
		D3D11_BLEND_DESC desc{};
		a_source->GetDesc(&desc);
		if (!_mutate(desc))
			return result;
		winrt::com_ptr<ID3D11Device> device;
		a_source->GetDevice(device.put());
		if (FAILED(device->CreateBlendState(&desc, result.derived.put())))
			L->error("{}: could not derive a blend state from {}.", _name, static_cast<const void*>(a_source));
		else
			cs::render::annotation::SetName(result.derived.get(), _name);
		return result;
	}
}
