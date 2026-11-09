#include "stdafx.h"
#include "RenderProfiler.h"
#include "Dependencies/glew.h"
#include "PostProcessing.h"
#include "ShaderProgram.h"
#include <algorithm>
#include <iostream>

namespace
{
    constexpr int BloomLevels = 4;

    struct Target
    {
        GLuint framebuffer = 0, texture = 0, emissionTexture = 0;
        int width = 0, height = 0;
    };

    struct Buffers
    {
        Target scene, bloom[BloomLevels][2], soft[2];
    };

    Buffers buffers;
    PostProcessing::Settings settings;
    GLuint extractProgram = 0, downsampleProgram = 0, blurProgram = 0, compositeProgram = 0;
    GLuint fullscreenVao = 0;

    struct ExtractUniforms
    {
        GLint threshold, knee;
    } extractUniforms;

    struct BlurUniforms
    {
        GLint direction;
    } blurUniforms;

    struct CompositeUniforms
    {
        GLint enabled, exposure, bloomStrength, vignetteStrength, vignetteRange, edgeStrength, edgeRange,
            fade;
    } compositeUniforms;

    float Clamp(float value, float lo, float hi)
    {
        return (std::max)(lo, (std::min)(hi, value));
    }

    void Delete(Target& t)
    {
        if (t.framebuffer)
        {
            glDeleteFramebuffers(1, &t.framebuffer);
        }

        if (t.texture)
        {
            glDeleteTextures(1, &t.texture);
        }

        if (t.emissionTexture)
        {
            glDeleteTextures(1, &t.emissionTexture);
        }

        t = Target();
    }

    void Delete(Buffers& b)
    {
        Delete(b.scene);

        for (int level = 0; level < BloomLevels; ++level)
        {
            Delete(b.bloom[level][0]);
            Delete(b.bloom[level][1]);
        }

        for (int i = 0; i < 2; ++i)
        {
            Delete(b.soft[i]);
        }
    }

    bool Allocate(Target& t, int width, int height, bool withEmission = false)
    {
        t.width = width;
        t.height = height;
        glGenTextures(1, &t.texture);
        glBindTexture(GL_TEXTURE_2D, t.texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, NULL);
        glGenFramebuffers(1, &t.framebuffer);
        glBindFramebuffer(GL_FRAMEBUFFER, t.framebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t.texture, 0);
        const GLenum attachments[] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};

        if (withEmission)
        {
            glGenTextures(1, &t.emissionTexture);
            glBindTexture(GL_TEXTURE_2D, t.emissionTexture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, NULL);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, t.emissionTexture, 0);
        }

        glDrawBuffers(withEmission ? 2 : 1, attachments);
        return t.texture && t.framebuffer &&
               glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    }

    void BindTexture(GLuint texture, int unit)
    {
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_2D, texture);
    }

    void Destination(const Target& target)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, target.framebuffer);
        glViewport(0, 0, target.width, target.height);
    }

    void Blur(const Target& source, const Target& destination, float dx, float dy)
    {
        Destination(destination);
        glUseProgram(blurProgram);
        BindTexture(source.texture, 0);
        glUniform2f(blurUniforms.direction, dx / source.width, dy / source.height);
        RenderProfiler::DrawArrays(GL_TRIANGLES, 0, 3);
    }
} // namespace

PostProcessing::Settings& PostProcessing::Options()
{
    return settings;
}

void PostProcessing::Reset()
{
    settings = Settings();
}

bool PostProcessing::Initialize()
{
    extractProgram = ShaderProgram::Load(L"Fullscreen.vs", L"BloomExtract.fs");
    downsampleProgram = ShaderProgram::Load(L"Fullscreen.vs", L"BloomDownsample.fs");
    blurProgram = ShaderProgram::Load(L"Fullscreen.vs", L"GaussianBlur.fs");
    compositeProgram = ShaderProgram::Load(L"Fullscreen.vs", L"Composite.fs");

    if (!extractProgram || !downsampleProgram || !blurProgram || !compositeProgram)
    {
        Shutdown();
        return false;
    }

    glGenVertexArrays(1, &fullscreenVao);

    if (!fullscreenVao)
    {
        Shutdown();
        return false;
    }

    glUseProgram(extractProgram);
    glUniform1i(glGetUniformLocation(extractProgram, "source"), 0);
    extractUniforms = {glGetUniformLocation(extractProgram, "threshold"),
                       glGetUniformLocation(extractProgram, "knee")};
    glUseProgram(downsampleProgram);
    glUniform1i(glGetUniformLocation(downsampleProgram, "source"), 0);
    glUseProgram(blurProgram);
    glUniform1i(glGetUniformLocation(blurProgram, "source"), 0);
    blurUniforms.direction = glGetUniformLocation(blurProgram, "direction");
    glUseProgram(compositeProgram);
    glUniform1i(glGetUniformLocation(compositeProgram, "sceneImage"), 0);
    const GLint bloomUnits[] = {1, 2, 3, 4};
    glUniform1iv(glGetUniformLocation(compositeProgram, "bloomImages[0]"), BloomLevels, bloomUnits);
    glUniform1i(glGetUniformLocation(compositeProgram, "softImage"), 5);
    compositeUniforms = {glGetUniformLocation(compositeProgram, "enabled"),
                         glGetUniformLocation(compositeProgram, "exposure"),
                         glGetUniformLocation(compositeProgram, "bloomStrength"),
                         glGetUniformLocation(compositeProgram, "vignetteStrength"),
                         glGetUniformLocation(compositeProgram, "vignetteRange"),
                         glGetUniformLocation(compositeProgram, "edgeStrength"),
                         glGetUniformLocation(compositeProgram, "edgeRange"),
                         glGetUniformLocation(compositeProgram, "fade")};
    glUseProgram(0);
    return true;
}

bool PostProcessing::Resize(int width, int height)
{
    if (!compositeProgram || width < 1 || height < 1)
    {
        return false;
    }

    GLint maxSize = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxSize);

    if (width > maxSize || height > maxSize)
    {
        std::cerr << "Post-processing size exceeds GL_MAX_TEXTURE_SIZE; retaining previous buffers.\n";
        return false;
    }

    if (buffers.scene.width == width && buffers.scene.height == height)
    {
        return true;
    }

    Buffers replacement;
    glActiveTexture(GL_TEXTURE0);
    const int halfW = (std::max)(1, (width + 1) / 2), halfH = (std::max)(1, (height + 1) / 2);
    bool ok = Allocate(replacement.scene, width, height, true);
    int levelWidth = halfW, levelHeight = halfH;

    for (int level = 0; level < BloomLevels && ok; ++level)
    {
        ok = Allocate(replacement.bloom[level][0], levelWidth, levelHeight) &&
             Allocate(replacement.bloom[level][1], levelWidth, levelHeight);
        levelWidth = (std::max)(1, (levelWidth + 1) / 2);
        levelHeight = (std::max)(1, (levelHeight + 1) / 2);
    }

    for (int i = 0; i < 2 && ok; ++i)
    {
        ok = Allocate(replacement.soft[i], halfW, halfH);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);

    if (!ok)
    {
        Delete(replacement);
        std::cerr << "Could not allocate HDR framebuffers; retaining previous buffers or direct rendering.\n";
        return false;
    }

    Delete(buffers);
    buffers = replacement;
    return true;
}

bool PostProcessing::Available()
{
    return compositeProgram && buffers.scene.framebuffer;
}

bool PostProcessing::BeginScene()
{
    if (!Available())
    {
        return false;
    }

    Destination(buffers.scene);
    glDisable(GL_FRAMEBUFFER_SRGB);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    return true;
}

void PostProcessing::EndScene(int x, int y, int width, int height, float fade)
{
    if (!Available())
    {
        return;
    }

    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_FRAMEBUFFER_SRGB);
    glBindVertexArray(fullscreenVao);

    if (settings.enabled && settings.bloom)
    {
        Destination(buffers.bloom[0][0]);
        glUseProgram(extractProgram);
        BindTexture(buffers.scene.emissionTexture, 0);
        glUniform1f(extractUniforms.threshold, Clamp(settings.bloomThreshold, 0, 20));
        glUniform1f(extractUniforms.knee, Clamp(settings.bloomKnee, .001f, 5));
        RenderProfiler::DrawArrays(GL_TRIANGLES, 0, 3);
        const float radius = Clamp(settings.bloomRadius, .25f, 4);

        for (int level = 0; level < BloomLevels; ++level)
        {
            if (level > 0)
            {
                Destination(buffers.bloom[level][0]);
                glUseProgram(downsampleProgram);
                BindTexture(buffers.bloom[level - 1][0].texture, 0);
                RenderProfiler::DrawArrays(GL_TRIANGLES, 0, 3);
            }

            // Each level spreads the same visible emission over a progressively larger screen area.
            // Ping-pong targets keep every draw independent of its texture input.
            Blur(buffers.bloom[level][0], buffers.bloom[level][1], radius, 0);
            Blur(buffers.bloom[level][1], buffers.bloom[level][0], 0, radius);
        }
    }

    if (settings.enabled && settings.edgeBlur)
    {
        const float radius = Clamp(settings.edgeBlurRadius, 0, 12);
        // First pass downsamples from full resolution, hence twice the source-pixel step.
        Blur(buffers.scene, buffers.soft[0], radius * 2, 0);
        Blur(buffers.soft[0], buffers.soft[1], 0, radius);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(x, y, width, height);
    glUseProgram(compositeProgram);
    BindTexture(buffers.scene.texture, 0);

    for (int level = 0; level < BloomLevels; ++level)
    {
        BindTexture(settings.enabled && settings.bloom ? buffers.bloom[level][0].texture
                                                       : buffers.scene.texture,
                    level + 1);
    }

    BindTexture(settings.enabled && settings.edgeBlur ? buffers.soft[1].texture : buffers.scene.texture, 5);
    glUniform1i(compositeUniforms.enabled, settings.enabled ? 1 : 0);
    glUniform1f(compositeUniforms.exposure, Clamp(settings.exposure, .25f, 4));
    glUniform1f(compositeUniforms.bloomStrength, settings.bloom ? Clamp(settings.bloomStrength, 0, 3) : 0);
    glUniform1f(compositeUniforms.vignetteStrength,
                settings.vignette ? Clamp(settings.vignetteStrength, 0, .9f) : 0);
    const float vignetteStart = Clamp(settings.vignetteStart, 0, 1.4f);
    glUniform2f(compositeUniforms.vignetteRange, vignetteStart,
                Clamp(settings.vignetteEnd, vignetteStart + .01f, 2));
    glUniform1f(compositeUniforms.edgeStrength,
                settings.edgeBlur ? Clamp(settings.edgeBlurStrength, 0, 1) : 0);
    const float start = Clamp(settings.edgeStart, 0, 1.4f);
    glUniform2f(compositeUniforms.edgeRange, start, Clamp(settings.edgeEnd, start + .01f, 2));
    glUniform1f(compositeUniforms.fade, Clamp(fade, 0, 1));
    RenderProfiler::DrawArrays(GL_TRIANGLES, 0, 3);

    for (int i = 5; i >= 0; --i)
    {
        BindTexture(0, i);
    }

    glBindVertexArray(0);
    glUseProgram(0);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void PostProcessing::Shutdown()
{
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    Delete(buffers);

    if (fullscreenVao)
    {
        glDeleteVertexArrays(1, &fullscreenVao);
    }

    if (extractProgram)
    {
        glDeleteProgram(extractProgram);
    }

    if (blurProgram)
    {
        glDeleteProgram(blurProgram);
    }

    if (downsampleProgram)
    {
        glDeleteProgram(downsampleProgram);
    }

    if (compositeProgram)
    {
        glDeleteProgram(compositeProgram);
    }

    fullscreenVao = extractProgram = downsampleProgram = blurProgram = compositeProgram = 0;
}
