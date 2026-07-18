#pragma once

#include "Feature.h"
#include "Utils/CSBuffer.h"

#include <atomic>
#include <cstdint>
#include <memory>

#include <winrt/base.h>

namespace cs::features
{
	class ScreenSpaceGI : public Feature
	{
	public:
		static ScreenSpaceGI* GetSingleton();

		std::string_view GetName() const override { return "ScreenSpaceGI"; }
		std::string GetCategory() const override { return "Lighting"; }
		std::string GetFeatureSummary() const override { return "Screen-space ambient occlusion and indirect diffuse lighting."; }
		std::vector<FeatureRequirement> GetRequirements() const override { return {}; }
		EnbPolicy GetEnbPolicy() const override { return EnbPolicy::kDeactivate; }

		bool Configure(const toml::table& a_config, std::string& a_error) override;
		void Load() override;
		void OnD3D11Ready(IDXGIAdapter* a_adapter, ID3D11Device* a_device) override;
		void DrawSettings() override;
		void RestoreDefaultSettings() override;
		bool HasResettableSettings() const override { return true; }
		bool IsReady();

		struct Settings
		{
			bool enabled = false;
		};

	private:
		struct alignas(16) ResolveCB
		{
			std::uint32_t Extent[2];
			std::uint32_t Origin[2];
			std::uint32_t FrameIndex;
			std::uint32_t Padding[3];
		};
		static_assert(sizeof(ResolveCB) % 16 == 0);

		ScreenSpaceGI() = default;

		void SaveSettings();
		void OnComputeResolve();
		bool EnsureResources();

		static constexpr std::uint32_t kBouncePSSlot = 0;
		static constexpr std::uint32_t kAOPSSlot = 13;

		Settings _settings;
		std::atomic_bool _started{ false };
		std::atomic_bool _resourcesReady{ false };
		std::atomic_bool _resourceInitFailed{ false };

		std::unique_ptr<cs::buffer::Texture2D> _bounceTexture;
		std::unique_ptr<cs::buffer::Texture2D> _aoTexture;
		std::unique_ptr<cs::buffer::ConstantBuffer> _resolveCB;
		winrt::com_ptr<ID3D11ComputeShader> _resolveCS;
		std::uint32_t _allocW = 0;
		std::uint32_t _allocH = 0;
		std::uint32_t _generation = 0;
	};
}
