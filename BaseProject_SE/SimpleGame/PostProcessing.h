#pragma once

namespace PostProcessing
{
    struct Settings
    {
        bool enabled = true;
        bool bloom = true;
        bool vignette = true;
        bool edgeBlur = true;
        float exposure = 1.12f;
        float bloomStrength = 0.80f;
        float bloomThreshold = 0.45f; // Applied only to linear HDR emission, before downsampling.
        float bloomKnee = 0.30f;
        float bloomRadius = 1.25f; // Blur step in each pyramid level's pixels.
        float vignetteStrength = 0.30f;
        float vignetteStart = 0.35f;
        float vignetteEnd = 1.38f;
        float edgeBlurStrength = 0.65f;
        float edgeStart = 0.65f;
        float edgeEnd = 1.18f;
        float edgeBlurRadius = 1.6f; // In half-resolution pixels.
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
