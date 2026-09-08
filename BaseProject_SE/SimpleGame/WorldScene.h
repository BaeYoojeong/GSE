#pragma once

// Two connected prototype areas. Coordinates use a 1000 x 700 logical canvas.
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
