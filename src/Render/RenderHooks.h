#pragma once

#include <cstdint>
#include <functional>
#include <span>

struct ID3D11VertexShader;

namespace cs
{
	class Feature;
}

namespace RE
{
	class BSBatchRenderer;
	class BSGeometry;
	class BSRenderPass;
}

namespace cs::engine
{
	bool EnsureDrawProfilingInstalled();
	using RenderHookCallback = std::function<void()>;

	// Late priority keeps additive lights after darkening.
	enum class HookPriority : int
	{
		Early = -100,
		Default = 0,
		Late = 100,
		// Brackets that enclose, or sit inside, every feature callback.
		OutermostPre = -1000,
		InnermostPost = -1000,
		InnermostPre = 1000,
		OutermostPost = 1000,
	};

	// True on the startup thread before the first render hook fires.
	[[nodiscard]] bool RenderHookRegistrationAllowed(const char* a_where);

	// Register only during Load or OnPostPostLoad.
#define CS_DECLARE_RENDER_HOOK(name, ...)                                                              \
	bool name(__VA_ARGS__ RenderHookCallback callback, HookPriority priority = HookPriority::Default); \
	bool name(Feature& a_owner, __VA_ARGS__ RenderHookCallback callback, HookPriority priority = HookPriority::Default)

	CS_DECLARE_RENDER_HOOK(RegisterPostDeferredPrePass);
	CS_DECLARE_RENDER_HOOK(RegisterPreDeferredPrePass);
	CS_DECLARE_RENDER_HOOK(RegisterPreDeferredLightsImpl);
	CS_DECLARE_RENDER_HOOK(RegisterPostDeferredLightsImpl);
	CS_DECLARE_RENDER_HOOK(RegisterPreDeferredComposite);
	CS_DECLARE_RENDER_HOOK(RegisterPostDeferredComposite);
	// After SSLR, before the second composite; only while native SSS is on.
	CS_DECLARE_RENDER_HOOK(RegisterPreSecondDeferredComposite);
	// Right after the main sun's cascades render, before focus shadows reuse them.
	CS_DECLARE_RENDER_HOOK(RegisterPostSunShadowRender);
	// Before the stock precipitation occlusion pass.
	CS_DECLARE_RENDER_HOOK(RegisterPrePrecipitationOcclusion);
	// After the stock precipitation occlusion pass; it runs in clear weather too.
	CS_DECLARE_RENDER_HOOK(RegisterPostPrecipitationOcclusion);

	// Start of Render_PreUI, before the world camera or frame targets are set.
	CS_DECLARE_RENDER_HOOK(RegisterPreWaterUpdate);

	// After the update-water callback returns.
	CS_DECLARE_RENDER_HOOK(RegisterPostWaterUpdate);

	// Around each ProcessCommandBuffer replay (one technique bucket).
	CS_DECLARE_RENDER_HOOK(RegisterPreCommandBufferReplay);
	CS_DECLARE_RENDER_HOOK(RegisterPostCommandBufferReplay);

	enum class ShadowLightKind : std::uint8_t
	{
		kDirectional,
		kFrustum,   // spot
		kParabolic  // omnidirectional
	};
	// Bracket every Render of that light kind, whatever its mask.
	CS_DECLARE_RENDER_HOOK(RegisterPreShadowLightRender, ShadowLightKind a_kind, );
	CS_DECLARE_RENDER_HOOK(RegisterPostShadowLightRender, ShadowLightKind a_kind, );

	// After the ImageSpace effects and resolve, before the UI.
	CS_DECLARE_RENDER_HOOK(RegisterPostImagespace);

	// After all sky and cloud draws in the main color target, before water and alpha.
	CS_DECLARE_RENDER_HOOK(RegisterPostForwardSky);

	// After every engine ResetState, including loading-screen and UI callers.
	CS_DECLARE_RENDER_HOOK(RegisterPostResetState);

#undef CS_DECLARE_RENDER_HOOK

	// One world batch-4 bucket: every pass in it shares the technique.
	struct HeldPrepassBucket
	{
		RE::BSBatchRenderer* renderer;
		RE::BSRenderPass* head;
		std::uint32_t technique;
		bool alpha;
	};
	using PrepassBucketPredicate = bool (*)(std::uint32_t a_technique) noexcept;
	using HeldPrepassBucketsCallback = std::function<void(std::span<const HeldPrepassBucket>)>;

	// Holds matching world batch-4 buckets; replays them after opaque decals.
	bool RegisterWorldPrepassHold(
		PrepassBucketPredicate a_predicate,
		HeldPrepassBucketsCallback a_replay,
		HookPriority a_priority = HookPriority::Default);
	bool RegisterWorldPrepassHold(
		Feature& a_owner,
		PrepassBucketPredicate a_predicate,
		HeldPrepassBucketsCallback a_replay,
		HookPriority a_priority = HookPriority::Default);
	// Draws a held bucket via the engine; valid only inside the replay callback.
	void ReplayHeldPrepassBucket(const HeldPrepassBucket& a_bucket);

	// Vertex shader for one opaque world Z-prepass draw; null keeps the engine's.
	using ZPrepassVertexShaderSelector = ID3D11VertexShader* (*)(const RE::BSGeometry& a_geometry) noexcept;
	// The first non-null selection binds for that draw only.
	bool RegisterWorldZPrepassVertexShader(
		ZPrepassVertexShaderSelector a_selector,
		HookPriority a_priority = HookPriority::Default);

	// Install the post-dirty-state deferred draw anchor.
	bool EnsureDeferredDrawAnchorInstalled();
}
