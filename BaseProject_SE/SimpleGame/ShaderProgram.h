#pragma once

#include "Dependencies/glew.h"

namespace ShaderProgram
{
    // Files are resolved relative to the executable's Shaders directory, independent of the CWD.
    GLuint Load(const wchar_t* vertexFile, const wchar_t* fragmentFile);
} // namespace ShaderProgram
