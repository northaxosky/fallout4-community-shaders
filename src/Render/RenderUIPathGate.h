#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <limits>
#include <optional>

namespace cs::engine
{
	enum class RenderUIOutputExtent : std::uint8_t
	{
		kInvalid,
		kCommitted,
		kFull
	};

	[[nodiscard]] inline RenderUIOutputExtent ClassifyRenderUIOutputExtent(
		float a_topLeftX,
		float a_topLeftY,
		float a_width,
		float a_height,
		std::uint32_t a_committedWidth,
		std::uint32_t a_committedHeight,
		std::uint32_t a_fullWidth,
		std::uint32_t a_fullHeight) noexcept
	{
		const auto isNear = [](float a_value, std::uint32_t a_expected) {
			return std::isfinite(a_value) &&
				std::abs(a_value - static_cast<float>(a_expected)) <= 0.5f;
		};
		if (!isNear(a_topLeftX, 0) || !isNear(a_topLeftY, 0) ||
			!a_committedWidth || !a_committedHeight ||
			!a_fullWidth || !a_fullHeight) {
			return RenderUIOutputExtent::kInvalid;
		}
		if (isNear(a_width, a_fullWidth) && isNear(a_height, a_fullHeight)) {
			return RenderUIOutputExtent::kFull;
		}
		if (isNear(a_width, a_committedWidth) &&
			isNear(a_height, a_committedHeight)) {
			return RenderUIOutputExtent::kCommitted;
		}
		return RenderUIOutputExtent::kInvalid;
	}

	class RenderUIPathGate
	{
	public:
		[[nodiscard]] static std::optional<RenderUIPathGate> Decode(
			std::uintptr_t a_compareInstruction,
			std::uintptr_t a_dataBegin,
			std::size_t a_dataSize,
			std::uintptr_t a_expectedTarget = 0) noexcept
		{
			if (!a_compareInstruction || !a_dataBegin || !a_dataSize ||
				a_dataBegin > std::numeric_limits<std::uintptr_t>::max() - a_dataSize) {
				return std::nullopt;
			}

			const auto* bytes =
				reinterpret_cast<const std::uint8_t*>(a_compareInstruction);
			if (bytes[0] != 0x80 || bytes[1] != 0x3D || bytes[6] != 0x00) {
				return std::nullopt;
			}

			std::int32_t displacement = 0;
			std::memcpy(&displacement, bytes + 2, sizeof(displacement));
			const auto instructionEnd = a_compareInstruction + kInstructionLength;
			const auto target = static_cast<std::uintptr_t>(
				static_cast<std::intptr_t>(instructionEnd) + displacement);
			const auto dataEnd = a_dataBegin + a_dataSize;
			if (target < a_dataBegin || target >= dataEnd ||
				(a_expectedTarget && target != a_expectedTarget)) {
				return std::nullopt;
			}

			return RenderUIPathGate(target);
		}

		[[nodiscard]] bool TakesFullEffectsPath() const noexcept
		{
			return *_value != 0;
		}

		[[nodiscard]] std::uintptr_t Address() const noexcept
		{
			return reinterpret_cast<std::uintptr_t>(_value);
		}

		static constexpr std::size_t kInstructionLength = 7;

	private:
		explicit RenderUIPathGate(std::uintptr_t a_address) noexcept :
			_value(reinterpret_cast<const volatile std::uint8_t*>(a_address))
		{}

		const volatile std::uint8_t* _value = nullptr;
	};
}
