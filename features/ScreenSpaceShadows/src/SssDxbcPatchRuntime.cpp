#include "SssDxbcPatchRuntime.h"

#include <algorithm>
#include <limits>
#include <vector>

namespace cs::features::sss_dxbc_patch
{
	PatchedPixelShaderIdentity MakeIdentity(
		const engine::dxbc_patch::Plan& a_plan,
		std::size_t a_planIndex) noexcept
	{
		PatchedPixelShaderIdentity identity;
		identity.target = static_cast<std::uint16_t>(a_plan.target);
		if (a_planIndex <= std::numeric_limits<std::uint32_t>::max())
			identity.planIndex = static_cast<std::uint32_t>(a_planIndex);
		std::ranges::copy_n(
			a_plan.expectedSha256.bytes.begin(),
			identity.patchedSha256Prefix.size(),
			identity.patchedSha256Prefix.begin());
		return identity;
	}

	PublishStatus PublishPatchedPixelShader(
		const engine::PixelShaderSwapRequest& a_request,
		const engine::dxbc_patch::Plan& a_plan,
		std::size_t a_planIndex,
		std::span<const std::byte> a_bytecode,
		CreatePatchedPixelShaderFunction a_create) noexcept
	{
		if (!a_request.device
			|| !a_request.output
			|| !*a_request.output
			|| *a_request.output != a_request.stockOutput
			|| a_bytecode.empty()
			|| !a_request.bytecode
			|| a_request.bytecodeLength == 0
			|| a_request.bytecodeLength
				> std::numeric_limits<UINT>::max()
			|| !a_create
			|| a_planIndex > std::numeric_limits<std::uint32_t>::max()) {
			return PublishStatus::kInvalidRequest;
		}

		ID3D11PixelShader* patched = nullptr;
		HRESULT result = E_FAIL;
		{
			engine::ScopedPixelShaderBrokerBypass bypass;
			result = a_create(
				a_request.device,
				a_bytecode.data(),
				a_bytecode.size(),
				a_request.linkage,
				&patched);
		}
		if (FAILED(result)
			|| !patched
			|| patched == a_request.stockOutput) {
			if (patched)
				patched->Release();
			return PublishStatus::kCreateRejected;
		}

		const auto identity = MakeIdentity(a_plan, a_planIndex);
		if (FAILED(patched->SetPrivateData(
				kPatchedPixelShaderStockBytecodeGuid,
				static_cast<UINT>(a_request.bytecodeLength),
				a_request.bytecode))
			|| FAILED(patched->SetPrivateData(
				kPatchedPixelShaderIdentityGuid,
				static_cast<UINT>(sizeof(identity)),
				&identity))) {
			patched->Release();
			return PublishStatus::kIdentityRejected;
		}

		a_request.stockOutput->Release();
		*a_request.output = patched;
		return PublishStatus::kAccepted;
	}

	std::optional<PatchedPixelShaderIdentity> ReadIdentity(
		ID3D11PixelShader* a_shader,
		std::span<const engine::dxbc_patch::Plan> a_plans) noexcept
	{
		if (!a_shader)
			return std::nullopt;
		PatchedPixelShaderIdentity identity{};
		UINT size = static_cast<UINT>(sizeof(identity));
		if (FAILED(a_shader->GetPrivateData(
				kPatchedPixelShaderIdentityGuid,
				&size,
				&identity))
			|| size != sizeof(identity)
			|| identity.magic != PatchedPixelShaderIdentity{}.magic
			|| identity.version != PatchedPixelShaderIdentity{}.version
			|| identity.planIndex >= a_plans.size()) {
			return std::nullopt;
		}
		const auto& plan = a_plans[identity.planIndex];
		if (identity.target != static_cast<std::uint16_t>(plan.target)
			|| !std::equal(
				identity.patchedSha256Prefix.begin(),
				identity.patchedSha256Prefix.end(),
				plan.expectedSha256.bytes.begin())) {
			return std::nullopt;
		}
		return identity;
	}

	bool MatchesPatchedPixelShader(
		ID3D11PixelShader* a_shader,
		engine::dxbc_patch::Target a_target,
		std::span<const engine::dxbc_patch::Plan> a_plans) noexcept
	{
		const auto identity = ReadIdentity(a_shader, a_plans);
		return identity
			&& identity->target == static_cast<std::uint16_t>(a_target);
	}

	RestoreStockStatus RestoreStockPixelShader(
		ID3D11PixelShader* a_patchedShader,
		ID3D11Device* a_device,
		std::span<const engine::dxbc_patch::Plan> a_plans,
		CreatePatchedPixelShaderFunction a_create,
		void* a_bindContext,
		BindPixelShaderFunction a_bind) noexcept
	{
		const auto failClosed =
			[a_bindContext, a_bind](
				RestoreStockStatus a_status) noexcept {
				if (a_bindContext && a_bind)
					a_bind(a_bindContext, nullptr);
				return a_status;
			};
		if (!a_patchedShader
			|| !a_device
			|| !a_create
			|| !a_bindContext
			|| !a_bind) {
			return failClosed(RestoreStockStatus::kInvalidRequest);
		}
		const auto identity = ReadIdentity(a_patchedShader, a_plans);
		if (!identity)
			return failClosed(RestoreStockStatus::kInvalidIdentity);
		const auto& plan = a_plans[identity->planIndex];
		if (plan.stock.length == 0
			|| plan.stock.length > std::numeric_limits<UINT>::max()) {
			return failClosed(RestoreStockStatus::kMissingBytecode);
		}

		try {
			std::vector<std::byte> stock(plan.stock.length);
			UINT size = static_cast<UINT>(stock.size());
			if (FAILED(a_patchedShader->GetPrivateData(
					kPatchedPixelShaderStockBytecodeGuid,
					&size,
					stock.data()))
				|| size != static_cast<UINT>(stock.size())) {
				return failClosed(
					RestoreStockStatus::kMissingBytecode);
			}
			sha1::Sha1InitOnce();
			const auto sha1 = sha1::Sha1Compute(
				stock.data(),
				stock.size());
			const auto sha256 = sha256::Sha256Compute(
				stock.data(),
				stock.size());
			if (!engine::dxbc_patch::StockMatches(
					plan.stock,
					stock.size(),
					sha1,
					sha256)) {
				return failClosed(RestoreStockStatus::kHashMismatch);
			}

			ID3D11PixelShader* restored = nullptr;
			HRESULT result = E_FAIL;
			{
				engine::ScopedPixelShaderBrokerBypass bypass;
				result = a_create(
					a_device,
					stock.data(),
					stock.size(),
					nullptr,
					&restored);
			}
			if (FAILED(result)
				|| !restored
				|| restored == a_patchedShader) {
				if (restored)
					restored->Release();
				return failClosed(
					RestoreStockStatus::kCreateRejected);
			}
			a_bind(a_bindContext, restored);
			restored->Release();
			return RestoreStockStatus::kRestored;
		} catch (...) {
			return failClosed(RestoreStockStatus::kMissingBytecode);
		}
	}
}
