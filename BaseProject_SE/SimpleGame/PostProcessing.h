#pragma once

namespace PostProcessing
{
	struct Settings
	{
		bool enabled = true;
		bool bloom = true;
		bool vignette = true;
		bool edgeBlur = true;
		float exposure = 1.05f;
		float bloomStrength = 0.28f;
		float bloomThreshold = 0.65f; // Threshold applied to the emission attachment only.
		float bloomKnee = 0.20f;
		float vignetteStrength = 0.23f;
		float edgeBlurStrength = 0.35f;
		float edgeStart = 0.75f;
		float edgeEnd = 1.40f;
		float edgeBlurRadius = 1.0f; // In half-resolution pixels.
	};

	Settings& Options();
	void Reset();
	bool Initialize();
	bool Resize(int width, int height);
	bool Available();
	bool BeginScene();
	// Composites to the default framebuffer; UI must be drawn afterwards.
	void EndScene(int viewportX, int viewportY, int viewportWidth, int viewportHeight, float fade);
	void Shutdown();
}
