#pragma once

#include "Render/ShaderInjectionTargets.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace cs::engine
{
	using ShaderDefineOptions = std::vector<std::pair<std::string_view, std::string_view>>;

	class ShaderDefineProvider
	{
	public:
		virtual ~ShaderDefineProvider() = default;
		virtual std::string_view GetShaderDefineName() const { return {}; }
		virtual ShaderDefineOptions GetShaderDefineOptions(ShaderInjectionTarget = ShaderInjectionTarget::kCount) const { return {}; }
		virtual bool HasShaderDefine(ShaderInjectionTarget) const { return false; }
		virtual bool IsLoaded() const { return true; }
		virtual bool RequiresShaderGraphicsPair(ShaderInjectionTarget) const { return false; }

		bool IsFullscreenDebugSelected() const noexcept { return _fullscreenDebugSelected.load(std::memory_order_acquire); }

		// Return only targets whose compile options changed; mode changes need no recompile.
		std::vector<ShaderInjectionTarget> SetFullscreenDebugSelected(bool a_selected)
		{
			if (IsFullscreenDebugSelected() == a_selected)
				return {};
			std::array<ShaderDefineOptions, kShaderInjectionTargets.size()> previous;
			for (const auto& target : kShaderInjectionTargets)
				previous[static_cast<std::size_t>(target.id)] = GetShaderDefineOptions(target.id);
			_fullscreenDebugSelected.store(a_selected, std::memory_order_release);
			std::vector<ShaderInjectionTarget> changed;
			for (const auto& target : kShaderInjectionTargets) {
				if (HasShaderDefine(target.id) && previous[static_cast<std::size_t>(target.id)] != GetShaderDefineOptions(target.id))
					changed.push_back(target.id);
			}
			return changed;
		}

	private:
		std::atomic_bool _fullscreenDebugSelected{ false };
	};

	// Lets a feature add a second contributor that follows its own load state.
	class OwnedShaderDefineProvider final : public ShaderDefineProvider
	{
	public:
		OwnedShaderDefineProvider(const ShaderDefineProvider& a_declaration, const ShaderDefineProvider& a_owner) noexcept :
			_declaration(a_declaration), _owner(a_owner)
		{}

		std::string_view GetShaderDefineName() const override { return _declaration.GetShaderDefineName(); }
		ShaderDefineOptions GetShaderDefineOptions(ShaderInjectionTarget a_target = ShaderInjectionTarget::kCount) const override { return _declaration.GetShaderDefineOptions(a_target); }
		bool HasShaderDefine(ShaderInjectionTarget a_target) const override { return _declaration.HasShaderDefine(a_target); }
		bool IsLoaded() const override { return _owner.IsLoaded(); }
		bool RequiresShaderGraphicsPair(ShaderInjectionTarget a_target) const override { return _declaration.RequiresShaderGraphicsPair(a_target); }

	private:
		const ShaderDefineProvider& _declaration;
		const ShaderDefineProvider& _owner;
	};

	struct ShaderDefineDeclaration final : ShaderDefineProvider
	{
		ShaderDefineDeclaration(std::string_view a_name,
			std::initializer_list<ShaderInjectionTarget> a_targets,
			std::string_view a_debug = {},
			ShaderInjectionTarget a_pair = ShaderInjectionTarget::kCount) :
			name(a_name), targets(a_targets), debug(a_debug), pair(a_pair)
		{}

		std::string_view GetShaderDefineName() const override { return name; }
		ShaderDefineOptions GetShaderDefineOptions(ShaderInjectionTarget a_target = ShaderInjectionTarget::kCount) const override
		{
			return GetShaderDefineOptions(a_target, IsFullscreenDebugSelected());
		}
		ShaderDefineOptions GetShaderDefineOptions(ShaderInjectionTarget a_target, bool a_fullscreenDebugSelected) const
		{
			// kCount queries feature-wide options, not an injection target.
			return a_fullscreenDebugSelected && !debug.empty() &&
			               HasShaderDefine(ShaderInjectionTarget::kBsdfComposite) &&
			               (a_target == ShaderInjectionTarget::kCount || a_target == ShaderInjectionTarget::kBsdfComposite) ?
			           ShaderDefineOptions{ { debug, "1" } } :
			           ShaderDefineOptions{};
		}
		bool HasShaderDefine(ShaderInjectionTarget a_target) const override
		{
			return std::ranges::find(targets, a_target) != targets.end();
		}
		bool RequiresShaderGraphicsPair(ShaderInjectionTarget a_target) const override { return pair == a_target; }

		std::string_view name;
		std::vector<ShaderInjectionTarget> targets;
		std::string_view debug;
		ShaderInjectionTarget pair;
	};
}
