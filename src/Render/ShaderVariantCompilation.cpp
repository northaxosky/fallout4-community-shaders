#include "Render/ShaderVariantCompilation.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <list>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <utility>

namespace cs::engine
{
	namespace
	{
		class AsyncShaderVariantCompilationHandle final :
			public ShaderVariantCompilationHandle
		{
		public:
			AsyncShaderVariantCompilationHandle() = default;

			ShaderVariantCompilationState GetState() const noexcept override
			{
				return _state.load(std::memory_order_acquire);
			}

			winrt::com_ptr<ID3D11DeviceChild> Acquire() noexcept override
			{
				if (GetState() != ShaderVariantCompilationState::kReady)
					return {};
				std::scoped_lock lock(_mutex);
				return _shader;
			}

			std::string GetError() const override
			{
				if (GetState() != ShaderVariantCompilationState::kFailed)
					return {};
				std::scoped_lock lock(_mutex);
				return _error;
			}

			util::ShaderSamplerUse GetSamplerUse() const noexcept override
			{
				return GetState() == ShaderVariantCompilationState::kReady ? _samplers : util::ShaderSamplerUse{};
			}

			void Complete(ShaderVariantCompilationOutput a_output)
			{
				const bool ready = !!a_output.shader;
				{
					std::scoped_lock lock(_mutex);
					if (ready) {
						_shader = std::move(a_output.shader);
						_samplers = a_output.samplers;
						_error.clear();
					} else {
						_shader = nullptr;
						_error = a_output.error.empty() ?
						             "shader compilation failed" :
						             std::move(a_output.error);
					}
				}
				_state.store(
					ready ?
						ShaderVariantCompilationState::kReady :
						ShaderVariantCompilationState::kFailed,
					std::memory_order_release);
			}

			void Fail(std::string a_error)
			{
				{
					std::scoped_lock lock(_mutex);
					_shader = nullptr;
					_error = std::move(a_error);
				}
				_state.store(
					ShaderVariantCompilationState::kFailed,
					std::memory_order_release);
			}

		private:
			std::atomic<ShaderVariantCompilationState> _state{
				ShaderVariantCompilationState::kPending
			};
			mutable std::mutex _mutex;
			winrt::com_ptr<ID3D11DeviceChild> _shader;
			std::string _error;
			util::ShaderSamplerUse _samplers;
		};

		class BatchTracker
		{
		public:
			void Enqueued()
			{
				const std::scoped_lock lock(_mutex);
				if (_outstanding++ == 0) {
					_batch = { .active = true };
					_started = std::chrono::steady_clock::now();
					_active.store(true, std::memory_order_relaxed);
				}
				++_batch.totalTasks;
			}

			void Finished(bool a_succeeded, bool a_cacheHit)
			{
				Settle([&] {
					++(a_succeeded ? _batch.completedTasks : _batch.failedTasks);
					_batch.cacheHits += a_succeeded && a_cacheHit;
				});
			}

			void Cancelled()
			{
				Settle([&] { --_batch.totalTasks; });
			}

			void AddObserver(ShaderCompileBatchObserver a_observer)
			{
				const std::scoped_lock lock(_mutex);
				_observers.push_back(std::move(a_observer));
			}

			// Lock-free; lets per-frame pollers skip the snapshot while idle.
			bool Active() const noexcept { return _active.load(std::memory_order_relaxed); }

			ShaderCompileBatch Snapshot() const
			{
				const std::scoped_lock lock(_mutex);
				return Current();
			}

		private:
			ShaderCompileBatch Current() const
			{
				auto batch = _batch;
				if (batch.active) {
					batch.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
						std::chrono::steady_clock::now() - _started);
				}
				return batch;
			}

			template <class Update>
			void Settle(Update&& a_update)
			{
				ShaderCompileBatch finished;
				std::vector<ShaderCompileBatchObserver> observers;
				{
					const std::scoped_lock lock(_mutex);
					a_update();
					if (--_outstanding != 0)
						return;
					_batch.active = false;
					_active.store(false, std::memory_order_relaxed);
					_batch.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
						std::chrono::steady_clock::now() - _started);
					if (_batch.totalTasks == 0)
						return;
					finished = _batch;
					observers = _observers;
				}
				for (const auto& observer : observers) {
					try {
						observer(finished);
					} catch (...) {
					}
				}
			}

			mutable std::mutex _mutex;
			std::vector<ShaderCompileBatchObserver> _observers;
			ShaderCompileBatch _batch;
			std::chrono::steady_clock::time_point _started;
			std::uint32_t _outstanding = 0;
			std::atomic<bool> _active{ false };
		};

		BatchTracker& Batches()
		{
			static BatchTracker tracker;
			return tracker;
		}

		struct CompilationKey
		{
			std::uint64_t generation = 0;
			std::filesystem::path sourcePath;
			std::string entryPoint;
			std::string profile;
			ShaderStage stage = ShaderStage::kPixel;
			std::vector<std::pair<std::string, std::string>> defines;

			bool operator==(const CompilationKey&) const = default;
		};

		struct CompilationKeyHash
		{
			std::size_t operator()(const CompilationKey& a_key) const noexcept
			{
				auto value = std::hash<std::uint64_t>{}(a_key.generation);
				value = value * 131U + std::filesystem::hash_value(a_key.sourcePath);
				value = value * 131U + std::hash<std::string>{}(a_key.entryPoint);
				value = value * 131U + std::hash<std::string>{}(a_key.profile);
				value = value * 131U + static_cast<std::size_t>(a_key.stage);
				for (const auto& [name, defineValue] : a_key.defines) {
					value = value * 131U + std::hash<std::string>{}(name);
					value = value * 131U + std::hash<std::string>{}(defineValue);
				}
				return value;
			}
		};

		struct CompilationTask
		{
			std::uint64_t generation = 0;
			ShaderVariantCompilationRequest request;
			std::shared_ptr<AsyncShaderVariantCompilationHandle> handle;
		};

		class AsyncShaderVariantCompilationCache final :
			public ShaderVariantCompilationCache
		{
		public:
			AsyncShaderVariantCompilationCache(
				ShaderVariantCompiler a_compiler,
				std::size_t a_workerCount) :
				_compiler(std::move(a_compiler))
			{
				a_workerCount = std::max<std::size_t>(a_workerCount, 1);
				_workers.reserve(a_workerCount);
				for (std::size_t index = 0;
					index < a_workerCount;
					++index) {
					_workers.emplace_back(
						[this](std::stop_token a_stopToken) {
							RunWorker(a_stopToken);
						});
				}
			}

			~AsyncShaderVariantCompilationCache() override
			{
				Stop();
			}

			std::shared_ptr<ShaderVariantCompilationHandle> Request(
				ShaderVariantCompilationRequest a_request) override
			{
				std::shared_ptr<AsyncShaderVariantCompilationHandle> handle;
				{
					std::scoped_lock lock(_mutex);
					a_request.sourceGeneration = _generation;
					CompilationKey key{
						.generation = _generation,
						.sourcePath =
							a_request.sourcePath.lexically_normal(),
						.entryPoint = a_request.entryPoint,
						.profile = a_request.profile,
						.stage = a_request.stage,
						.defines = a_request.defines
					};
					if (const auto existing = _entries.find(key);
						existing != _entries.end()) {
						return existing->second;
					}

					handle =
						std::make_shared<
							AsyncShaderVariantCompilationHandle>();
					if (_stopped) {
						handle->Fail("shader compilation stopped");
						return handle;
					}
					_entries.emplace(std::move(key), handle);
					Batches().Enqueued();
					_queue.push_back({ .generation = _generation,
						.request = std::move(a_request),
						.handle = handle });
				}
				_condition.notify_one();
				return handle;
			}

			void Invalidate() override
			{
				std::list<CompilationTask> invalidated;
				{
					std::scoped_lock lock(_mutex);
					if (_stopped)
						return;
					++_generation;
					_entries.clear();
					invalidated = TakeQueuedTasks();
				}
				for (auto& task : invalidated) {
					task.handle->Fail("shader compilation invalidated");
					Batches().Cancelled();
				}
			}

			void Stop() noexcept override
			{
				std::list<CompilationTask> stopped;
				{
					std::scoped_lock lock(_mutex);
					if (_stopped)
						return;
					_stopped = true;
					_entries.clear();
					stopped = TakeQueuedTasks();
				}
				for (auto& task : stopped) {
					task.handle->Fail("shader compilation stopped");
					Batches().Cancelled();
				}
				for (auto& worker : _workers)
					worker.request_stop();
				_condition.notify_all();
				_workers.clear();
			}

			void Demand(const std::shared_ptr<ShaderVariantCompilationHandle>& a_handle) override
			{
				std::scoped_lock lock(_mutex);
				const auto task = std::ranges::find(_queue, a_handle, &CompilationTask::handle);
				if (task != _queue.end())
					_demandedQueue.splice(_demandedQueue.end(), _queue, task);
			}

		private:
			std::list<CompilationTask> TakeQueuedTasks()
			{
				std::list<CompilationTask> tasks;
				tasks.splice(tasks.end(), _demandedQueue);
				tasks.splice(tasks.end(), _queue);
				return tasks;
			}

			void RunWorker(std::stop_token a_stopToken)
			{
				// As upstream; Normal starves the BelowNormal loading-screen thread.
				SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
				for (;;) {
					CompilationTask task;
					{
						std::unique_lock lock(_mutex);
						_condition.wait(
							lock,
							a_stopToken,
							[this] {
								return _stopped || !_demandedQueue.empty() || !_queue.empty();
							});
						if (a_stopToken.stop_requested() || _stopped)
							return;
						auto& queue = _demandedQueue.empty() ? _queue : _demandedQueue;
						task = std::move(queue.front());
						queue.pop_front();
					}

					ShaderVariantCompilationOutput output;
					try {
						output = _compiler(std::move(task.request));
					} catch (const std::exception& error) {
						output.error = error.what();
					} catch (...) {
						output.error = "shader compiler raised an unknown exception";
					}
					bool publish = false;
					{
						std::scoped_lock lock(_mutex);
						publish =
							!_stopped && task.generation == _generation;
					}
					if (publish) {
						const bool succeeded = !!output.shader;
						const bool cacheHit = output.cacheHit;
						task.handle->Complete(std::move(output));
						Batches().Finished(succeeded, cacheHit);
					} else {
						task.handle->Fail(
							"shader compilation invalidated");
						Batches().Cancelled();
					}
				}
			}

			std::mutex _mutex;
			std::condition_variable_any _condition;
			std::uint64_t _generation = 1;
			bool _stopped = false;
			std::list<CompilationTask> _queue;
			std::list<CompilationTask> _demandedQueue;
			std::unordered_map<
				CompilationKey,
				std::shared_ptr<AsyncShaderVariantCompilationHandle>,
				CompilationKeyHash>
				_entries;
			ShaderVariantCompiler _compiler;
			std::vector<std::jthread> _workers;
		};
	}

	void AddShaderCompileBatchObserver(ShaderCompileBatchObserver a_observer)
	{
		Batches().AddObserver(std::move(a_observer));
	}

	bool IsShaderCompileBatchActive() noexcept
	{
		return Batches().Active();
	}

	ShaderCompileBatch GetShaderCompileBatch()
	{
		return Batches().Snapshot();
	}

	std::shared_ptr<ShaderVariantCompilationCache>
	CreateAsyncShaderVariantCompilationCache(
		ShaderVariantCompiler a_compiler,
		std::size_t a_workerCount)
	{
		return std::make_shared<AsyncShaderVariantCompilationCache>(
			std::move(a_compiler),
			a_workerCount);
	}
}
