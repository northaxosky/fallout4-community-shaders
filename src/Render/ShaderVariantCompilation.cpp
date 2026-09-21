#include "Render/ShaderVariantCompilation.h"

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <exception>
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

			void Complete(ShaderVariantCompilationOutput a_output)
			{
				const bool ready = !!a_output.shader;
				{
					std::scoped_lock lock(_mutex);
					if (ready) {
						_shader = std::move(a_output.shader);
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
		};

		struct CompilationKey
		{
			std::uint64_t generation = 0;
			std::filesystem::path sourcePath;
			std::string entryPoint;
			std::string profile;
			ShaderStage stage = ShaderStage::kPixel;
			std::vector<std::pair<std::string, std::string>> defines;
			std::uint32_t familyId = 0xFFFFFFFFU;
			std::uint32_t descriptor = 0;
			std::string owner;

			bool operator==(const CompilationKey&) const = default;
		};

		struct CompilationKeyHash
		{
			std::size_t operator()(const CompilationKey& a_key) const noexcept
			{
				auto value = std::hash<std::uint64_t>{}(a_key.generation);
				value = value * 131U
					+ std::filesystem::hash_value(a_key.sourcePath);
				value = value * 131U
					+ std::hash<std::string>{}(a_key.entryPoint);
				value = value * 131U
					+ std::hash<std::string>{}(a_key.profile);
				value = value * 131U
					+ static_cast<std::size_t>(a_key.stage);
				value = value * 131U + a_key.familyId;
				value = value * 131U + a_key.descriptor;
				value = value * 131U
					+ std::hash<std::string>{}(a_key.owner);
				for (const auto& [name, defineValue] : a_key.defines) {
					value = value * 131U
						+ std::hash<std::string>{}(name);
					value = value * 131U
						+ std::hash<std::string>{}(defineValue);
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
						.defines = a_request.defines,
						.familyId = a_request.familyId,
						.descriptor = a_request.descriptor,
						.owner = a_request.owner
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
					_queue.push_back({
						.generation = _generation,
						.request = std::move(a_request),
						.handle = handle
					});
				}
				_condition.notify_one();
				return handle;
			}

			void Invalidate() override
			{
				std::deque<CompilationTask> invalidated;
				{
					std::scoped_lock lock(_mutex);
					if (_stopped)
						return;
					++_generation;
					_entries.clear();
					invalidated.swap(_queue);
				}
				for (auto& task : invalidated)
					task.handle->Fail("shader compilation invalidated");
			}

			void Stop() noexcept override
			{
				std::deque<CompilationTask> stopped;
				{
					std::scoped_lock lock(_mutex);
					if (_stopped)
						return;
					_stopped = true;
					_entries.clear();
					stopped.swap(_queue);
				}
				for (auto& task : stopped)
					task.handle->Fail("shader compilation stopped");
				for (auto& worker : _workers)
					worker.request_stop();
				_condition.notify_all();
				_workers.clear();
			}

		private:
			void RunWorker(std::stop_token a_stopToken)
			{
				for (;;) {
					CompilationTask task;
					{
						std::unique_lock lock(_mutex);
						_condition.wait(
							lock,
							a_stopToken,
							[this] {
								return _stopped || !_queue.empty();
							});
						if (a_stopToken.stop_requested() || _stopped)
							return;
						task = std::move(_queue.front());
						_queue.pop_front();
					}

					ShaderVariantCompilationOutput output;
					auto completion =
						std::move(task.request.completion);
					task.request.completion = {};
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
							!_stopped
							&& task.generation == _generation;
					}
					if (publish) {
						const auto state = output.shader ?
							ShaderVariantCompilationState::kReady :
							ShaderVariantCompilationState::kFailed;
						task.handle->Complete(std::move(output));
						if (completion) {
							try {
								if (state
									== ShaderVariantCompilationState::kFailed) {
									const auto error =
										task.handle->GetError();
									completion(state, error);
								} else {
									completion(state, {});
								}
							} catch (...) {
								// Diagnostic observers must not terminate the worker.
							}
						}
					} else {
						task.handle->Fail(
							"shader compilation invalidated");
					}
				}
			}

			std::mutex _mutex;
			std::condition_variable_any _condition;
			std::uint64_t _generation = 1;
			bool _stopped = false;
			std::deque<CompilationTask> _queue;
			std::unordered_map<
				CompilationKey,
				std::shared_ptr<AsyncShaderVariantCompilationHandle>,
				CompilationKeyHash>
				_entries;
			ShaderVariantCompiler _compiler;
			std::vector<std::jthread> _workers;
		};
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
