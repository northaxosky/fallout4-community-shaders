#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <thread>
#include <vector>

inline unsigned ParallelThreadCount()
{
	return std::clamp(std::thread::hardware_concurrency(), 1u, 8u);
}

// Runs a_body(index) for every index in [0, a_count) across worker threads.
template <class Body>
void ParallelFor(std::size_t a_count, Body&& a_body)
{
	std::atomic_size_t next = 0;
	const auto threadCount = ParallelThreadCount();
	std::vector<std::jthread> workers;
	for (unsigned i = 0; i < threadCount; ++i) {
		workers.emplace_back([&] {
			for (auto index = next.fetch_add(1); index < a_count; index = next.fetch_add(1))
				a_body(index);
		});
	}
}
