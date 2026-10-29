#pragma once

#include <cstdint>

namespace cs::engine
{
	// Pending requests consumed by the sky update hook.
	struct CelestialTransitionRequest
	{
		bool timeJump = false;
		bool gameLoad = false;
	};

	// Without a handler, requests complete at once.
	void SetCelestialTransitionHandlerAvailable(bool a_available);
	// Requests celestial synchronization after an abrupt game-time change.
	void RequestTimeJumpTransition();
	// Requests celestial synchronization after loading an existing save.
	void RequestGameLoadTransition();
	[[nodiscard]] CelestialTransitionRequest ConsumeCelestialTransitionRequest();
	// Marks a celestial transition ready for dependent rendering updates.
	void CompleteCelestialTransition();
	// The latest completed celestial-transition generation.
	[[nodiscard]] std::uint32_t GetCompletedCelestialTransitionGeneration();

	// Registers the event sources once; every consumer may call it.
	void RegisterCelestialTransitionSources();
}
