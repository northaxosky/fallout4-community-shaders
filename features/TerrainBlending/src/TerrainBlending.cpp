#include "TerrainBlending.h"
#include "Render/SharedDataLayout.h"

#include "Log.h"
#include "Render/Annotation.h"
#include "Render/Engine.h"
#include "Render/FeatureShaderBindings.h"
#include "Render/FrameBuffer.h"
#include "Render/FrameProfiler.h"
#include "Render/PrepassTechnique.h"
#include "Render/RenderHooks.h"
#include "Render/RendererContext.h"
#include "Render/ShaderInjection.h"
#include "Render/SharedData.h"
#include "Telemetry/Telemetry.h"
#include "Utils/CSUtil.h"

#include "RE/B/BSGeometry.h"
#include "RE/B/BSRenderPass.h"
#include "RE/B/BSShaderProperty.h"
#include <REX/TScopeExit.h>

#include <array>
#include <d3d11.h>
#include <winrt/base.h>

namespace cs::features
{
	namespace
	{
		auto* L = cs::log::Get("cs.feature.terrainblending");

		using ShaderFlag = RE::BSShaderProperty::EShaderPropertyFlag;

		using namespace cs::engine::prepass_technique;

		// Free in the landscape pixel stage; slots from 16 up are not engine-owned.
		constexpr UINT kObjectDepthSlot = 55;
		constexpr UINT kReplayPassSlot = 8;
		// Upstream offsets terrain whose bound is within this many units of the eye.
		constexpr float kOffsetRange = 1024.0f;

		enum ReplayPass : std::uint32_t
		{
			kStock = 0,
			kColor = 1,
			kData = 2
		};

		struct ReplayResources
		{
			winrt::com_ptr<ID3D11Texture2D> depth;
			winrt::com_ptr<ID3D11ShaderResourceView> depthSRV;
			D3D11_TEXTURE2D_DESC depthDesc{};
			std::array<winrt::com_ptr<ID3D11BlendState>, 3> blend;
			std::array<winrt::com_ptr<ID3D11Buffer>, 3> passBuffer;
			winrt::com_ptr<ID3D11VertexShader> offsetDepthVS;
			ReplayPass replaying = kStock;
		};

		ReplayResources g_replay;

		void SetBlendTarget(
			D3D11_RENDER_TARGET_BLEND_DESC& a_target, bool a_blend, UINT8 a_mask)
		{
			a_target = {
				.BlendEnable = a_blend,
				.SrcBlend = D3D11_BLEND_SRC_ALPHA,
				.DestBlend = D3D11_BLEND_INV_SRC_ALPHA,
				.BlendOp = D3D11_BLEND_OP_ADD,
				.SrcBlendAlpha = D3D11_BLEND_ONE,
				.DestBlendAlpha = D3D11_BLEND_ZERO,
				.BlendOpAlpha = D3D11_BLEND_OP_ADD,
				.RenderTargetWriteMask = a_mask
			};
		}

		// Colour lerps by PS alpha; data channels belong to the data draw.
		D3D11_BLEND_DESC ColorBlendDesc()
		{
			D3D11_BLEND_DESC desc{};
			desc.IndependentBlendEnable = TRUE;
			SetBlendTarget(desc.RenderTarget[0], true, 0x7);
			SetBlendTarget(desc.RenderTarget[1], true, 0x3);
			SetBlendTarget(desc.RenderTarget[2], false, 0x0);
			SetBlendTarget(desc.RenderTarget[3], true, 0x7);
			SetBlendTarget(desc.RenderTarget[4], true, 0x7);
			SetBlendTarget(desc.RenderTarget[5], true, 0x3);
			// Parallax shadow is continuous shading, so it blends like colour.
			SetBlendTarget(desc.RenderTarget[6], true, 0x1);
			SetBlendTarget(desc.RenderTarget[7], false, 0x0);
			return desc;
		}

		// RT4 .w is vertex AO, which terrain owns outright where it wins.
		D3D11_BLEND_DESC DataBlendDesc()
		{
			D3D11_BLEND_DESC desc{};
			desc.IndependentBlendEnable = TRUE;
			constexpr std::array<UINT8, 8> masks{ 0x8, 0x0, 0xF, 0x8, 0x8, 0x0, 0x0, 0x0 };
			for (std::size_t slot = 0; slot < masks.size(); ++slot)
				SetBlendTarget(desc.RenderTarget[slot], false, masks[slot]);
			return desc;
		}

		void CreateReplayState(ID3D11Device* a_device, ReplayPass a_pass, const D3D11_BLEND_DESC& a_blend)
		{
			DX::ThrowIfFailed(a_device->CreateBlendState(&a_blend, g_replay.blend[a_pass].put()));
			const std::array<std::uint32_t, 4> constants{ a_pass, 0, 0, 0 };
			const D3D11_BUFFER_DESC bufferDesc{
				.ByteWidth = sizeof(constants),
				.Usage = D3D11_USAGE_IMMUTABLE,
				.BindFlags = D3D11_BIND_CONSTANT_BUFFER
			};
			const D3D11_SUBRESOURCE_DATA data{ .pSysMem = constants.data() };
			DX::ThrowIfFailed(a_device->CreateBuffer(&bufferDesc, &data, g_replay.passBuffer[a_pass].put()));
			cs::render::annotation::SetName(g_replay.blend[a_pass].get(), a_pass == kColor ? "TerrainBlending/ColorBlend" : "TerrainBlending/DataBlend");
			cs::render::annotation::SetName(g_replay.passBuffer[a_pass].get(), a_pass == kColor ? "TerrainBlending/ColorPass.CB" : "TerrainBlending/DataPass.CB");
		}

		bool Hold(std::uint32_t a_technique) noexcept
		{
			return TerrainBlending::GetSingleton()->HoldsTechnique(a_technique);
		}

		ID3D11VertexShader* OffsetDepth(const RE::BSGeometry& a_geometry) noexcept
		{
			return TerrainBlending::GetSingleton()->SelectZPrepassShader(a_geometry);
		}
	}

	TerrainBlending* TerrainBlending::GetSingleton()
	{
		static TerrainBlending instance;
		return &instance;
	}

	void TerrainBlending::Load()
	{
		if (!cs::engine::RegisterFeatureShaderBindings("TerrainBlending", *this, [this](cs::engine::ShaderReplacementRegistration& registration) {
				if (registration.targetId == cs::engine::ShaderInjectionTarget::kDeferredPrepass)
					registration.bind = [this](ID3D11DeviceContext* a_context) { BindReplayDraw(a_context); };
			})) {
			FailLoad("Terrain Blending shader contribution registration failed.");
			return;
		}
		if (!cs::engine::RegisterWorldPrepassHold(
				*this, &Hold, [this](std::span<const cs::engine::HeldPrepassBucket> a_buckets) { Replay(a_buckets); })) {
			FailLoad("Terrain Blending needs the world prepass bucket hold-back.");
			return;
		}
		if (!cs::engine::RegisterWorldZPrepassVertexShader(&OffsetDepth)) {
			FailLoad("Terrain Blending needs the world Z-prepass draw hook to offset terrain depth.");
			return;
		}
		_registrationsReady.store(true, std::memory_order_release);
	}

	void TerrainBlending::OnD3D11Ready(IDXGIAdapter*, ID3D11Device* a_device)
	{
		CreateReplayState(a_device, kColor, ColorBlendDesc());
		CreateReplayState(a_device, kData, DataBlendDesc());
		g_replay.offsetDepthVS.attach(static_cast<ID3D11VertexShader*>(cs::util::CompileShader(
			L"Data\\Shaders\\FO4\\ZPrePass.hlsl", { { "OFFSET_DEPTH", "" } }, "vs_5_0")));
		if (!g_replay.offsetDepthVS) {
			FailLoad("Terrain Blending could not compile the offset Z-prepass vertex shader.");
			return;
		}
		cs::render::annotation::SetName(g_replay.offsetDepthVS.get(), "TerrainBlending/OffsetDepth.VS");
		_resourcesReady.store(true, std::memory_order_release);
	}

	bool TerrainBlending::ValidateShaderInjections(std::string& a_error)
	{
		_injectionsOperational.store(false, std::memory_order_release);
		if (!_registrationsReady.load(std::memory_order_acquire)) {
			a_error = "hooks or shader contributions did not all register";
			return false;
		}
		if (!cs::render::IsSharedDataReady()) {
			a_error = "the shared substrate is unavailable, so b6 carries no Terrain Blending state";
			return false;
		}
		if (!cs::engine::ValidateShaderInjectionRoutes("TerrainBlending", a_error))
			return false;
		_injectionsOperational.store(true, std::memory_order_release);
		return true;
	}

	bool TerrainBlending::IsOperational() const noexcept
	{
		return IsEnabled() && IsHealthy() &&
		       _resourcesReady.load(std::memory_order_acquire) &&
		       _injectionsOperational.load(std::memory_order_acquire);
	}

	void TerrainBlending::WriteSharedData(render::SharedFeatureDataCB& a_feature, render::FO4SharedDataCB&)
	{
		a_feature.terrainBlendingSettings.Enabled = IsOperational() ? 1u : 0u;
	}

	bool TerrainBlending::HoldsTechnique(std::uint32_t a_technique) const noexcept
	{
		return (a_technique & kLandscape) != 0 && (a_technique & kLodLandscape) == 0 &&
		       IsOperational();
	}

	ID3D11VertexShader* TerrainBlending::SelectZPrepassShader(const RE::BSGeometry& a_geometry) noexcept
	{
		// Only shader properties register Z-prepass passes.
		const auto* property = static_cast<const RE::BSShaderProperty*>(a_geometry.properties[1].get());
		if (!property || !property->flags.all(ShaderFlag::kMultiTextureLandscape) ||
			property->flags.any(ShaderFlag::kLODLandscape) || !IsOperational())
			return nullptr;
		const auto camera = cs::engine::GetWorldCameraRecord();
		if (!camera)
			return nullptr;
		const RE::NiPoint3 eye{ camera->PosAdjust.x, camera->PosAdjust.y, camera->PosAdjust.z };
		const auto& bound = a_geometry.worldBound;
		if (bound.center.GetDistance(eye) - bound.fRadius > kOffsetRange)
			return nullptr;
		_zPassesOffset.fetch_add(1, std::memory_order_relaxed);
		return g_replay.offsetDepthVS.get();
	}

	bool TerrainBlending::CopyObjectDepth(ID3D11DeviceContext* a_context)
	{
		auto* source = cs::engine::GetDepthStencilTexture(RE::BSGraphics::DepthStencilTargetID::kMain);
		if (!source)
			return false;
		D3D11_TEXTURE2D_DESC desc{};
		source->GetDesc(&desc);
		if (desc.Format != DXGI_FORMAT_R24G8_TYPELESS || desc.SampleDesc.Count != 1)
			return false;
		if (!g_replay.depth || g_replay.depthDesc.Width != desc.Width || g_replay.depthDesc.Height != desc.Height) {
			auto* device = cs::engine::GetDevice();
			if (!device)
				return false;
			auto copyDesc = desc;
			copyDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			copyDesc.MiscFlags = 0;
			copyDesc.CPUAccessFlags = 0;
			copyDesc.Usage = D3D11_USAGE_DEFAULT;
			const D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc{
				.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS,
				.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
				.Texture2D = { .MostDetailedMip = 0, .MipLevels = 1 }
			};
			winrt::com_ptr<ID3D11Texture2D> texture;
			winrt::com_ptr<ID3D11ShaderResourceView> view;
			if (FAILED(device->CreateTexture2D(&copyDesc, nullptr, texture.put())) ||
				FAILED(device->CreateShaderResourceView(texture.get(), &viewDesc, view.put())))
				return false;
			cs::render::annotation::SetName(texture.get(), "TerrainBlending/ObjectDepth");
			cs::render::annotation::SetName(view.get(), "TerrainBlending/ObjectDepth.SRV");
			g_replay.depth = std::move(texture);
			g_replay.depthSRV = std::move(view);
			g_replay.depthDesc = copyDesc;
		}
		cs::render::annotation::ScopedEvent timing("TerrainBlending::DepthCopy");
		cs::engine::CopyResourcePreservingOM(a_context, g_replay.depth.get(), source);
		_depthCopies.fetch_add(1, std::memory_order_relaxed);
		return true;
	}

	void TerrainBlending::DrawHeld(std::span<const cs::engine::HeldPrepassBucket> a_buckets, std::uint32_t a_pass)
	{
		g_replay.replaying = static_cast<ReplayPass>(a_pass);
		const REX::TScopeExit restore{ []() noexcept { g_replay.replaying = kStock; } };
		for (const auto& bucket : a_buckets)
			cs::engine::ReplayHeldPrepassBucket(bucket);
	}

	void TerrainBlending::Replay(std::span<const cs::engine::HeldPrepassBucket> a_buckets)
	{
		std::uint32_t passes = 0;
		for (const auto& bucket : a_buckets)
			for (auto* pass = bucket.head; pass; pass = pass->batchNext)
				++passes;
		_frameBuckets.store(static_cast<std::uint32_t>(a_buckets.size()), std::memory_order_relaxed);
		_framePasses.store(passes, std::memory_order_relaxed);
		_bucketsHeld.fetch_add(a_buckets.size(), std::memory_order_relaxed);
		_passesHeld.fetch_add(passes, std::memory_order_relaxed);
		_replays.fetch_add(1, std::memory_order_relaxed);

		auto* context = cs::engine::GetImmediateContext();
		if (!context || !CopyObjectDepth(context)) {
			// Falls back to the stock opaque landscape draw.
			_unblendedReplays.fetch_add(1, std::memory_order_relaxed);
			DrawHeld(a_buckets, kStock);
			return;
		}
		cs::render::annotation::ScopedEvent timing("TerrainBlending::Replay");
		const REX::TScopeExit clear{ [context]() noexcept {
			ID3D11ShaderResourceView* noView = nullptr;
			ID3D11Buffer* noBuffer = nullptr;
			cs::engine::BindFrameShaderResources(context, cs::engine::ShaderStage::kPixel, kObjectDepthSlot, 1, &noView);
			cs::engine::BindFrameConstantBuffers(context, cs::engine::ShaderStage::kPixel, kReplayPassSlot, 1, &noBuffer);
		} };
		DrawHeld(a_buckets, kColor);
		DrawHeld(a_buckets, kData);
	}

	void TerrainBlending::BindReplayDraw(ID3D11DeviceContext* a_context) noexcept
	{
		const auto pass = g_replay.replaying;
		if (pass == kStock)
			return;
		auto* depth = g_replay.depthSRV.get();
		auto* constants = g_replay.passBuffer[pass].get();
		cs::engine::BindInjectionShaderResources(a_context, kObjectDepthSlot, 1, &depth);
		cs::engine::BindInjectionConstantBuffers(a_context, kReplayPassSlot, 1, &constants);
		constexpr float blendFactor[4]{};
		cs::engine::SetInjectionBlendState(a_context, g_replay.blend[pass].get(), blendFactor, 0xFFFFFFFFu);
		(pass == kColor ? _colorDraws : _dataDraws).fetch_add(1, std::memory_order_relaxed);
	}

	void TerrainBlending::CollectTelemetry(cs::telemetry::Sink& a_sink) const
	{
		a_sink
			.Field("operational", IsOperational())
			.Field("registrations_ready", _registrationsReady.load(std::memory_order_relaxed))
			.Field("frame_buckets_held", _frameBuckets.load(std::memory_order_relaxed))
			.Field("frame_passes_held", _framePasses.load(std::memory_order_relaxed))
			.Field("buckets_held", _bucketsHeld.load(std::memory_order_relaxed))
			.Field("passes_held", _passesHeld.load(std::memory_order_relaxed))
			.Field("replays", _replays.load(std::memory_order_relaxed))
			.Field("unblended_replays", _unblendedReplays.load(std::memory_order_relaxed))
			.Field("color_draws", _colorDraws.load(std::memory_order_relaxed))
			.Field("data_draws", _dataDraws.load(std::memory_order_relaxed))
			.Field("depth_copies", _depthCopies.load(std::memory_order_relaxed))
			.Field("z_passes_offset", _zPassesOffset.load(std::memory_order_relaxed));
		cs::render::profiling::CollectPassTimings(a_sink, "TerrainBlending::");
	}

	namespace
	{
		struct AutoRegister
		{
			AutoRegister() { FeatureManager::Get().Register(TerrainBlending::GetSingleton()); }
		};
		static AutoRegister _autoRegister;
	}
}
