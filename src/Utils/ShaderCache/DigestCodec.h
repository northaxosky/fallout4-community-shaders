#pragma once

#include "Utils/ByteCodec.h"
#include "Utils/CSSha256.h"

#include <algorithm>

namespace cs::shader_cache
{
	using bytes::ByteReader;
	using bytes::ByteWriter;

	inline void WriteDigest(ByteWriter& a_writer, const sha256::Sha256Result& a_digest)
	{
		a_writer.Bytes(a_digest.bytes.data(), a_digest.bytes.size());
	}

	inline bool ReadDigest(ByteReader& a_reader, sha256::Sha256Result& a_digest) noexcept
	{
		const auto view = a_reader.View(a_digest.bytes.size());
		if (view.empty())
			return false;
		std::copy(view.begin(), view.end(), a_digest.bytes.begin());
		return true;
	}
}
