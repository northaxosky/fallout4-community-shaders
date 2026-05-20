#pragma once

namespace cs::features::catalog::hooks
{
	namespace detail
	{
		inline thread_local unsigned recordSuppressionDepth = 0;
	}

	class ScopedRecordSuppression
	{
	public:
		ScopedRecordSuppression() noexcept { ++detail::recordSuppressionDepth; }
		~ScopedRecordSuppression() noexcept { --detail::recordSuppressionDepth; }

		ScopedRecordSuppression(const ScopedRecordSuppression&) = delete;
		ScopedRecordSuppression& operator=(const ScopedRecordSuppression&) = delete;
	};

	inline bool RecordingSuppressed() noexcept
	{
		return detail::recordSuppressionDepth != 0;
	}
}
