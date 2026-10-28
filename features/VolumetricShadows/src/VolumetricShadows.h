#pragma once

#include "Feature.h"
#include "FeatureCategories.h"
#include "Render/RgbaPreview.h"
#include "Render/ShadowCascades.h"
#include "Render/SunShadowLights.h"
#include "ShaderDefines.h"

#include <d3d11.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include <winrt/base.h>

namespace cs::features
{
	class VolumetricShadows : public ShaderFeature<volumetric_shadows_shader::kShaderDefines>
	{
	public:
		static constexpr std::uint32_t kMapSize = 512;

		static VolumetricShadows* GetSingleton();

		std::string_view GetName() const override { return "VolumetricShadows"; }
		std::string_view GetDisplayName() const override { return "Volumetric Shadows"; }
		std::string GetCategory() const override { return FeatureCategories::kLighting; }
		std::string GetFeatureSummary() const override
		{
			return "Downsampled, blurred variance shadow maps of the sun cascades. They soften the sun's soft, rim and back lighting and shadow the sun on water and effects.";
		}

		void Load() override;
		void OnD3D11Ready(IDXGIAdapter* a_adapter, ID3D11Device* a_device) override;
		bool ValidateShaderInjections(std::string& a_error) override;
		void DrawSettings() override;
		void WriteSharedData(render::SharedFeatureDataCB&, render::FO4SharedDataCB& a_fo4) override;

		bool ProducesTelemetry() const override { return true; }
		void CollectTelemetry(cs::telemetry::Sink& a_sink) const override;
		std::span<const FeatureDebugView> GetDebugViews() const noexcept override;
		void SetDebugView(std::string_view a_view) noexcept override;

	private:
		VolumetricShadows() = default;

		// One two-mip texture: mip 0 is cascade 1, mip 1 is cascade 0.
		struct MipChain
		{
			winrt::com_ptr<ID3D11Texture2D> texture;
			winrt::com_ptr<ID3D11ShaderResourceView> srv;
			std::array<winrt::com_ptr<ID3D11ShaderResourceView>, 2> mipSRV;
			std::array<winrt::com_ptr<ID3D11UnorderedAccessView>, 2> mipUAV;
		};
		static MipChain CreateMipChain(ID3D11Device* a_device, std::string_view a_name);

		enum class Skip : std::uint8_t
		{
			kNone,
			kUnavailable,
			kDisabled,
			kSunCascades,
			kSource,
			kShaders
		};
		// After the sun's Render(7), while the borrowed cascades are valid.
		void Produce();
		Skip DownsampleAndBlur(ID3D11DeviceContext* a_context);
		bool EnsureSourceView(ID3D11Device* a_device, const cs::engine::SunCascadeSnapshot& a_snapshot);
		void ClearToLit(ID3D11DeviceContext* a_context);
		void Dispatch(ID3D11DeviceContext* a_context, std::string_view a_pass, ID3D11ComputeShader* a_shader, ID3D11ShaderResourceView* a_input, ID3D11UnorderedAccessView* a_output, std::uint32_t a_groupsX, std::uint32_t a_groupsY);
		void Blur(ID3D11DeviceContext* a_context, std::size_t a_mip);

		void BindConsumer(ID3D11DeviceContext* a_context, cs::engine::ShaderInjectionTarget a_target);

		enum class DebugCascade : std::uint8_t
		{
			kNone,
			kCascade0,
			kCascade1
		};
		FeatureDebugTexture GetCascadeDebugTexture(DebugCascade a_cascade) const;
		// Render thread; converts the selected cascade's moments for display.
		void RenderDebugPreview(ID3D11DeviceContext* a_context);

		// Indexed by map mip.
		std::array<cs::render::RgbaPreview, 2> _preview;
		winrt::com_ptr<ID3D11ComputeShader> _previewCS;
		MipChain _map;
		MipChain _blurTemp;
		winrt::com_ptr<ID3D11ComputeShader> _downsampleMip0;
		winrt::com_ptr<ID3D11ComputeShader> _downsampleMip1;
		winrt::com_ptr<ID3D11ComputeShader> _blurHorizontal;
		winrt::com_ptr<ID3D11ComputeShader> _blurVertical;

		// Render thread only; the view is cached for the borrowed cascade texture.
		winrt::com_ptr<ID3D11ShaderResourceView> _sourceSRV;
		ID3D11Texture2D* _sourceTexture = nullptr;
		D3D11_TEXTURE2D_DESC _sourceDesc{};
		// Render thread only; lit maps sample nothing stale.
		bool _mapsLit = true;
		cs::engine::SunCascadeStatus _sunStatus = cs::engine::SunCascadeStatus::kOk;
		std::string_view _loggedSkip = "none";

		std::atomic_bool _registrationsReady{ false };
		std::atomic_bool _injectionsOperational{ false };
		std::atomic<DebugCascade> _debugCascade{ DebugCascade::kNone };

		struct Counters
		{
			std::atomic<std::uint64_t> productions{ 0 };
			std::array<std::atomic<std::uint64_t>, static_cast<std::size_t>(Skip::kShaders) + 1> skipped{};
			std::array<std::atomic<std::uint64_t>, cs::engine::kSunCascadeStatusCount> sunStatusSkipped{};
			std::atomic<std::uint32_t> cascadeCount{ 0 };
			std::atomic<std::uint32_t> sourceWidth{ 0 };
			std::atomic<std::uint32_t> sourceHeight{ 0 };
			std::atomic<std::uint64_t> lightDraws{ 0 };
			std::atomic<std::uint64_t> waterDraws{ 0 };
			std::atomic<std::uint64_t> effectDraws{ 0 };
		} _counters;
	};
}
