#pragma once

#include <string_view>
#include <vector>

struct IDXGIAdapter;
struct ID3D11Device;

namespace cs
{
	class Feature
	{
	public:
		virtual ~Feature() = default;

		virtual std::string_view GetName() const = 0;

		virtual void Load() {}
		virtual void OnDataLoaded() {}

		// Runs after every feature's Load(). Use this for hooks that must wrap a hook another
		// feature installs in its Load(): write_thunk_call patches in registration order, so
		// installing later means your thunk wraps theirs (e.g. Imagespace wrapping Upscaling).
		virtual void OnPostPostLoad() {}

		virtual void DrawSettings() {}

		// Always-on overlay rendered on top of the game even when the settings menu is closed.
		// Default empty; features that want a HUD-style widget override this.
		virtual void DrawOverlay() {}

		// Fired by cs::Streamline once the D3D11 device exists and the SDK is initialized.
		// Features cache feature-specific Streamline entry points here via slGetFeatureFunction.
		// Default empty; features that don't use Streamline ignore it.
		virtual void OnD3D11Ready(IDXGIAdapter* /*adapter*/, ID3D11Device* /*device*/) {}
	};

	class FeatureManager
	{
	public:
		static FeatureManager& Get();

		void Register(Feature* a_feature);

		void LoadAll();
		void OnDataLoadedAll();
		void OnPostPostLoadAll();

		const std::vector<Feature*>& GetAll() const noexcept { return _features; }

	private:
		FeatureManager() = default;
		std::vector<Feature*> _features;
	};
}
