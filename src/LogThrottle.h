#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <type_traits>

#include <spdlog/spdlog.h>

// CS_LOG_ONCE logs only on first hit; each macro has independent thread-local state per expansion site.
#define CS_LOG_ONCE(logger, level, ...) \
	do { \
		static thread_local bool csLogOnceLogged = false; \
		if (!csLogOnceLogged) { \
			csLogOnceLogged = true; \
			(logger)->log((level), __VA_ARGS__); \
		} \
	} while (0)

// CS_LOG_EVERY_N logs on first hit and every N hits after; suppress 4127 because constant n makes the guard constant.
#define CS_LOG_EVERY_N(logger, n, level, ...) \
	do { \
		static thread_local std::uint64_t csLogEveryNCounter = 0; \
		const auto csLogEveryNIntervalValue = (n); \
		__pragma(warning(suppress: 4127)) \
		if (csLogEveryNIntervalValue >= 1) { \
			const auto csLogEveryNInterval = static_cast<std::uint64_t>(csLogEveryNIntervalValue); \
			if (csLogEveryNCounter == 0) { \
				(logger)->log((level), __VA_ARGS__); \
			} \
			++csLogEveryNCounter; \
			if (csLogEveryNCounter >= csLogEveryNInterval) { \
				csLogEveryNCounter = 0; \
			} \
		} \
	} while (0)

// CS_LOG_EVERY_MS logs on the first hit and at most once per interval.
#define CS_LOG_EVERY_MS(logger, ms, level, ...) \
	do { \
		using CsLogEveryMsClock = std::chrono::steady_clock; \
		static thread_local bool csLogEveryMsLogged = false; \
		static thread_local CsLogEveryMsClock::time_point csLogEveryMsLast; \
		const auto csLogEveryMsNow = CsLogEveryMsClock::now(); \
		const auto csLogEveryMsInterval = \
			std::chrono::milliseconds{ static_cast<std::chrono::milliseconds::rep>(ms) }; \
		if (!csLogEveryMsLogged || csLogEveryMsNow - csLogEveryMsLast >= csLogEveryMsInterval) { \
			csLogEveryMsLogged = true; \
			csLogEveryMsLast = csLogEveryMsNow; \
			(logger)->log((level), __VA_ARGS__); \
		} \
	} while (0)

// CS_LOG_ON_CHANGE logs on the first hit and whenever the observed value changes.
#define CS_LOG_ON_CHANGE(logger, level, value, ...) \
	do { \
		const auto csLogOnChangeValue = (value); \
		using CsLogOnChangeValue = std::decay_t<decltype(csLogOnChangeValue)>; \
		static thread_local std::optional<CsLogOnChangeValue> csLogOnChangeLast; \
		if (!csLogOnChangeLast || *csLogOnChangeLast != csLogOnChangeValue) { \
			csLogOnChangeLast = csLogOnChangeValue; \
			(logger)->log((level), __VA_ARGS__); \
		} \
	} while (0)
