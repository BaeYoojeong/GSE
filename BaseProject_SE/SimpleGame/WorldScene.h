#pragma once

// Continuous 6000 x 4200 world viewed through a pixel-snapped 1000 x 700 camera.
namespace WorldScene
{
	bool Initialize();
	void Shutdown();
	void Render();
	void Update(float seconds);
	void Resize(int width, int height);
	void Arrow(int key, bool down);
	void Key(unsigned char key, bool down);
	void ClearInput();
}
