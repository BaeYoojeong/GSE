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
#include <iostream>
#include "Dependencies/glew.h"
#include "Dependencies/freeglut.h"
#include "WorldScene.h"

namespace
{
    int lastUpdate = 0;
    HWND gameWindow = NULL;

    void RenderScene()
    {
        WorldScene::Render();
        glutSwapBuffers();
    }

    void Idle()
    {
        const int now = glutGet(GLUT_ELAPSED_TIME);
        if (GetForegroundWindow() != gameWindow)
            WorldScene::ClearInput();
        WorldScene::Update((now - lastUpdate) * 0.001f);
        lastUpdate = now;
        glutPostRedisplay();
    }

    void KeyDown(unsigned char key, int, int) { WorldScene::Key(key, true); }
    void KeyUp(unsigned char key, int, int) { WorldScene::Key(key, false); }
    void ArrowDown(int key, int, int) { WorldScene::Arrow(key, true); }
    void ArrowUp(int key, int, int) { WorldScene::Arrow(key, false); }
}

int main(int argc, char** argv)
{
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA);
    glutInitWindowSize(1120, 784);
    glutInitWindowPosition(80, 40);
    const int window = glutCreateWindow("Moonlit Herbalist - Village & Palace");
    if (window <= 0)
        return 1;
    gameWindow = GetActiveWindow();

    glewExperimental = GL_TRUE;
    const GLenum glewStatus = glewInit();
    if (glewStatus != GLEW_OK || !GLEW_VERSION_3_3)
    {
        std::cerr << "OpenGL 3.3 is required for the village and palace scene." << std::endl;
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
    std::cout << "Arrow keys: move | E: interact / close dialogue | ESC: close dialogue\n";
    glutMainLoop();
    return 0;
}

