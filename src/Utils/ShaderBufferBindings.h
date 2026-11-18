#pragma once

#include "Render/SubstrateSlots.h"

#include <cstdint>
#include <cstring>
#include <d3d11shader.h>
#include <d3dcompiler.h>
#include <optional>
#include <string>
#include <string_view>

namespace cs::util
{
	namespace slot_detail
	{
		[[nodiscard]] inline std::optional<render::SlotKind> KindOf(D3D_SHADER_INPUT_TYPE a_type) noexcept
		{
			switch (a_type) {
			case D3D_SIT_CBUFFER:
				return render::SlotKind::kConstantBuffer;
			case D3D_SIT_SAMPLER:
				return render::SlotKind::kSampler;
			case D3D_SIT_TEXTURE:
			case D3D_SIT_TBUFFER:
			case D3D_SIT_STRUCTURED:
			case D3D_SIT_BYTEADDRESS:
				return render::SlotKind::kTexture;
			default:
				return std::nullopt;
			}
		}

		[[nodiscard]] inline std::uint8_t StageBit(D3D11_SHADER_VERSION_TYPE a_type) noexcept
		{
			switch (a_type) {
			case D3D11_SHVER_VERTEX_SHADER:
				return render::kSlotVertex;
			case D3D11_SHVER_PIXEL_SHADER:
				return render::kSlotPixel;
			case D3D11_SHVER_COMPUTE_SHADER:
				return render::kSlotCompute;
			default:
				return 0;
			}
		}

		[[nodiscard]] inline bool NameMatches(std::string_view a_reflected, std::string_view a_claim) noexcept
		{
			return a_reflected == a_claim ||
			       (a_reflected.size() > a_claim.size() + 2 && a_reflected.ends_with(a_claim) &&
					   a_reflected[a_reflected.size() - a_claim.size() - 1] == ':');
		}

		[[nodiscard]] inline const char* KindName(render::SlotKind a_kind) noexcept
		{
			switch (a_kind) {
			case render::SlotKind::kConstantBuffer:
				return "Constant-buffer";
			case render::SlotKind::kSampler:
				return "Sampler";
			default:
				return "Texture";
			}
		}

		[[nodiscard]] inline char RegisterLetter(render::SlotKind a_kind) noexcept
		{
			return a_kind == render::SlotKind::kConstantBuffer ? 'b' : a_kind == render::SlotKind::kSampler ? 's' :
			                                                                                                  't';
		}
	}

	// Every reflected binding on a claimed exclusive slot must carry a name the table claims there.
	[[nodiscard]] inline std::string ValidateSubstrateSlots(ID3D11ShaderReflection* reflection)
	{
		D3D11_SHADER_DESC desc{};
		if (FAILED(reflection->GetDesc(&desc)))
			return "Substrate slot validation could not read shader";
		const auto stage = slot_detail::StageBit(static_cast<D3D11_SHADER_VERSION_TYPE>(D3D11_SHVER_GET_TYPE(desc.Version)));
		for (UINT index = 0; index < desc.BoundResources; ++index) {
			D3D11_SHADER_INPUT_BIND_DESC binding{};
			if (FAILED(reflection->GetResourceBindingDesc(index, &binding)))
				return "Substrate slot validation could not read binding";
			const auto kind = slot_detail::KindOf(binding.Type);
			if (!kind)
				continue;
			const auto name = std::string_view(binding.Name);
			bool exclusive = false;
			bool claimed = false;
			for (const auto* block : render::kSlotTable) {
				if (block->kind != *kind || (block->stages & stage) == 0 ||
					binding.BindPoint < block->first || binding.BindPoint > block->last())
					continue;
				const auto claim = block->names[binding.BindPoint - block->first];
				if (claim.empty())
					continue;
				exclusive |= block->exclusive;
				claimed |= slot_detail::NameMatches(name, claim);
			}
			if (exclusive && !claimed)
				return std::string(slot_detail::KindName(*kind)) + " slot collision at " + slot_detail::RegisterLetter(*kind) +
				       std::to_string(binding.BindPoint) + ": " + std::string(name);
		}
		return {};
	}
}
