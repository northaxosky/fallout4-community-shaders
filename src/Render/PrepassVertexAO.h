#pragma once

namespace cs::engine
{
	struct ShaderReplacementRegistration;

	// Masks hair alpha in a vertex-AO feature's kDeferredPrepass registration.
	void AttachPrepassVertexAO(ShaderReplacementRegistration& a_registration);
}
