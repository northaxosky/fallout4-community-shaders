#include "UITextureIsolation.h"

#include "Log.h"

namespace cs::features
{
	namespace { auto* L = cs::log::Get("cs.feature.uitex"); }

	// kUI = 17, kUITemp = 18 in BSGraphics::RendererData::renderTargets.
	static constexpr unsigned int kEngineUIIndex     = 17;
	static constexpr unsigned int kEngineUITempIndex = 18;

	UITextureIsolation* UITextureIsolation::Get()
	{
		static UITextureIsolation instance;
		return &instance;
	}

	void UITextureIsolation::Load()
	{
		L->info("Loaded; observation hook deferred until D3D11 device available");
	}

	void UITextureIsolation::OnD3D11Ready(ID3D11Device*, ID3D11DeviceContext* a_context)
	{
		UITextureIsolationDetail::InstallOMSetRenderTargetsObserver(a_context);
	}

	bool UITextureIsolation::MatchesEngineUI(ID3D11RenderTargetView* a_rtv) const noexcept
	{
		if (!a_rtv)
			return false;
		auto* rd = RE::BSGraphics::GetRendererData();
		if (!rd)
			return false;
		auto* uiRTV     = reinterpret_cast<ID3D11RenderTargetView*>(rd->renderTargets[kEngineUIIndex].rtView);
		auto* uiTempRTV = reinterpret_cast<ID3D11RenderTargetView*>(rd->renderTargets[kEngineUITempIndex].rtView);
		return a_rtv == uiRTV || a_rtv == uiTempRTV;
	}

	namespace
	{
		struct AutoRegister
		{
			AutoRegister()
			{
				FeatureManager::Get().Register(UITextureIsolation::Get());
			}
		};
		static AutoRegister _autoRegister;
	}
}
