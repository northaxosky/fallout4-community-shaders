#pragma once

#include "Feature.h"
#include "Sha1.h"

#include <atomic>
#include <cstddef>
#include <string>

struct IDXGIAdapter;
struct ID3D11Device;
struct ID3D11PixelShader;

namespace cs::features
{
	class ShaderCatalog : public Feature
	{
	public:
		static ShaderCatalog* GetSingleton();

		std::string_view GetName() const override { return "ShaderCatalog"; }
		std::string GetFeatureSummary() const override { return "Captures every shader the engine compiles into a SQLite catalog for inspection."; }
		std::string GetCategory() const override { return "Diagnostics"; }

		bool Configure(const toml::table& a_config, std::string& a_error) override;
		bool HasCapability(FeatureCapability a_capability) const noexcept override;
		void Load() override;
		void OnD3D11Ready(IDXGIAdapter* adapter, ID3D11Device* device) override;
		void DrawSettings() override;
		bool ProducesTelemetry() const override { return true; }
		void CollectTelemetry(cs::telemetry::Sink& a_sink) const override;
		bool HooksInstalled() const noexcept { return _hooksInstalled.load(std::memory_order_acquire); }

		// Resolve a bound pixel-shader pointer to its tracked DXBC SHA1 hex (empty if untracked or the tracker is disabled). Dev-only diagnostic; read-thread-safe.
		std::string GetShaForPixelShader(ID3D11PixelShader* a_shader) const noexcept;

		// Optional callback the CreatePixelShader hook offers each shader after tracking; return true and swap *a_out (AddRef replacement, Release incoming) to substitute.
		using PixelShaderSwapCallback = bool (*)(const void* a_bytecode, std::size_t a_bytecode_len,
			const catalog::Sha1Result& a_sha, ID3D11PixelShader** a_out);
		void RegisterPixelShaderSwapCallback(PixelShaderSwapCallback a_cb) noexcept;

		// Optional observer invoked for every PSSetShader bind (render thread, before subclass attribution) with the bound shader. Dev-only diagnostic; must be null-safe and non-throwing.
		using PixelShaderBindObserver = void (*)(ID3D11PixelShader* a_bound);
		void RegisterPixelShaderBindObserver(PixelShaderBindObserver a_cb) noexcept;

		struct Settings
		{
			bool        enabled = false;
			int         writerFlushIntervalMs = 5000;
			std::string catalogPath = "Data\\F4SE\\Plugins\\FO4CommunityShaders\\shader-catalog.sqlite";
			// BSShader subclass technique attribution skips unverified layouts to prevent next-gen CTDs without affecting the swap broker.
			bool        subclassAttribution = true;
		};

	private:
		ShaderCatalog() = default;
		~ShaderCatalog() override;

		void SaveSettings();

		Settings _settings;
		std::atomic<bool> _started{ false };
		std::atomic<bool> _hooksInstalled{ false };
	};
}
