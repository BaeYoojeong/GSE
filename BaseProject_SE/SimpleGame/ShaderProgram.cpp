#include "stdafx.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "ShaderProgram.h"
#include <iostream>
#include <string>
#include <vector>

namespace
{
    std::wstring ShaderPath(const wchar_t* file)
    {
        std::vector<wchar_t> executable(32768);
        DWORD size = GetModuleFileNameW(NULL, executable.data(), static_cast<DWORD>(executable.size()));

        if (size == 0 || size >= executable.size())
        {
            return {};
        }

        std::wstring path(executable.data(), size);
        auto slash = path.find_last_of(L"\\/");

        if (slash == std::wstring::npos)
        {
            return {};
        }

        return path.substr(0, slash + 1) + L"Shaders\\" + file;
    }

    bool Read(const std::wstring& path, std::string& source)
    {
        if (path.empty())
        {
            std::cerr << "Could not resolve the executable's shader directory.\n";
            return false;
        }

        HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL, NULL);

        if (file == INVALID_HANDLE_VALUE)
        {
            std::wcerr << L"Cannot open shader: " << path << L" (Windows error " << GetLastError() << L")\n";
            return false;
        }

        LARGE_INTEGER size = {};

        if (!GetFileSizeEx(file, &size) || size.QuadPart < 1 || size.QuadPart > 4 * 1024 * 1024)
        {
            CloseHandle(file);
            std::wcerr << L"Invalid shader file size: " << path << L'\n';
            return false;
        }

        source.resize(static_cast<std::size_t>(size.QuadPart));
        DWORD read = 0;
        bool ok = ReadFile(file, &source[0], static_cast<DWORD>(source.size()), &read, NULL) != FALSE &&
                  read == source.size();
        CloseHandle(file);

        if (!ok)
        {
            std::wcerr << L"Cannot read shader: " << path << L'\n';
            return false;
        }

        if (source.size() >= 3 && static_cast<unsigned char>(source[0]) == 0xef &&
            static_cast<unsigned char>(source[1]) == 0xbb && static_cast<unsigned char>(source[2]) == 0xbf)
        {
            source.erase(0, 3);
        }

        return true;
    }

    GLuint Compile(GLenum type, const std::wstring& path)
    {
        std::string source;

        if (!Read(path, source))
        {
            return 0;
        }

        GLuint shader = glCreateShader(type);

        if (!shader)
        {
            std::wcerr << L"Cannot create shader: " << path << L'\n';
            return 0;
        }

        const char* data = source.c_str();
        GLint length = static_cast<GLint>(source.size());
        glShaderSource(shader, 1, &data, &length);
        glCompileShader(shader);
        GLint success = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &success);

        if (!success)
        {
            char log[8192] = {};
            glGetShaderInfoLog(shader, sizeof(log), NULL, log);
            std::wcerr << L"Shader compile failed: " << path << L'\n';
            std::cerr << log << '\n';
            glDeleteShader(shader);
            return 0;
        }

        return shader;
    }
} // namespace

GLuint ShaderProgram::Load(const wchar_t* vertexFile, const wchar_t* fragmentFile)
{
    std::wstring vertexPath = ShaderPath(vertexFile);
    std::wstring fragmentPath = ShaderPath(fragmentFile);
    GLuint vertex = Compile(GL_VERTEX_SHADER, vertexPath);
    GLuint fragment = Compile(GL_FRAGMENT_SHADER, fragmentPath);

    if (!vertex || !fragment)
    {
        if (vertex)
        {
            glDeleteShader(vertex);
        }

        if (fragment)
        {
            glDeleteShader(fragment);
        }

        return 0;
    }

    GLuint program = glCreateProgram();

    if (program)
    {
        glAttachShader(program, vertex);
        glAttachShader(program, fragment);
        glLinkProgram(program);
        glDetachShader(program, vertex);
        glDetachShader(program, fragment);
    }

    glDeleteShader(vertex);
    glDeleteShader(fragment);

    if (!program)
    {
        return 0;
    }

    GLint success = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &success);

    if (!success)
    {
        char log[8192] = {};
        glGetProgramInfoLog(program, sizeof(log), NULL, log);
        std::wcerr << L"Shader link failed: " << vertexPath << L" + " << fragmentPath << L'\n';
        std::cerr << log << '\n';
        glDeleteProgram(program);
        return 0;
    }

    return program;
}
