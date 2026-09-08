#include "stdafx.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "Dependencies/glew.h"
#include "Dependencies/freeglut.h"
#include "WorldScene.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace
{
	const float Pi = 3.14159265f;
	struct Color { float r, g, b, a; };
	Color Ink = { .08f, .13f, .18f, 1 }, Gold = { .91f, .73f, .40f, 1 };
	Color Cream = { .97f, .92f, .78f, 1 }, Wood = { .30f, .16f, .16f, 1 };
	struct Vertex { float x, y, r, g, b, a, u, v; };
	struct Texture { GLuint id; int width, height; };
	struct Label { float x, y; std::wstring text; int size; Color color; };
	struct Obstacle { float x, y, w, h; };
	struct Target { float x, y; int type, index; const wchar_t* name; };
	enum { Gate, Merchant, Notice, Herb, Physician };
	enum class Area { Village, Palace };
	Area area = Area::Village;
	float playerX = 500, playerY = 540, walk = 0, elapsed = 0, transition = 0;
	bool arrows[4] = {}, eDown = false, moving = false;
	int facing = GLUT_KEY_DOWN;
	bool picked[3] = {}, requested = false, delivered = false;
	int herbs = 0;
	std::wstring speaker, message1, message2;
	GLuint program = 0, vao = 0, vbo = 0;
	GLint textureMode = -1, fadeLocation = -1;
	std::vector<Vertex> vertices;
	std::vector<Label> labels;
	std::map<std::pair<std::wstring, int>, Texture> textCache;

	float Clamp(float v, float lo, float hi) { return (std::max)(lo, (std::min)(hi, v)); }
	Color Alpha(Color c, float a) { c.a = a; return c; }
	Vertex V(float x, float y, Color c, float u = 0, float v = 0)
	{
		return { x, y, c.r, c.g, c.b, c.a, u, v };
	}
	void Tri(float x, float y, float x2, float y2, float x3, float y3, Color c)
	{
		vertices.push_back(V(x,y,c)); vertices.push_back(V(x2,y2,c)); vertices.push_back(V(x3,y3,c));
	}
	void Rect(float x, float y, float w, float h, Color c)
	{
		Tri(x,y,x+w,y,x+w,y+h,c); Tri(x,y,x+w,y+h,x,y+h,c);
	}
	void Gradient(float x, float y, float w, float h, Color top, Color bottom)
	{
		vertices.push_back(V(x,y,top)); vertices.push_back(V(x+w,y,top)); vertices.push_back(V(x+w,y+h,bottom));
		vertices.push_back(V(x,y,top)); vertices.push_back(V(x+w,y+h,bottom)); vertices.push_back(V(x,y+h,bottom));
	}
	void Ellipse(float x, float y, float rx, float ry, Color c)
	{
		for (int i = 0; i < 24; ++i)
		{
			const float a = i * 2 * Pi / 24, b = (i+1) * 2 * Pi / 24;
			Tri(x,y,x+std::cos(a)*rx,y+std::sin(a)*ry,x+std::cos(b)*rx,y+std::sin(b)*ry,c);
		}
	}
	void Line(float x, float y, float x2, float y2, float width, Color c)
	{
		const float len = std::sqrt((x2-x)*(x2-x)+(y2-y)*(y2-y));
		if (len < .01f) return;
		const float nx = -(y2-y)/len*width*.5f, ny = (x2-x)/len*width*.5f;
		Tri(x+nx,y+ny,x2+nx,y2+ny,x2-nx,y2-ny,c);
		Tri(x+nx,y+ny,x2-nx,y2-ny,x-nx,y-ny,c);
	}
	void Ring(float x, float y, float radius, Color c)
	{
		for (int i=0; i<32; ++i)
		{
			float a=i*2*Pi/32, b=(i+1)*2*Pi/32;
			Line(x+std::cos(a)*radius,y+std::sin(a)*radius*.42f,
				x+std::cos(b)*radius,y+std::sin(b)*radius*.42f,2,c);
		}
	}
	void Glow(float x, float y, float radius, Color c)
	{
		for (int i=6; i>=1; --i) Ellipse(x,y,radius*i/6,radius*i/6,Alpha(c,.025f));
	}
	void Text(float x, float y, const std::wstring& s, int size = 18, Color c = Cream)
	{
		labels.push_back({x,y,s,size,c});
	}

	// Rasterize Korean UI into cached textures. No downloaded font or bitmap asset required.
	Texture MakeText(const std::wstring& s, int size)
	{
		Texture result = { 0,0,0 };
		HDC dc = CreateCompatibleDC(NULL);
		if (!dc) return result;
		HFONT font = CreateFontW(-size,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
			OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Malgun Gothic");
		if (!font) { DeleteDC(dc); return result; }
		HGDIOBJ oldFont = SelectObject(dc,font);
		SIZE extent = {};
		GetTextExtentPoint32W(dc,s.c_str(),static_cast<int>(s.size()),&extent);
		result.width = (std::max)(1, static_cast<int>(extent.cx)+4);
		result.height = (std::max)(size+6, static_cast<int>(extent.cy)+4);
		BITMAPINFO info = {};
		info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		info.bmiHeader.biWidth = result.width;
		info.bmiHeader.biHeight = -result.height;
		info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
		info.bmiHeader.biCompression = BI_RGB;
		void* pixels = NULL;
		HBITMAP bitmap = CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,NULL,0);
		if (bitmap)
		{
			HGDIOBJ oldBitmap = SelectObject(dc,bitmap);
			PatBlt(dc,0,0,result.width,result.height,BLACKNESS);
			SetBkMode(dc,TRANSPARENT); SetTextColor(dc,RGB(255,255,255));
			TextOutW(dc,2,0,s.c_str(),static_cast<int>(s.size()));
			GdiFlush();
			glGenTextures(1,&result.id); glBindTexture(GL_TEXTURE_2D,result.id);
			glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
			glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,result.width,result.height,0,GL_BGRA,GL_UNSIGNED_BYTE,pixels);
			SelectObject(dc,oldBitmap); DeleteObject(bitmap);
		}
		SelectObject(dc,oldFont); DeleteObject(font); DeleteDC(dc);
		return result;
	}
	void Flush()
	{
		glUseProgram(program); glBindVertexArray(vao); glBindBuffer(GL_ARRAY_BUFFER,vbo);
		glUniform1f(fadeLocation,1.0f-Clamp(transition/.38f,0,1));
		glUniform1i(textureMode,0);
		glBufferData(GL_ARRAY_BUFFER,vertices.size()*sizeof(Vertex),vertices.data(),GL_STREAM_DRAW);
		glDrawArrays(GL_TRIANGLES,0,static_cast<GLsizei>(vertices.size()));
		glUniform1i(textureMode,1); glActiveTexture(GL_TEXTURE0);
		for (const Label& label : labels)
		{
			auto key = std::make_pair(label.text,label.size);
			auto found = textCache.find(key);
			if (found == textCache.end()) found = textCache.emplace(key,MakeText(label.text,label.size)).first;
			const Texture& t = found->second;
			if (!t.id) continue;
			float x=label.x, y=label.y, w=static_cast<float>(t.width), h=static_cast<float>(t.height);
			Vertex quad[] = {V(x,y,label.color,0,0),V(x+w,y,label.color,1,0),V(x+w,y+h,label.color,1,1),
				V(x,y,label.color,0,0),V(x+w,y+h,label.color,1,1),V(x,y+h,label.color,0,1)};
			glBindTexture(GL_TEXTURE_2D,t.id);
			glBufferData(GL_ARRAY_BUFFER,sizeof(quad),quad,GL_STREAM_DRAW);
			glDrawArrays(GL_TRIANGLES,0,6);
		}
		glBindVertexArray(0); glUseProgram(0);
	}

	std::vector<Obstacle> Obstacles()
	{
		if (area == Area::Village) return {{70,255,225,98},{705,255,225,98},{105,391,120,48}};
		return {{285,235,430,102},{92,368,192,120},{716,368,192,120}};
	}
	bool CanStand(float x, float y)
	{
		for (const Obstacle& o : Obstacles())
			if (x>o.x-13 && x<o.x+o.w+13 && y>o.y-8 && y<o.y+o.h+8) return false;
		return true;
	}
	std::vector<Target> Targets()
	{
		if (area == Area::Village)
		{
			std::vector<Target> targets = {{500,268,Gate,0,L"궁중으로 이동"},{270,449,Merchant,0,L"약재상과 대화"},
				{650,390,Notice,0,L"마을 게시판 읽기"}};
			const float hx[] = {760,825,860}, hy[] = {526,565,508};
			for (int i=0;i<3;++i) if (!picked[i]) targets.push_back({hx[i],hy[i],Herb,i,L"달빛 약초 채집"});
			return targets;
		}
		return {{500,600,Gate,0,L"마을로 돌아가기"},{430,429,Physician,0,L"궁중 의원과 대화"}};
	}
	int Nearest(const std::vector<Target>& targets)
	{
		int found=-1; float best=65*65;
		for (size_t i=0;i<targets.size();++i)
		{
			float dx=targets[i].x-playerX, dy=targets[i].y-playerY, distance=dx*dx+dy*dy;
			if (distance<best) { best=distance; found=static_cast<int>(i); }
		}
		return found;
	}
	void Speak(const wchar_t* who, const wchar_t* first, const wchar_t* second)
	{
		speaker=who; message1=first; message2=second;
		WorldScene::ClearInput();
	}
	void Interact()
	{
		if (transition>0) return;
		if (!speaker.empty()) { speaker.clear(); WorldScene::ClearInput(); return; }
		auto targets=Targets(); int id=Nearest(targets);
		if (id<0) return;
		Target t=targets[id];
		switch(t.type)
		{
		case Gate:
			area = area==Area::Village ? Area::Palace : Area::Village;
			playerX=500; playerY=area==Area::Palace ? 545.0f : 335.0f;
			facing=area==Area::Palace ? GLUT_KEY_UP : GLUT_KEY_DOWN;
			transition=.38f; WorldScene::ClearInput();
			break;
		case Merchant:
			requested=true;
			if (delivered) Speak(L"약재상 · 연화",L"의원께서 고맙다는 말을 전해 달라 하셨네.",L"사람을 살리는 약사가 되어 주게. 언제든 쉬었다 가도 좋아.");
			else Speak(L"약재상 · 연화",L"궁중에서 쓸 달빛 약초 세 포기가 필요하다네.",L"마을 오른쪽 아래 화단에서 채집해서 궁중 의원에게 전해 주겠나?");
			break;
		case Notice:
			Speak(L"마을 게시판",L"북쪽 붉은 문은 궁중으로 이어집니다. 문 가까이에서 E를 누르세요.",L"약재상은 왼쪽 시장에, 달빛 약초는 오른쪽 아래 화단에 있습니다.");
			break;
		case Herb:
			picked[t.index]=true; ++herbs;
			Speak(L"약초 채집",L"은은한 빛이 맺힌 달빛 약초를 약재 가방에 담았습니다.",L"세 포기를 모으면 궁중 정원의 의원에게 전달할 수 있습니다.");
			break;
		case Physician:
			requested=true;
			if (delivered) Speak(L"궁중 의원 · 서윤",L"덕분에 필요한 약재를 마련했습니다. 수고하셨습니다.",L"다음에는 환자의 증상을 함께 살펴보도록 하지요.");
			else if (herbs>=3)
			{
				herbs-=3; delivered=true;
				Speak(L"궁중 의원 · 서윤",L"달빛 약초 세 포기, 잘 받았습니다. 약사의 첫 심부름을 마쳤군요.",L"의뢰 완료 · 궁중 신뢰 +1. 정원을 둘러보고 마을로 돌아가도 좋습니다.");
			}
			else Speak(L"궁중 의원 · 서윤",L"마을의 달빛 약초 세 포기가 필요합니다. 아직 약재가 부족하군요.",L"남쪽 문으로 돌아가 화단을 살펴보세요. 약재상에게 길을 물어도 됩니다.");
			break;
		}
	}

	void Roof(float x, float y, float w, Color c)
	{
		// Swept eaves, pitched center and fine rows of gold-edged tiles.
		Tri(x-18,y+35,x+w*.5f,y-28,x+w+18,y+35,c);
		Tri(x-18,y+35,x-29,y+22,x+30,y+33,c);
		Tri(x+w+18,y+35,x+w+29,y+22,x+w-30,y+33,c);
		Rect(x-18,y+33,w+36,7,Wood);
		Line(x-22,y+33,x+w+22,y+33,2,Gold);
		for(int i=1;i<12;++i)
		{
			float tx=x+w*i/12;
			Line(x+w*.5f+(tx-x-w*.5f)*.35f,y-6,tx,y+30,1,Alpha(Cream,.20f));
		}
		Line(x+w*.5f-20,y-28,x+w*.5f+20,y-28,4,Gold);
	}
	void Lantern(float x, float y)
	{
		float sway=std::sin(elapsed*1.5f+x)*2;
		Line(x,y-42,x+sway,y-23,2,Gold);
		Glow(x+sway,y-12,35+std::sin(elapsed*2+x)*3,Gold);
		Ellipse(x+sway,y-12,10,14,{.85f,.24f,.19f,1});
		Ellipse(x+sway,y-12,6,11,{1,.64f,.27f,1});
		Rect(x+sway-9,y-26,18,3,Gold); Rect(x+sway-9,y,18,3,Gold);
		Line(x+sway,y+2,x+sway,y+12,2,Gold);
	}
	void House(float x, float y, float w, bool jade)
	{
		Ellipse(x+w*.5f,y+101,w*.56f,16,Alpha(Ink,.2f));
		Rect(x,y,w,100,{.77f,.59f,.40f,1});
		Rect(x,y+87,w,13,{.49f,.35f,.29f,1});
		for(float px=x+10;px<x+w;px+=48) Rect(px,y,6,88,Wood);
		for(float px=x+25;px<x+w-20;px+=64)
		{
			Rect(px,y+23,34,38,Wood); Rect(px+3,y+26,28,32,{.95f,.70f,.33f,1});
			Line(px+17,y+26,px+17,y+58,2,Wood); Line(px+3,y+42,px+31,y+42,2,Wood);
		}
		Rect(x+w*.5f-18,y+47,36,42,Wood);
		Roof(x-7,y-5,w+14,jade ? Color{.18f,.38f,.39f,1} : Color{.37f,.25f,.34f,1});
		Lantern(x+15,y+45); Lantern(x+w-15,y+45);
	}
	void Tree(float x, float y, float scale, bool pink)
	{
		Ellipse(x,y,42*scale,12*scale,Alpha(Ink,.18f));
		Line(x,y,x-4*scale,y-75*scale,11*scale,Wood);
		Line(x,y-35*scale,x-32*scale,y-72*scale,6*scale,Wood);
		Line(x-2*scale,y-46*scale,x+31*scale,y-90*scale,5*scale,Wood);
		for(int i=0;i<15;++i)
		{
			float a=i*2.4f, cx=x+std::cos(a)*(20+(i%3)*9)*scale;
			float cy=y-(77+std::sin(a)*25)*scale;
			Color c=pink ? Color{.80f+(i%3)*.06f,.43f+(i%4)*.055f,.57f+(i%3)*.055f,1} :
				Color{.20f+(i%3)*.055f,.40f+(i%4)*.045f,.32f+(i%3)*.035f,1};
			Ellipse(cx,cy,22*scale,17*scale,c);
			if (pink) Ellipse(cx-4*scale,cy-6*scale,5*scale,3*scale,Alpha(Cream,.48f));
		}
	}
	void Person(float x, float y, Color robe, bool player)
	{
		float step=player && moving ? std::sin(walk*12)*3 : 0;
		float bob=std::fabs(step)*.4f;
		Ellipse(x,y,20,6,Alpha(Ink,.32f));
		Rect(x-10,y-9+step,8,9,Ink); Rect(x+3,y-9-step,8,9,Ink);
		Tri(x-10,y-38-bob,x-16,y-8-bob,x+16,y-8-bob,robe);
		Tri(x-10,y-38-bob,x+10,y-38-bob,x+16,y-8-bob,robe);
		Line(x-11,y-34-bob,x-19,y-19-step,8,robe);
		Line(x+11,y-34-bob,x+19,y-19+step,8,robe);
		Line(x-5,y-37-bob,x+3,y-25-bob,3,Cream);
		Rect(x-12,y-23-bob,24,4,Gold);
		Ellipse(x,y-47-bob,10,12,{.94f,.74f,.56f,1});
		if (player && facing==GLUT_KEY_UP) Ellipse(x,y-48-bob,10,10,Wood);
		else
		{
			float offset=player && facing==GLUT_KEY_LEFT ? -3.0f : (player && facing==GLUT_KEY_RIGHT ? 3.0f : 0.0f);
			Ellipse(x-3+offset,y-47-bob,1.2f,1.8f,Ink); Ellipse(x+3+offset,y-47-bob,1.2f,1.8f,Ink);
		}
		if (player)
		{
			Ellipse(x,y-57-bob,19,4,Gold);
			Tri(x-13,y-58-bob,x,y-67-bob,x+13,y-58-bob,{.74f,.57f,.32f,1});
			Line(x-8,y-35-bob,x+12,y-17-bob,2,Wood);
			Rect(x+9,y-22-bob,10,13,{.57f,.32f,.18f,1});
			Rect(x+11,y-22-bob,6,3,Gold);
		}
		else { Ellipse(x,y-56,11,5,Wood); Rect(x-5,y-64,10,7,Wood); }
	}
	void HerbPlant(float x,float y)
	{
		Glow(x,y-12,22,{.50f,.95f,.72f,1});
		for(int i=0;i<3;++i)
		{
			float dx=(i-1)*7.0f;
			Line(x,y,x+dx,y-19,2,{.38f,.65f,.33f,1});
			Ellipse(x+dx-3,y-10,6,3,{.38f,.75f,.50f,1});
			Ellipse(x+dx,y-21,4,5,{.78f,.81f,.96f,1});
		}
	}
	void Background()
	{
		bool palace=area==Area::Palace;
		Gradient(0,0,1000,700,palace ? Color{.12f,.20f,.34f,1} : Color{.66f,.44f,.45f,1},
			palace ? Color{.39f,.57f,.56f,1} : Color{.95f,.73f,.52f,1});
		Glow(833,109,72,Gold); Ellipse(833,109,27,27,Cream);
		for(int i=0;i<9;++i)
		{
			float x=i*140.0f-80, top=115+std::sin(i*1.6f)*27;
			Tri(x-120,244,x+65,top,x+210,244,palace ? Color{.24f,.37f,.44f,1} : Color{.47f,.44f,.47f,1});
		}
		Gradient(0,225,1000,425,palace ? Color{.32f,.48f,.43f,1} : Color{.48f,.55f,.38f,1},
			palace ? Color{.14f,.30f,.29f,1} : Color{.29f,.41f,.28f,1});
		for(int i=0;i<130;++i)
		{
			float x=static_cast<float>((i*193+27)%990), y=static_cast<float>(260+(i*83)%382);
			Line(x,y,x+2,y-5,1,Alpha(Cream,.13f));
		}
		Rect(450,240,100,400,{.64f,.64f,.53f,1});
		Rect(30,485,940,66,{.62f,.61f,.49f,1});
		for(int y=254;y<640;y+=28)
		{
			Line(453,static_cast<float>(y),547,static_cast<float>(y),1,Alpha(Ink,.26f));
			Line(y%56 ? 481.0f : 517.0f,static_cast<float>(y),y%56 ? 481.0f : 517.0f,static_cast<float>(y+27),1,Alpha(Ink,.16f));
		}
		for(int x=35;x<968;x+=45) Line(static_cast<float>(x),488,static_cast<float>(x),548,1,Alpha(Ink,.20f));
	}
	void Water(float x)
	{
		Ellipse(x+96,428,108,73,{.58f,.61f,.52f,1});
		Ellipse(x+96,428,98,63,{.10f,.36f,.40f,1});
		Ellipse(x+96,415,90,46,{.15f,.44f,.48f,1});
		for(int i=0;i<16;++i)
		{
			float px=x+30+(i*29)%130+std::sin(elapsed+i)*5, py=388+(i*19)%77;
			Line(px,py,px+13+std::sin(elapsed*2+i)*5,py,1,Alpha(Cream,.25f));
		}
		for(int i=0;i<4;++i)
		{
			float px=x+54+i*24, py=427+std::sin(i*2.0f)*23;
			Ellipse(px,py,11,5,{.29f,.57f,.37f,1});
			Ellipse(px+2,py-4,5,5,{.95f,.65f,.73f,1});
		}
	}
	void Scenery()
	{
		if(area==Area::Village)
		{
			House(70,255,225,true); House(705,255,225,false);
			Rect(430,211,14,87,Wood); Rect(556,211,14,87,Wood);
			Roof(414,203,172,{.51f,.21f,.22f,1});
			Rect(460,233,80,24,Ink); Text(472,234,L"궁중 입구",15,Gold);
			Lantern(431,273); Lantern(569,273);
			Rect(105,391,120,48,Wood); Rect(111,397,108,33,{.58f,.39f,.22f,1});
			Rect(99,365,132,24,{.74f,.32f,.29f,1});
			for(int i=0;i<6;++i) Rect(101+i*22.0f,365,10,24,Cream);
			Line(107,367,107,441,4,Wood); Line(223,367,223,441,4,Wood);
			for(int i=0;i<5;++i) { Ellipse(124+i*19.0f,407,7,4,Gold); Ellipse(124+i*19.0f,415,7,4,{.38f,.60f,.35f,1}); }
			Rect(645,351,5,49,Wood); Rect(621,341,54,34,Wood); Rect(625,345,46,26,Cream);
			for(int i=0;i<3;++i) Rect(630,350+i*6.0f,33,2,Alpha(Wood,.7f));
			Ellipse(808,542,113,57,{.26f,.36f,.25f,1});
			for(int i=0;i<22;++i) Ellipse(711+i*9.0f,572+std::sin(i*.45f)*15,5,3,{.64f,.59f,.43f,1});
			Text(126,302,L"달빛 약방",18,Ink); Text(769,302,L"비단 찻집",18,Ink);
			Line(60,171,940,171,2,Alpha(Wood,.6f));
			for(int i=0;i<9;++i) Lantern(80+i*105.0f,203+std::sin(i*.5f)*7);
		}
		else
		{
			Rect(34,218,932,35,{.53f,.46f,.43f,1}); Rect(34,216,932,5,Gold);
			House(285,235,430,false);
			Roof(309,184,382,{.18f,.33f,.37f,1});
			Rect(453,271,94,28,Ink); Text(470,272,L"월하 궁중",18,Gold);
			Water(92); Water(716);
			for(int i=0;i<4;++i)
			{
				float y=378+i*63.0f;
				Rect(326,y,7,32,Wood); Rect(667,y,7,32,Wood);
				Lantern(330,y+7); Lantern(670,y+7);
			}
			Rect(443,601,114,24,{.42f,.39f,.37f,1});
			Rect(435,579,8,39,Wood); Rect(557,579,8,39,Wood);
			Lantern(438,578); Lantern(561,578);
		}
	}
	void Actors()
	{
		struct Actor { float x,y,scale; int kind; };
		std::vector<Actor> actors={{playerX,playerY,1,0}};
		if(area==Area::Village)
		{
			actors.push_back({270,449,1,1});
			actors.push_back({58,459,1,3}); actors.push_back({937,425,1.1f,3});
			actors.push_back({115,626,.95f,4}); actors.push_back({890,632,1,4});
		}
		else
		{
			actors.push_back({430,429,1,2});
			actors.push_back({95,346,1.2f,4}); actors.push_back({905,346,1.2f,4});
			actors.push_back({180,599,1.2f,4}); actors.push_back({820,599,1.2f,4});
		}
		std::stable_sort(actors.begin(),actors.end(),[](const Actor& a,const Actor& b){return a.y<b.y;});
		for(const Actor& a:actors)
		{
			if(a.kind==0) Person(a.x,a.y,{.26f,.65f,.55f,1},true);
			else if(a.kind<3) Person(a.x,a.y,a.kind==1 ? Color{.66f,.37f,.35f,1} : Color{.56f,.64f,.82f,1},false);
			else Tree(a.x,a.y,a.scale,a.kind==4);
		}
	}
	void Particles()
	{
		for(int i=0;i<48;++i)
		{
			float x=std::fmod(i*73.0f+elapsed*(8+i%5),1040.0f)-20;
			float y=100+std::fmod(i*47.0f+elapsed*(12+i%7),535.0f);
			float sway=std::sin(elapsed*1.7f+i)*12;
			Ellipse(x+sway,y,3.5f,1.5f+std::fabs(std::sin(elapsed+i)),{.98f,.74f,.80f,.70f});
		}
		for(int i=0;i<16;++i)
		{
			float x=90+(i*139)%840+std::sin(elapsed+i)*10, y=398+(i*59)%232+std::cos(elapsed+i)*9;
			float alpha=.4f+.3f*std::sin(elapsed*2+i);
			Ellipse(x,y,2,2,Alpha(Gold,alpha));
		}
	}
	void Interface(const std::vector<Target>& targets,int nearest)
	{
		Rect(0,0,1000,77,Alpha(Ink,.94f)); Rect(28,75,944,1,Alpha(Gold,.7f));
		Text(29,12,area==Area::Village ? L"월하 마을" : L"월하 궁중",29,Gold);
		Text(30,48,area==Area::Village ? L"등불 아래 작은 약방과 여행의 시작" : L"연못과 꽃잎 사이, 약사의 첫걸음",15);
		Text(693,16,L"견습 약사  ·  궁중 신뢰 " + std::to_wstring(delivered ? 1 : 0),17);
		Text(693,44,L"약재 가방  ·  달빛 약초 " + std::to_wstring(herbs) + L" / 3",16,Gold);
		Rect(22,91,375,50,Alpha(Ink,.78f)); Rect(22,91,3,50,Gold);
		Text(35,97,delivered ? L"의뢰 완료 · 의원에게 약초 전달" :
			(requested ? (herbs>=3 ? L"목표 · 궁중 의원에게 약초 전달" : L"목표 · 마을 화단에서 약초 3포기 채집") : L"첫 걸음 · 시장의 약재상과 대화"),17);
		Text(35,120,L"북쪽: 궁중 입구  /  남쪽: 마을 귀환",13,Alpha(Cream,.75f));
		Rect(0,650,1000,50,Alpha(Ink,.95f)); Rect(28,650,944,1,Alpha(Gold,.6f));
		Text(30,664,L"방향키  이동     E  상호작용 / 대화 닫기     ESC  대화 닫기",17);
		Text(831,665,L"월하 · 첫 심부름",16,Gold);
		if(!speaker.empty())
		{
			Rect(36,525,928,111,Alpha(Ink,.97f)); Rect(36,525,4,111,Gold);
			Text(55,534,speaker,19,Gold); Text(55,565,message1,18); Text(55,592,message2,17);
			Text(833,535,L"E / ESC 닫기",15,Gold);
		}
		else if(nearest>=0)
		{
			Rect(315,603,370,32,Alpha(Ink,.93f));
			Text(330,607,std::wstring(L"[E] ")+targets[nearest].name,18,Gold);
		}
	}
}

bool WorldScene::Initialize()
{
	const char* vs="#version 330\nlayout(location=0) in vec2 p; layout(location=1) in vec4 c; layout(location=2) in vec2 uv; out vec4 color; out vec2 texUV; void main(){ gl_Position=vec4(p/vec2(1000,700)*vec2(2,-2)+vec2(-1,1),0,1); color=c; texUV=uv; }";
	const char* fs="#version 330\nin vec4 color; in vec2 texUV; out vec4 result; uniform sampler2D image; uniform int useTexture; uniform float fade; void main(){float a=color.a; if(useTexture!=0) a*=texture(image,texUV).r; result=vec4(color.rgb*fade,a);}";
	GLuint shaders[2]={0,0};
	for(int i=0;i<2;++i)
	{
		shaders[i]=glCreateShader(i==0 ? GL_VERTEX_SHADER : GL_FRAGMENT_SHADER);
		const char* source=i==0 ? vs : fs;
		glShaderSource(shaders[i],1,&source,NULL); glCompileShader(shaders[i]);
		GLint success=0; glGetShaderiv(shaders[i],GL_COMPILE_STATUS,&success);
		if(!success)
		{
			char log[2048]={}; glGetShaderInfoLog(shaders[i],sizeof(log),NULL,log); std::cerr<<log<<std::endl;
			for(int j=0;j<=i;++j) glDeleteShader(shaders[j]);
			return false;
		}
	}
	program=glCreateProgram();
	for(int i=0;i<2;++i) glAttachShader(program,shaders[i]);
	glLinkProgram(program);
	for(int i=0;i<2;++i) { glDetachShader(program,shaders[i]); glDeleteShader(shaders[i]); }
	GLint success=0; glGetProgramiv(program,GL_LINK_STATUS,&success);
	if(!success)
	{
		char log[2048]={}; glGetProgramInfoLog(program,sizeof(log),NULL,log); std::cerr<<log<<std::endl;
		Shutdown(); return false;
	}
	glGenVertexArrays(1,&vao); glBindVertexArray(vao); glGenBuffers(1,&vbo); glBindBuffer(GL_ARRAY_BUFFER,vbo);
	glEnableVertexAttribArray(0); glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,sizeof(Vertex),NULL);
	glEnableVertexAttribArray(1); glVertexAttribPointer(1,4,GL_FLOAT,GL_FALSE,sizeof(Vertex),reinterpret_cast<void*>(2*sizeof(float)));
	glEnableVertexAttribArray(2); glVertexAttribPointer(2,2,GL_FLOAT,GL_FALSE,sizeof(Vertex),reinterpret_cast<void*>(6*sizeof(float)));
	textureMode=glGetUniformLocation(program,"useTexture"); fadeLocation=glGetUniformLocation(program,"fade");
	glUseProgram(program); glUniform1i(glGetUniformLocation(program,"image"),0);
	glBindVertexArray(0); vertices.reserve(80000);
	return true;
}

void WorldScene::Shutdown()
{
	for(const auto& t:textCache) if(t.second.id) glDeleteTextures(1,&t.second.id);
	textCache.clear();
	if(vbo) glDeleteBuffers(1,&vbo); if(vao) glDeleteVertexArrays(1,&vao); if(program) glDeleteProgram(program);
	vbo=vao=program=0;
}

void WorldScene::Render()
{
	if(!program) return;
	glClearColor(.035f,.045f,.065f,1); glClear(GL_COLOR_BUFFER_BIT);
	glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA); glDisable(GL_DEPTH_TEST);
	vertices.clear(); labels.clear();
	Background(); Scenery();
	auto targets=Targets(); int nearest=Nearest(targets);
	for(const Target& t:targets)
	{
		if(t.type==Herb) HerbPlant(t.x,t.y);
		else if(t.type==Gate) Ring(t.x,t.y,30+std::sin(elapsed*2)*2,Gold);
	}
	if(nearest>=0 && speaker.empty()) Ring(targets[nearest].x,targets[nearest].y,25,Gold);
	Actors(); Particles(); Interface(targets,nearest); Flush();
}

void WorldScene::Update(float seconds)
{
	seconds=Clamp(seconds,0,.05f); elapsed+=seconds; transition=(std::max)(0.0f,transition-seconds);
	moving=false;
	if(!speaker.empty() || transition>0) {walk=0; return;}
	float dx=(arrows[2]?1.0f:0)-(arrows[0]?1.0f:0), dy=(arrows[3]?1.0f:0)-(arrows[1]?1.0f:0);
	float length=std::sqrt(dx*dx+dy*dy);
	if(length>0)
	{
		float speed=160*seconds/length, x=Clamp(playerX+dx*speed,42,958), y=Clamp(playerY+dy*speed,268,620);
		float oldX=playerX,oldY=playerY;
		if(CanStand(x,playerY)) playerX=x;
		if(CanStand(playerX,y)) playerY=y;
		moving=oldX!=playerX || oldY!=playerY;
		facing=dy<0 ? GLUT_KEY_UP : (dy>0 ? GLUT_KEY_DOWN : (dx<0 ? GLUT_KEY_LEFT : GLUT_KEY_RIGHT));
	}
	if(moving) walk+=seconds; else walk=0;
}

void WorldScene::Resize(int width,int height)
{
	width=(std::max)(1,width); height=(std::max)(1,height);
	float scale=(std::min)(width/1000.0f,height/700.0f);
	int w=(std::max)(1,static_cast<int>(1000*scale)), h=(std::max)(1,static_cast<int>(700*scale));
	glViewport((width-w)/2,(height-h)/2,w,h);
}

void WorldScene::Arrow(int key,bool down)
{
	if(key>=GLUT_KEY_LEFT && key<=GLUT_KEY_DOWN) arrows[key-GLUT_KEY_LEFT]=down;
}

void WorldScene::Key(unsigned char key,bool down)
{
	if(key=='e' || key=='E')
	{
		if(down && !eDown) Interact();
		eDown=down;
	}
	if(key==27 && down) {speaker.clear(); ClearInput();}
}

void WorldScene::ClearInput()
{
	for(int i=0;i<4;++i) arrows[i]=false;
	moving=false; walk=0; eDown=false;
}
