#pragma once

#include "Feature.h"

#include <d3d11.h>

namespace cs::features
{
	class UITextureIsolation : public Feature
	{
	public:
		static UITextureIsolation* Get();

		std::string_view GetName() const override { return "UITextureIsolation"; }

		void Load() override;
		void OnD3D11Ready(ID3D11Device* a_device, ID3D11DeviceContext* a_context);

		// Always re-read the engine's current kUI/kUITemp RTV pointers - no caching, so engine
		// resource recreation (DRS / resolution scale change) can't stale us out.
		bool MatchesEngineUI(ID3D11RenderTargetView* a_rtv) const noexcept;
	};

	namespace UITextureIsolationDetail
	{
		void InstallOMSetRenderTargetsObserver(ID3D11DeviceContext* a_context);
	}
}
