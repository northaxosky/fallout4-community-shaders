#include "Format/BgsmV100.h"

#include "Utils/ByteCodec.h"

#include <format>
#include <string_view>

namespace cs::features::true_pbr
{
	namespace
	{
		constexpr std::size_t kMaxPathBytes = 1024;

		// Reads fields in declaration order; the first failure sticks and later reads return zeros.
		class FieldReader
		{
		public:
			explicit FieldReader(std::span<const std::uint8_t> a_bytes) noexcept :
				_reader(a_bytes)
			{}

			std::uint8_t U8() noexcept { return Read<std::uint8_t>(&bytes::ByteReader::U8); }
			std::uint32_t U32() noexcept { return Read<std::uint32_t>(&bytes::ByteReader::U32); }
			float F32() noexcept { return Read<float>(&bytes::ByteReader::F32); }
			bool Bool() noexcept { return U8() != 0; }

			template <std::size_t N>
			std::array<float, N> Floats() noexcept
			{
				std::array<float, N> values{};
				for (auto& value : values)
					value = F32();
				return values;
			}

			// BGSM string: u32 length including the terminating null.
			std::string Text()
			{
				const auto length = U32();
				if (!_ok || length > kMaxPathBytes || length > _reader.Remaining())
					return Fail<std::string>();
				if (length == 0)
					return {};
				const auto bytes = _reader.View(length);
				std::string text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
				if (text.back() == '\0')
					text.pop_back();
				if (text.find('\0') != std::string::npos)
					return Fail<std::string>();
				return text;
			}

			[[nodiscard]] bool Ok() const noexcept { return _ok; }
			[[nodiscard]] bool AtEnd() const noexcept { return _reader.AtEnd(); }

		private:
			template <class T>
			T Read(bool (bytes::ByteReader::*a_read)(T&) noexcept) noexcept
			{
				T value{};
				if (!_ok || !(_reader.*a_read)(value))
					return Fail<T>();
				return value;
			}

			template <class T>
			T Fail() noexcept
			{
				_ok = false;
				return {};
			}

			bytes::ByteReader _reader;
			bool _ok = true;
		};

		std::expected<void, std::string> DecodeCspb(std::span<const std::uint8_t> a_payload, PbrMaterial& a_material)
		{
			FieldReader in(a_payload);
			if (const auto layout = in.U32(); in.Ok() && layout != kCspbLayout)
				return std::unexpected(std::format("Layout must be {} (is {}).", kCspbLayout, layout));

			a_material.tileFlags = in.U32();
			a_material.uvOffset = in.Floats<2>();
			a_material.uvScale = in.Floats<2>();
			a_material.alpha = in.F32();
			a_material.alphaBlend = in.U8();
			a_material.blendSource = in.U32();
			a_material.blendDestination = in.U32();
			a_material.alphaTestRef = in.U8();
			a_material.alphaTest = in.Bool();
			a_material.refraction = in.Bool();
			a_material.refractionFalloff = in.Bool();
			a_material.refractionPower = in.F32();
			a_material.modelSpaceNormals = in.Bool();
			a_material.emitEnabled = in.Bool();
			a_material.emittanceColor = in.Floats<3>();
			a_material.emittanceMult = in.F32();
			a_material.externalEmittance = in.Bool();
			for (auto& path : a_material.textures)
				path = in.Text();
			a_material.payloadVersion = in.U32();
			a_material.featureFlags = in.U32();
			a_material.parameters = in.Floats<kParameterCount>();

			if (!in.Ok())
				return std::unexpected("CSPB payload is truncated or malformed.");
			if (!in.AtEnd())
				return std::unexpected("CSPB payload has trailing bytes.");
			return {};
		}

		std::expected<void, std::string> DecodeCsts(std::span<const std::uint8_t> a_payload, PbrMaterial& a_material)
		{
			FieldReader in(a_payload);
			const auto values = in.Floats<4>();
			if (!in.Ok() || !in.AtEnd())
				return std::unexpected("CSTS payload must be four floats.");
			a_material.thinSurface = ThinSurfaceOverride{ values[0], values[1], values[2], values[3] };
			return {};
		}

		std::expected<void, std::string> DecodeCsfz(std::span<const std::uint8_t> a_payload, PbrMaterial& a_material)
		{
			FieldReader in(a_payload);
			const auto roughness = in.F32();
			const auto fromGreen = in.U32();
			if (!in.Ok() || !in.AtEnd() || fromGreen > 1)
				return std::unexpected("CSFZ payload must be a float and a 0 or 1 flag.");
			a_material.fuzz = FuzzOverride{ roughness, fromGreen != 0 };
			return {};
		}

		std::string Tag(std::span<const std::uint8_t> a_tag)
		{
			return { a_tag.begin(), a_tag.end() };
		}

	}

	std::optional<std::uint32_t> ReadBgsmVersion(std::span<const std::uint8_t> a_file)
	{
		bytes::ByteReader in(a_file);
		const auto magic = in.View(4);
		std::uint32_t version = 0;
		if (magic.size() != 4 || std::string_view(reinterpret_cast<const char*>(magic.data()), 4) != "BGSM" || !in.U32(version))
			return std::nullopt;
		return version;
	}

	std::expected<DecodedPart2, std::string> DecodePart2(std::span<const std::uint8_t> a_part2)
	{
		DecodedPart2 decoded;
		bytes::ByteReader in(a_part2);
		std::span<const std::uint8_t> cspb, csts, csfz;
		bool hasCspb = false, hasCsts = false, hasCsfz = false;

		while (!in.AtEnd()) {
			const auto tag = in.View(4);
			std::uint32_t size = 0;
			if (tag.empty() || !in.U32(size))
				return std::unexpected("Truncated block header.");
			if (size > in.Remaining())
				return std::unexpected(std::format("Truncated block '{}'.", Tag(tag)));
			const auto payload = size == 0 ? std::span<const std::uint8_t>{} : in.View(size);

			const auto name = Tag(tag);
			const auto claim = [&](bool& a_seen, std::span<const std::uint8_t>& a_slot) -> std::expected<void, std::string> {
				if (a_seen)
					return std::unexpected(std::format("Duplicate block '{}'.", name));
				a_seen = true;
				a_slot = payload;
				return {};
			};

			std::expected<void, std::string> seen;
			if (name == "CSPB")
				seen = claim(hasCspb, cspb);
			else if (name == "CSTS")
				seen = claim(hasCsts, csts);
			else if (name == "CSFZ")
				seen = claim(hasCsfz, csfz);
			else
				decoded.unknownTags.push_back(name);
			if (!seen)
				return std::unexpected(seen.error());
		}

		if (!hasCspb)
			return std::unexpected("Missing CSPB block.");
		if (auto result = DecodeCspb(cspb, decoded.material); !result)
			return std::unexpected(result.error());
		if (hasCsts)
			if (auto result = DecodeCsts(csts, decoded.material); !result)
				return std::unexpected(result.error());
		if (hasCsfz)
			if (auto result = DecodeCsfz(csfz, decoded.material); !result)
				return std::unexpected(result.error());
		return decoded;
	}
}
