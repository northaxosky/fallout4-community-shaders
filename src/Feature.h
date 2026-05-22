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
		virtual std::vector<std::string_view> GetDependencies() const { return {}; }
		virtual bool IsInstalled() const;

		bool IsLoaded() const noexcept { return _loaded; }

		virtual void Load() {}
		virtual void OnDataLoaded() {}

		// Runs after all features' Load(). Defer here to wrap hooks installed in another feature's Load().
		virtual void OnPostPostLoad() {}

		virtual void DrawSettings() {}

		// Always-on overlay rendered on top of the game even when the settings menu is closed.
		virtual void DrawOverlay() {}

		// Fired by cs::Streamline once the D3D11 device exists and the SDK is initialized.
		virtual void OnD3D11Ready(IDXGIAdapter* /*adapter*/, ID3D11Device* /*device*/) {}

	private:
		friend class FeatureManager;
		void SetLoaded(bool a_loaded) noexcept { _loaded = a_loaded; }

		bool _loaded = false;
	};

	class FeatureManager
	{
	public:
		static FeatureManager& Get();

		void Register(Feature* a_feature);

		void LoadAll();
		void OnDataLoadedAll();
		void OnPostPostLoadAll();

		const std::vector<Feature*>& GetAll() const noexcept { return _loadedFeatures; }

	private:
		FeatureManager() = default;
		std::vector<Feature*> _features;
		std::vector<Feature*> _loadedFeatures;
	};
}
