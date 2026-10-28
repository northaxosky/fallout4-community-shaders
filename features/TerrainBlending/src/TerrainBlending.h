#pragma once

#include "Feature.h"
#include "FeatureCategories.h"
#include "ShaderDefines.h"

#include <atomic>
#include <span>

struct ID3D11DeviceContext;
struct ID3D11VertexShader;

namespace RE
{
	class BSGeometry;
}

namespace cs::engine
{
	struct HeldPrepassBucket;
}

namespace cs::features
{
	class TerrainBlending : public ShaderFeature<terrain_blending_shader::kShaderDefines>
	{
	public:
		static TerrainBlending* GetSingleton();

		std::string_view GetName() const override { return "TerrainBlending"; }
		std::string_view GetDisplayName() const override { return "Terrain Blending"; }
		std::string GetCategory() const override { return FeatureCategories::kLandscapeAndTextures; }
		std::string GetFeatureSummary() const override
		{
			return "Blends terrain into objects that sink into it by depth.";
		}

		void Load() override;
		void OnD3D11Ready(IDXGIAdapter*, ID3D11Device* a_device) override;
		bool ValidateShaderInjections(std::string& a_error) override;
		bool ProducesTelemetry() const override { return true; }
		void CollectTelemetry(cs::telemetry::Sink& a_sink) const override;

		void WriteSharedData(render::SharedFeatureDataCB& a_feature, render::FO4SharedDataCB&) override;

		// Render thread: offset near terrain depth, hold landscape, replay blended.
		bool HoldsTechnique(std::uint32_t a_technique) const noexcept;
		void Replay(std::span<const engine::HeldPrepassBucket> a_buckets);
		void BindReplayDraw(ID3D11DeviceContext* a_context) noexcept;
		ID3D11VertexShader* SelectZPrepassShader(const RE::BSGeometry& a_geometry) noexcept;

	private:
		TerrainBlending() = default;
		bool IsOperational() const noexcept;
		bool CopyObjectDepth(ID3D11DeviceContext* a_context);
		void DrawHeld(std::span<const engine::HeldPrepassBucket> a_buckets, std::uint32_t a_pass);

		std::atomic_bool _registrationsReady{ false };
		std::atomic_bool _resourcesReady{ false };
		std::atomic_bool _injectionsOperational{ false };

		std::atomic_uint32_t _frameBuckets{ 0 };
		std::atomic_uint32_t _framePasses{ 0 };
		std::atomic_uint64_t _bucketsHeld{ 0 };
		std::atomic_uint64_t _passesHeld{ 0 };
		std::atomic_uint64_t _replays{ 0 };
		std::atomic_uint64_t _unblendedReplays{ 0 };
		std::atomic_uint64_t _colorDraws{ 0 };
		std::atomic_uint64_t _dataDraws{ 0 };
		std::atomic_uint64_t _depthCopies{ 0 };
		std::atomic_uint64_t _zPassesOffset{ 0 };
	};
}
