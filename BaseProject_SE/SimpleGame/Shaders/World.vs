#version 330 core

// Locations 0-3 support the bounded-cache fallback. Batches fetch immutable geometry from a TBO.
layout(location = 0) in vec2 p;
layout(location = 1) in vec4 c;
layout(location = 2) in vec2 uv;
layout(location = 3) in float radiance;
layout(location = 4) in vec4 instanceTransform;
layout(location = 5) in vec4 instanceTint;
layout(location = 6) in vec4 instanceParameters;
layout(location = 7) in uvec2 instanceRange;
layout(location = 8) in vec2 instanceLocal;

uniform bool useInstancing;
uniform samplerBuffer geometryAtlas;
uniform vec2 meshOffset;
uniform vec2 meshScale = vec2(1);
uniform vec4 meshTint = vec4(1);
uniform vec2 cameraOffset;
uniform float pixelSize;
uniform vec3 cameraBasis;
uniform int objectProjection;
uniform vec2 objectAnchor;
uniform float objectOpacity = 1;
uniform float emissionScale = 1;
uniform float meshRadiance = 1;
uniform vec2 meshLocalOffset = vec2(0);

out vec4 color;
out vec2 texUV;
out float lightEnergy;

void main()
{
    vec2 local = p, offset = meshOffset, scale = meshScale, anchor = objectAnchor;
    vec4 vertexColor = c, tint = meshTint;
    vec2 coordinates = uv;
    float energy = radiance * meshRadiance, opacity = objectOpacity, emission = emissionScale;
    vec2 localOffset = meshLocalOffset;
    int projection = objectProjection;
    if (useInstancing)
    {
        // Counts are triangle-aligned: padded vertices form only clipped, degenerate triangles.
        if (uint(gl_VertexID) >= instanceRange.y)
        {
            gl_Position = vec4(2, 2, 2, 1);
            color = vec4(0);
            texUV = vec2(0);
            lightEnergy = 1;
            return;
        }
        int index = int(instanceRange.x + uint(gl_VertexID)) * 3;
        vec4 a = texelFetch(geometryAtlas, index);
        vec4 b = texelFetch(geometryAtlas, index + 1);
        vec4 d = texelFetch(geometryAtlas, index + 2);
        local = a.xy;
        vertexColor = vec4(a.zw, b.xy);
        coordinates = b.zw;
        offset = instanceTransform.xy;
        scale = instanceTransform.zw;
        anchor = offset;
        tint = instanceTint;
        projection = int(instanceParameters.x);
        opacity = instanceParameters.y;
        emission = instanceParameters.z;
        energy = d.x * instanceParameters.w;
        localOffset = instanceLocal;
    }
    vec2 position = local * scale + localOffset + offset;
    if (pixelSize > 0)
    {
        mat2 ground =
            mat2(cameraBasis.x, cameraBasis.z * cameraBasis.y, -cameraBasis.y, cameraBasis.z * cameraBasis.x);
        if (projection == 0)
        {
            position = ground * (position - cameraOffset);
        }
        else
        {
            position = ground * (anchor - cameraOffset) + (position - anchor);
        }
        position += vec2(500, 350);
        // Keep terrain aligned to its grid without collapsing small model triangles.
        // Actor geometry already contains the projected low-poly silhouette.
        if (projection == 0)
            position = floor(position / pixelSize) * pixelSize;
    }
    gl_Position = vec4(position / vec2(1000, 700) * vec2(2, -2) + vec2(-1, 1), 0, 1);
    color = vertexColor * tint;
    color.a *= clamp(opacity, 0, 1);
    texUV = coordinates;
    lightEnergy = 1 + max(energy - 1, 0) * max(emission, 0);
}
