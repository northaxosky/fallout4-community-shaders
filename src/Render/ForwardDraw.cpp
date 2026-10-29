#include "Render/ForwardDraw.h"

#include "Feature.h"
#include "PCH.h"
#include "Render/Engine.h"
#include "Render/RuntimeCallback.h"
#include "Render/ShaderGeometryHooks.h"
#include "Render/ShaderInjection.h"
#include "Render/SubstrateSlots.h"
#include "Utils/CSBuffer.h"

#include "RE/B/BSRenderPass.h"
#include "RE/B/BSShaderAccumulator.h"
#include "RE/B/BSShaderManager.h"

#include <vector>

namespace cs::render::forward_draw
{
	namespace
	{
		struct Subscription
		{
			Feature* owner;
			std::string_view label;
			Observer observer;
		};

		std::vector<Subscription> g_subscriptions;
		thread_local Draw t_draw;
		thread_local bool t_active = false;

		buffer::CachedConstantBuffer<ForwardDrawCB> g_buffer;

		// MainRenderSetup may store 13 on the world accumulators.
		[[nodiscard]] bool IsWorldAccumulator(const RE::BSShaderAccumulator* a_accumulator) noexcept
		{
			using Mode = RE::BSShaderManager::etRenderMode;
			if (!a_accumulator)
				return false;
			const auto mode = a_accumulator->renderMode;
			if (mode != Mode::kDeferredGBuffer && mode != Mode::kDepthPrepass)
				return false;
			const auto* scene = a_accumulator->activeShadowSceneNode;
			return scene && scene == engine::GetWorldShadowSceneNode();
		}

		void SetupGeometry(RE::BSShader*, RE::BSRenderPass* a_pass) noexcept
		{
			auto* accumulator = RE::BSShaderManager::GetCurrentAccumulator();
			t_draw = { a_pass, IsWorldAccumulator(accumulator) };
			t_active = true;
			for (const auto& subscription : g_subscriptions)
				RunRenderCallback(subscription.owner, subscription.label, [&] { subscription.observer(t_draw); });
		}

		void RestoreGeometry(RE::BSShader*, RE::BSRenderPass*) noexcept
		{
			t_active = false;
			t_draw = {};
		}
	}

	void Subscribe(Feature* a_owner, std::string_view a_label, Observer a_observer)
	{
		g_subscriptions.push_back({ a_owner, a_label, a_observer });
	}

	void Install()
	{
		for (const auto target : kTargets)
			engine::ObserveShaderGeometry(target, { &SetupGeometry, &RestoreGeometry });
	}

	void Initialize(ID3D11Device* a_device)
	{
		if (!a_device)
			return;
		g_buffer.Create("Render/ForwardDraw.Descriptor");
	}

	void BindDescriptor(ID3D11DeviceContext* a_context) noexcept
	{
		ID3D11Buffer* bound = nullptr;
		RunRenderCallback(nullptr, "forward draw descriptor", [&] {
			ForwardDrawCB next;
			if (t_active && t_draw.inWorld)
				next.ExtraShaderDescriptor |= kInWorld;
			bound = g_buffer.Update(next);
		});
		engine::BindInjectionConstantBuffers(a_context, kForwardDrawSlot, 1, &bound);
	}
}
