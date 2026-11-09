/*
Copyright 2022 Lee Taek Hee (Tech University of Korea)

This program is free software: you can redistribute it and/or modify
it under the terms of the What The Hell License. Do it plz.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY.
*/

#include "stdafx.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <iostream>
#include "Dependencies/glew.h"
#include "Dependencies/freeglut.h"
#include "WorldScene.h"
#include "RenderProfiler.h"
#include "AcceptanceChecks.h"
#include <string>

namespace
{
    int lastUpdate = 0;
    HWND gameWindow = NULL;

    void RenderScene()
    {
        RenderProfiler::BeginFrame();
        WorldScene::Render();
        glutSwapBuffers();
        RenderProfiler::EndFrame();
    }

    void Idle()
    {
        const int now = glutGet(GLUT_ELAPSED_TIME);

        if (GetForegroundWindow() != gameWindow)
        {
            WorldScene::ClearInput();
            // Keep the observed world alive while the local monitor has keyboard focus.
            if (now - lastUpdate >= 20)
            {
                WorldScene::Update((now - lastUpdate) * 0.001f);
                lastUpdate = now;
            }
            Sleep(5);
            return;
        }

        WorldScene::Update((now - lastUpdate) * 0.001f);
        lastUpdate = now;
        glutPostRedisplay();
    }

    void KeyDown(unsigned char key, int, int)
    {
        WorldScene::Key(key, true);
    }

    void KeyUp(unsigned char key, int, int)
    {
        WorldScene::Key(key, false);
    }

    void ArrowDown(int key, int, int)
    {
        WorldScene::Arrow(key, true);
    }

    void ArrowUp(int key, int, int)
    {
        WorldScene::Arrow(key, false);
    }
} // namespace

int main(int argc, char** argv)
{
    if (argc == 3 && std::string(argv[1]) == "--self-test")
    {
        int wideCount = 0;
        auto wideArgs = CommandLineToArgvW(GetCommandLineW(), &wideCount);
        if (!wideArgs || wideCount != 3)
        {
            if (wideArgs)
            {
                LocalFree(wideArgs);
            }
            return 2;
        }
        const std::filesystem::path directory(wideArgs[2]);
        LocalFree(wideArgs);
        try
        {
            return Ashen::AcceptanceChecks::Run(directory);
        }
        catch (const std::exception& error)
        {
            std::cerr << "Could not start isolated acceptance checks: " << error.what() << '\n';
            return 2;
        }
    }
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA);
    glutInitWindowSize(1120, 784);
    glutInitWindowPosition(80, 40);
    const int window = glutCreateWindow("Ashen Inheritance - Living World");

    if (window <= 0)
    {
        return 1;
    }

    gameWindow = GetActiveWindow();

    glewExperimental = GL_TRUE;
    const GLenum glewStatus = glewInit();

    if (glewStatus != GLEW_OK || !GLEW_VERSION_3_3)
    {
        std::cerr << "OpenGL 3.3 is required for Ashen Inheritance." << std::endl;
        glutDestroyWindow(window);
        return 1;
    }

    if (!WorldScene::Initialize())
    {
        std::cerr << "Could not initialize the world renderer." << std::endl;
        WorldScene::Shutdown();
        glutDestroyWindow(window);
        return 1;
    }

    glutSetOption(GLUT_ACTION_ON_WINDOW_CLOSE, GLUT_ACTION_GLUTMAINLOOP_RETURNS);
    glutDisplayFunc(RenderScene);
    glutReshapeFunc(WorldScene::Resize);
    glutIdleFunc(Idle);
    glutKeyboardFunc(KeyDown);
    glutKeyboardUpFunc(KeyUp);
    glutSpecialFunc(ArrowDown);
    glutSpecialUpFunc(ArrowUp);
    glutCloseFunc(WorldScene::Shutdown);
    glutIgnoreKeyRepeat(1);
    WorldScene::Resize(1120, 784);
    lastUpdate = glutGet(GLUT_ELAPSED_TIME);
    std::cout << "Ashen Inheritance | Arrows: move | C: run | J: jump | E: interact | Space: attack\n"
                 "Dialogue 1/2/3: hope/fear/anger | Tab: life journal | R: next life after death | F8: save\n"
                 "P: PK toggle | B/N: buy/sell | V: live residents | [/]: resident pages\n"
                 "F9: reload assets | F10: export Blender snapshot in background\n"
                 "Save: %LOCALAPPDATA%\\AshenInheritance\\World_v1.txt.sqlite3 (legacy text is retained)\n";
    glutMainLoop();
    return 0;
}
