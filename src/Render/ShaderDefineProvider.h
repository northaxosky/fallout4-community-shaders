#pragma once

#include "Render/ShaderInjectionTargets.h"

#include <algorithm>
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
			return debug.empty() || a_target != ShaderInjectionTarget::kBsdfComposite ? ShaderDefineOptions{} : ShaderDefineOptions{ { debug, "1" } };
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
