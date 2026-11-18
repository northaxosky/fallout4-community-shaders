#pragma once

namespace cs
{
	class Feature;
}

namespace cs::features::true_pbr
{
	// Load only; writes each PBR draw's record id into the prepass lane.
	[[nodiscard]] bool RegisterPrepassClassifier(Feature& a_owner);
}
