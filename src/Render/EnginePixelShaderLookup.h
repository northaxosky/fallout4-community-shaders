#pragma once

#include <atomic>
#include <compare>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace cs::engine
{
	enum class EnginePixelShaderLookupTarget : std::uint8_t
	{
		kBsdfLight
	};

	class EngineLookupPsid
	{
	public:
		constexpr EngineLookupPsid() noexcept = default;
		explicit constexpr EngineLookupPsid(std::uint32_t a_value) noexcept :
			_value(a_value)
		{}

		[[nodiscard]] constexpr std::uint32_t Value() const noexcept
		{
			return _value;
		}

		auto operator<=>(const EngineLookupPsid&) const = default;

	private:
		std::uint32_t _value = 0;
	};

	struct EnginePixelShaderLookupObservation
	{
		EnginePixelShaderLookupTarget target =
			EnginePixelShaderLookupTarget::kBsdfLight;
		std::uint32_t functionInput = 0;
		EngineLookupPsid returnedPsid;
		std::uint64_t callSequence = 0;
		std::uint32_t threadId = 0;

		auto operator<=>(const EnginePixelShaderLookupObservation&) const =
			default;
	};

	struct EnginePixelShaderLookupTargetDescriptor
	{
		EnginePixelShaderLookupTarget target =
			EnginePixelShaderLookupTarget::kBsdfLight;
		std::string_view subclass;
		std::string_view engineSymbol;
		std::uintptr_t engineTargetAddress = 0;
		std::uint64_t engineCodeByteLength = 0;
		std::uintptr_t observerAddress = 0;
		bool installed = false;
	};

	struct EnginePixelShaderLookupInstallStats
	{
		unsigned attempted = 0;
		unsigned succeeded = 0;
		unsigned failed = 0;

		[[nodiscard]] bool Ready() const noexcept
		{
			return attempted != 0
				&& succeeded == attempted
				&& failed == 0;
		}
	};

	struct EnginePixelShaderLookupTelemetrySnapshot
	{
		std::uint64_t returnsSeen = 0;
		std::uint64_t returnsScoped = 0;
		std::uint64_t returnsCaptured = 0;
		std::uint64_t returnsConsumed = 0;
		std::uint64_t discardedOutOfScope = 0;
		std::uint64_t discardedSubclassMismatch = 0;
		std::uint64_t discardedTechniqueMismatch = 0;
		std::uint64_t discardedDuplicate = 0;
		std::uint64_t discardedWithoutCreate = 0;
	};

	[[nodiscard]] bool EnginePixelShaderLookupRelationshipsHold(
		const EnginePixelShaderLookupTelemetrySnapshot& a_snapshot) noexcept;
	[[nodiscard]] std::string_view EnginePixelShaderLookupTargetName(
		EnginePixelShaderLookupTarget a_target) noexcept;
	[[nodiscard]] std::string_view EnginePixelShaderLookupTargetSubclass(
		EnginePixelShaderLookupTarget a_target) noexcept;
	[[nodiscard]] std::string_view EnginePixelShaderLookupTargetSymbol(
		EnginePixelShaderLookupTarget a_target) noexcept;

	class EnginePixelShaderLookupScope
	{
	public:
		struct State
		{
			void* shader = nullptr;
			std::string_view subclass;
			std::uint32_t rawTechnique = 0;
			std::optional<EnginePixelShaderLookupObservation> observation;
			bool active = false;
			bool ambiguous = false;
			bool consumed = false;
		};

		EnginePixelShaderLookupScope(
			void* a_shader,
			std::string_view a_subclass,
			std::uint32_t a_rawTechnique) noexcept;
		~EnginePixelShaderLookupScope() noexcept;

		EnginePixelShaderLookupScope(
			const EnginePixelShaderLookupScope&) = delete;
		EnginePixelShaderLookupScope(
			EnginePixelShaderLookupScope&&) = delete;
		EnginePixelShaderLookupScope& operator=(
			const EnginePixelShaderLookupScope&) = delete;
		EnginePixelShaderLookupScope& operator=(
			EnginePixelShaderLookupScope&&) = delete;

	private:
		State _previous;
	};

	void RecordEnginePixelShaderLookupReturn(
		EnginePixelShaderLookupTarget a_target,
		std::uint32_t a_functionInput,
		std::uint32_t a_returnedPsid) noexcept;
	[[nodiscard]] std::optional<EnginePixelShaderLookupObservation>
		ConsumeEnginePixelShaderLookup(
			void* a_shader,
			std::string_view a_subclass,
			std::uint32_t a_rawTechnique) noexcept;

	void InstallEnginePixelShaderLookupHooks() noexcept;
	[[nodiscard]] EnginePixelShaderLookupInstallStats
		GetEnginePixelShaderLookupInstallStats() noexcept;
	[[nodiscard]] std::span<const EnginePixelShaderLookupTargetDescriptor>
		GetEnginePixelShaderLookupTargetDescriptors() noexcept;
	[[nodiscard]] EnginePixelShaderLookupTelemetrySnapshot
		SnapshotEnginePixelShaderLookupTelemetry() noexcept;

#ifdef FO4CS_ENGINE_LOOKUP_TESTING
	void ResetEnginePixelShaderLookupForTesting() noexcept;
#endif
}
