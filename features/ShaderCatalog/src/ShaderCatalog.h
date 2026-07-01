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

		void Load() override;
		void OnD3D11Ready(IDXGIAdapter* adapter, ID3D11Device* device) override;
		void DrawSettings() override;
		bool HooksInstalled() const noexcept { return _hooksInstalled.load(std::memory_order_acquire); }

		// Broker: one optional callback the single CreatePixelShader hook offers each created shader.
		// It runs after the shader is recorded/tracked; return true and swap *a_out (AddRef the
		// replacement, Release the incoming shader) to substitute. Register from OnD3D11Ready only.
		using PixelShaderSwapCallback = bool (*)(const void* a_bytecode, std::size_t a_bytecode_len,
			const catalog::Sha1Result& a_sha, ID3D11PixelShader** a_out);
		void RegisterPixelShaderSwapCallback(PixelShaderSwapCallback a_cb) noexcept;

		struct Settings
		{
			bool        enabled = false;
			int         writerFlushIntervalMs = 5000;
			std::string catalogPath = "Data\\F4SE\\Plugins\\FO4CommunityShaders\\shader-catalog.sqlite";
			int         symbolicationBudgetUs = 200;
		};

	private:
		ShaderCatalog() = default;
		~ShaderCatalog() override;

		void LoadSettings();
		void SaveSettings();

		Settings _settings;
		std::atomic<bool> _started{ false };
		std::atomic<bool> _hooksInstalled{ false };
	};
}
