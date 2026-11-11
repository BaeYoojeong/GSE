#pragma once

namespace PostProcessing
{
    struct Settings
    {
        bool enabled = true;
        bool bloom = true;
        bool vignette = true;
        bool edgeBlur = false; // Keep pixel silhouettes crisp by default; F4 enables a subtle corner blur.
        float exposure = 1.00f;
        float bloomStrength = 0.32f;
        float bloomThreshold = 0.90f; // Applied only to linear HDR emission, before downsampling.
        float bloomKnee = 0.20f;
        float bloomRadius = 0.85f; // Blur step in each pyramid level's pixels.
        float vignetteStrength = 0.22f;
        float vignetteStart = 0.70f;
        float vignetteEnd = 1.38f;
        float edgeBlurStrength = 0.20f;
        float edgeStart = 0.98f;
        float edgeEnd = 1.42f;
        float edgeBlurRadius = 0.75f; // In half-resolution pixels.
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
} // namespace PostProcessing
