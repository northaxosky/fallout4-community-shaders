#include "LogThrottle.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <iostream>
#include <memory>
#include <mutex>
#include <string_view>
#include <thread>

#include <spdlog/sinks/base_sink.h>

namespace
{
	using namespace std::chrono_literals;

	int failures = 0;

	void Check(bool a_condition, std::string_view a_expression, int a_line)
	{
		if (!a_condition) {
			std::cerr << "CHECK failed at line " << a_line << ": " << a_expression << '\n';
			++failures;
		}
	}

#define CHECK(a_expression) Check(static_cast<bool>(a_expression), #a_expression, __LINE__)

	class CountingSink final : public spdlog::sinks::base_sink<std::mutex>
	{
	public:
		std::size_t Count() const noexcept
		{
			return _count.load(std::memory_order_relaxed);
		}

	protected:
		void sink_it_(const spdlog::details::log_msg&) override
		{
			_count.fetch_add(1, std::memory_order_relaxed);
		}

		void flush_() override {}

	private:
		std::atomic<std::size_t> _count{ 0 };
	};

	void TestOnce()
	{
		auto sink = std::make_shared<CountingSink>();
		spdlog::logger logger("once", sink);

		for (int hit = 0; hit < 10; ++hit) {
			CS_LOG_ONCE(&logger, spdlog::level::info, "hit {}", hit);
		}

		CHECK(sink->Count() == 1);
	}

	void TestEveryN()
	{
		auto sink = std::make_shared<CountingSink>();
		spdlog::logger logger("every-n", sink);

		for (int hit = 1; hit <= 10; ++hit) {
			CS_LOG_EVERY_N(&logger, 3, spdlog::level::info, "hit {}", hit);
		}

		CHECK(sink->Count() == 4);
	}

	void TestEveryNGuard()
	{
		auto sink = std::make_shared<CountingSink>();
		spdlog::logger logger("every-n-guard", sink);

		for (int interval = -1; interval <= 0; ++interval) {
			CS_LOG_EVERY_N(&logger, interval, spdlog::level::info, "interval {}", interval);
		}

		CHECK(sink->Count() == 0);
	}

	void TestEveryMs()
	{
		auto sink = std::make_shared<CountingSink>();
		spdlog::logger logger("every-ms", sink);

		for (int hit = 0; hit < 3; ++hit) {
			CS_LOG_EVERY_MS(&logger, 20, spdlog::level::info, "hit {}", hit);
			if (hit == 1) {
				CHECK(sink->Count() == 1);
				std::this_thread::sleep_for(30ms);
			}
		}

		CHECK(sink->Count() == 2);
	}

	void TestOnChange()
	{
		auto sink = std::make_shared<CountingSink>();
		spdlog::logger logger("on-change", sink);
		constexpr int values[]{ 1, 1, 2, 2, 2, 3 };

		for (const int value : values) {
			CS_LOG_ON_CHANGE(&logger, spdlog::level::info, value, "value {}", value);
		}

		CHECK(sink->Count() == 3);
	}
}

int main()
{
	TestOnce();
	TestEveryN();
	TestEveryNGuard();
	TestEveryMs();
	TestOnChange();

	if (failures != 0) {
		std::cerr << failures << " check(s) failed\n";
		return 1;
	}

	std::cout << "LogThrottle tests passed\n";
	return 0;
}
