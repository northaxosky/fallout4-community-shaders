#pragma once

#include "Render/DxbcPatchArtifact.h"
#include "Render/PixelShaderSwapBroker.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <type_traits>

namespace cs::features::sss_dxbc_patch
{
	inline constexpr GUID kPatchedPixelShaderIdentityGuid{
		0x5ce6fc2d,
		0x6490,
		0x49e0,
		{ 0x94, 0x35, 0x8d, 0x87, 0xc8, 0xb4, 0x32, 0xd1 }
	};
	inline constexpr GUID kPatchedPixelShaderStockBytecodeGuid{
		0x10934da7,
		0xb4c5,
		0x4f8c,
		{ 0x8f, 0xc0, 0xb1, 0x12, 0x63, 0x8a, 0x76, 0x34 }
	};

	struct PatchedPixelShaderIdentity
	{
		std::uint32_t magic = 0x53535343;
		std::uint16_t version = 1;
		std::uint16_t target = 0;
		std::uint32_t planIndex = 0;
		std::array<std::uint8_t, 16> patchedSha256Prefix{};
	};
	static_assert(std::is_trivially_copyable_v<PatchedPixelShaderIdentity>);

	using CreatePatchedPixelShaderFunction = HRESULT (*)(
		ID3D11Device*,
		const void*,
		SIZE_T,
		ID3D11ClassLinkage*,
		ID3D11PixelShader**) noexcept;
	using BindPixelShaderFunction = void (*)(
		void*,
		ID3D11PixelShader*) noexcept;

	enum class PublishStatus : std::uint8_t
	{
		kAccepted,
		kInvalidRequest,
		kCreateRejected,
		kIdentityRejected
	};

	enum class RestoreStockStatus : std::uint8_t
	{
		kRestored,
		kInvalidRequest,
		kInvalidIdentity,
		kMissingBytecode,
		kHashMismatch,
		kCreateRejected
	};

	PatchedPixelShaderIdentity MakeIdentity(
		const engine::dxbc_patch::Plan& a_plan,
		std::size_t a_planIndex) noexcept;
	PublishStatus PublishPatchedPixelShader(
		const engine::PixelShaderSwapRequest& a_request,
		const engine::dxbc_patch::Plan& a_plan,
		std::size_t a_planIndex,
		std::span<const std::byte> a_bytecode,
		CreatePatchedPixelShaderFunction a_create) noexcept;
	std::optional<PatchedPixelShaderIdentity> ReadIdentity(
		ID3D11PixelShader* a_shader,
		std::span<const engine::dxbc_patch::Plan> a_plans) noexcept;
	bool MatchesPatchedPixelShader(
		ID3D11PixelShader* a_shader,
		engine::dxbc_patch::Target a_target,
		std::span<const engine::dxbc_patch::Plan> a_plans) noexcept;
	RestoreStockStatus RestoreStockPixelShader(
		ID3D11PixelShader* a_patchedShader,
		ID3D11Device* a_device,
		std::span<const engine::dxbc_patch::Plan> a_plans,
		CreatePatchedPixelShaderFunction a_create,
		void* a_bindContext,
		BindPixelShaderFunction a_bind) noexcept;
}
