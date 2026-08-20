#pragma once

#include "Utils/CSSha256.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cs::shader_cache
{
	// Every persisted field is written explicitly in little-endian order; no struct layout is trusted.
	class ByteWriter
	{
	public:
		explicit ByteWriter(std::vector<std::uint8_t>& a_bytes) noexcept :
			_bytes(&a_bytes)
		{}

		void U8(std::uint8_t a_value)
		{
			_bytes->push_back(a_value);
		}

		void U32(std::uint32_t a_value)
		{
			for (unsigned shift = 0; shift < 32; shift += 8)
				_bytes->push_back(static_cast<std::uint8_t>((a_value >> shift) & 0xFFu));
		}

		void U64(std::uint64_t a_value)
		{
			for (unsigned shift = 0; shift < 64; shift += 8)
				_bytes->push_back(static_cast<std::uint8_t>((a_value >> shift) & 0xFFu));
		}

		void Bytes(const void* a_data, std::size_t a_length)
		{
			if (a_length == 0)
				return;
			const auto* first = static_cast<const std::uint8_t*>(a_data);
			_bytes->insert(_bytes->end(), first, first + a_length);
		}

		void Digest(const sha256::Sha256Result& a_digest)
		{
			Bytes(a_digest.bytes.data(), a_digest.bytes.size());
		}

		void Text(std::string_view a_text)
		{
			U32(static_cast<std::uint32_t>(a_text.size()));
			Bytes(a_text.data(), a_text.size());
		}

	private:
		std::vector<std::uint8_t>* _bytes;
	};

	// Bounds-checked counterpart; every read reports failure instead of trusting the file.
	class ByteReader
	{
	public:
		explicit ByteReader(std::span<const std::uint8_t> a_bytes) noexcept :
			_bytes(a_bytes)
		{}

		[[nodiscard]] std::size_t Remaining() const noexcept
		{
			return _bytes.size() - _offset;
		}

		[[nodiscard]] bool AtEnd() const noexcept
		{
			return Remaining() == 0;
		}

		bool U8(std::uint8_t& a_value) noexcept
		{
			if (Remaining() < 1)
				return false;
			a_value = _bytes[_offset++];
			return true;
		}

		bool U32(std::uint32_t& a_value) noexcept
		{
			if (Remaining() < 4)
				return false;
			a_value = 0;
			for (unsigned index = 0; index < 4; ++index)
				a_value |= static_cast<std::uint32_t>(_bytes[_offset + index]) << (index * 8);
			_offset += 4;
			return true;
		}

		bool U64(std::uint64_t& a_value) noexcept
		{
			if (Remaining() < 8)
				return false;
			a_value = 0;
			for (unsigned index = 0; index < 8; ++index)
				a_value |= static_cast<std::uint64_t>(_bytes[_offset + index]) << (index * 8);
			_offset += 8;
			return true;
		}

		bool Digest(sha256::Sha256Result& a_digest) noexcept
		{
			const auto view = View(a_digest.bytes.size());
			if (view.empty())
				return false;
			std::copy(view.begin(), view.end(), a_digest.bytes.begin());
			return true;
		}

		// The length is checked against a_limit before anything is allocated.
		bool Text(std::string& a_text, std::size_t a_limit)
		{
			std::uint32_t length = 0;
			if (!U32(length) || length > a_limit || length > Remaining())
				return false;
			a_text.assign(
				reinterpret_cast<const char*>(_bytes.data() + _offset),
				length);
			_offset += length;
			return true;
		}

		[[nodiscard]] std::span<const std::uint8_t> View(std::size_t a_length) noexcept
		{
			if (a_length == 0 || Remaining() < a_length)
				return {};
			const auto view = _bytes.subspan(_offset, a_length);
			_offset += a_length;
			return view;
		}

	private:
		std::span<const std::uint8_t> _bytes;
		std::size_t                   _offset = 0;
	};
}
