#include "stdafx.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "Dependencies/glew.h"
#include "Dependencies/freeglut.h"
#include "WorldScene.h"
#include "PostProcessing.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <map>
#include <string>
#include <utility>
#include <vector>
#include <cstdint>

namespace
{
	const float Pi = 3.14159265f;
	struct Color { float r, g, b, a; };
	Color Ink = { .08f, .13f, .18f, 1 }, Gold = { .91f, .73f, .40f, 1 };
	Color Cream = { .97f, .92f, .78f, 1 }, Wood = { .30f, .16f, .16f, 1 };
	struct Vertex { float x, y, r, g, b, a, u, v, emission; };
	struct Texture { GLuint id; int width, height; };
	struct Label { float x, y; std::wstring text; int size; Color color; };
	struct Obstacle { float x, y, w, h; };
	struct Target { float x, y; int type, index; const wchar_t* name; };
	enum { Merchant, Notice, Herb, Physician };
	const float WorldWidth=6000, WorldHeight=4200, Pixel=2;
	const float RiverTop=2200, RiverBottom=2440;
	float playerX=1400, playerY=2900, walk=0, elapsed=0;
	float cameraX=900, cameraY=2550, cameraSmoothX=900, cameraSmoothY=2550;
	bool worldDrawing=false;
	enum DecorKind { Home, PalaceHall, Stall, Board, GreenTree, PinkTree, Lamp, Wall, Arch };
	struct Decoration { DecorKind kind; float x,y,w,h; int variant; };
	std::vector<Decoration> decorations;
	std::vector<Obstacle> obstacles;
	bool arrows[4] = {}, eDown = false, moving = false;
	int facing = GLUT_KEY_DOWN;
	bool picked[3] = {}, requested = false, delivered = false;
	int herbs = 0;
	std::wstring speaker, message1, message2;
	GLuint program = 0, vao = 0, vbo = 0;
	GLint textureMode = -1, fadeLocation = -1, linearLocation = -1;
	float emission = 1.0f;
	int viewportX = 0, viewportY = 0, viewportWidth = 1000, viewportHeight = 700;
	std::vector<Vertex> vertices;
	std::vector<Label> labels;
	std::map<std::pair<std::wstring, int>, Texture> textCache;

	float Clamp(float v, float lo, float hi) { return (std::max)(lo, (std::min)(hi, v)); }
	Color Alpha(Color c, float a) { c.a = a; return c; }
	Vertex V(float x, float y, Color c, float u = 0, float v = 0)
	{
		if(worldDrawing)
		{
			x=std::floor(x/Pixel)*Pixel-cameraX;
			y=std::floor(y/Pixel)*Pixel-cameraY;
		}
		return { x, y, c.r, c.g, c.b, c.a, u, v, emission };
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
		// Filled pixel scanlines, not smooth triangle-fan circles.
		if(rx<=0 || ry<=0) return;
		const float step=worldDrawing ? Pixel : 1.0f;
		for(float py=-ry; py<ry; py+=step)
		{
			float relative=Clamp((py+step*.5f)/ry,-1,1);
			float half=rx*std::sqrt((std::max)(0.0f,1-relative*relative));
			Rect(x-half,y+py,(std::max)(step,half*2),step,c);
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
	void LightEllipse(float x, float y, float rx, float ry, Color c, float intensity)
	{
		// Geometry carries radiance separately from the authored sRGB palette.
		const float previous = emission;
		emission = intensity;
		Ellipse(x,y,rx,ry,c);
		emission = previous;
	}
	void LightRect(float x,float y,float width,float height,Color c,float intensity)
	{
		const float previous=emission; emission=intensity;
		Rect(x,y,width,height,c); emission=previous;
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
	void Flush(bool hdr = false)
	{
		glUseProgram(program); glBindVertexArray(vao); glBindBuffer(GL_ARRAY_BUFFER,vbo);
		glUniform1f(fadeLocation,1.0f);
		glUniform1i(linearLocation,hdr ? 1 : 0);
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


	bool InPalace(float x,float y) { return x>=3500 && x<=5200 && y>=350 && y<=1850; }
	bool OnRoad(float x,float y)
	{
		return (x>=600 && x<=5050 && y>=2850 && y<=2990) ||
			(x>=1320 && x<=1480 && y>=1550 && y<=3500) ||
			(x>=4270 && x<=4430 && y>=1650 && y<=2970);
	}
	bool Visible(float x,float y,float padding=240)
	{
		return x>=cameraX-padding && x<=cameraX+1000+padding &&
			y>=cameraY-padding && y<=cameraY+700+padding;
	}
	std::uint32_t Hash(int x,int y)
	{
		std::uint32_t value=static_cast<std::uint32_t>(x)*374761393u+static_cast<std::uint32_t>(y)*668265263u+20260908u;
		value=(value^(value>>13))*1274126177u;
		return value^(value>>16);
	}
	bool CanStand(float x,float y)
	{
		// Water is continuous across the map. Both bridges are physically traversable.
		bool onBridge=std::fabs(x-1400)<76 || std::fabs(x-4350)<76;
		if(y>RiverTop-9 && y<RiverBottom+9 && !onBridge) return false;
		for(const Obstacle& o:obstacles)
			if(x>o.x-13 && x<o.x+o.w+13 && y>o.y-8 && y<o.y+o.h+8) return false;
		return true;
	}
	void Building(DecorKind kind,float x,float foot,float width,int variant=0)
	{
		decorations.push_back({kind,x,foot,width,0,variant});
		obstacles.push_back({x,foot-100,width,100});
	}
	void PlaceTree(float x,float y,bool pink,float scale=1)
	{
		decorations.push_back({pink?PinkTree:GreenTree,x,y,scale,0,0});
		obstacles.push_back({x-9,y-10,18,14});
	}
	void BuildWorld()
	{
		decorations.clear(); obstacles.clear();
		Building(Home,850,2680,250,1); Building(Home,1580,2660,280);
		Building(Home,840,3270,260); Building(Home,1610,3290,245);
		Building(Home,2290,2820,230); Building(Home,2750,3240,240);
		decorations.push_back({Stall,1135,2820,132,0,0});
		obstacles.push_back({1135,2772,132,48});
		decorations.push_back({Board,1640,2830,0,0,0});
		Building(PalaceHall,4050,960,590);
		Building(Home,3650,1090,245); Building(Home,4820,1090,245);
		Building(Home,4040,670,600);
		// The south wall has a 180-unit opening; there is no portal or loading trigger.
		const Obstacle walls[]={{3500,350,1700,22},{3500,350,22,1500},{5178,350,22,1500},
			{3500,1828,760,22},{4440,1828,760,22}};
		for(const Obstacle& wall:walls)
		{
			obstacles.push_back(wall);
			decorations.push_back({Wall,wall.x,wall.y,wall.w,wall.h,0});
		}
		decorations.push_back({Arch,4350,1850,0,0,0});
		obstacles.push_back({3660,1210,360,240}); obstacles.push_back({4630,1210,360,240});
		for(int i=0;i<5;++i)
		{
			float y=1100+i*150.0f;
			decorations.push_back({Lamp,4210,y,0,0,0});
			decorations.push_back({Lamp,4490,y,0,0,0});
		}
		for(int i=0;i<10;++i)
		{
			float x=1150+i*320.0f;
			decorations.push_back({Lamp,x,3040,0,0,0});
		}
		for(int i=0;i<6;++i)
		{
			float y=1870+i*170.0f;
			if(y>RiverTop-70 && y<RiverBottom+60) continue;
			decorations.push_back({Lamp,4230,y,0,0,0});
			decorations.push_back({Lamp,4470,y,0,0,0});
		}
		for(int i=0;i<5;++i)
		{
			if(i==2) continue; // Keep the central palace avenue open.
			PlaceTree(3610+i*360.0f,1650,true,1.2f);
			PlaceTree(3610+i*360.0f,520,true,1.1f);
		}
		PlaceTree(720,2890,true,1.4f); PlaceTree(2020,3070,true,1.3f);
		PlaceTree(1070,3100,true); PlaceTree(1940,2700,true);
		// Deterministic decoration placement, with reserved settlement, road and river corridors.
		for(int gy=1;gy<24;++gy) for(int gx=1;gx<35;++gx)
		{
			auto seed=Hash(gx,gy);
			if(seed%3==0) continue;
			float x=gx*170.0f+static_cast<float>(seed%66),y=gy*170.0f+static_cast<float>((seed>>8)%60);
			if(InPalace(x,y) || (x>620 && x<3050 && y>2450 && y<3420) ||
				(y>RiverTop-110 && y<RiverBottom+120) || OnRoad(x,y) ||
				OnRoad(x-85,y) || OnRoad(x+85,y) || OnRoad(x,y-85) || OnRoad(x,y+85)) continue;
			bool nearBuilding=false;
			for(const Obstacle& o:obstacles)
				if(x>o.x-90 && x<o.x+o.w+90 && y>o.y-90 && y<o.y+o.h+90) {nearBuilding=true;break;}
			if(!nearBuilding) PlaceTree(x,y,seed%7==0,.8f+static_cast<float>(seed%5)*.12f);
		}
	}
	std::vector<Target> Targets()
	{
		std::vector<Target> targets={{1300,2830,Merchant,0,L"약재상과 대화"},
			{1640,2830,Notice,0,L"길 안내 읽기"},{4280,1340,Physician,0,L"궁중 의원과 대화"}};
		const float hx[]={1810,1870,1930},hy[]={3020,3060,3000};
		for(int i=0;i<3;++i) if(!picked[i]) targets.push_back({hx[i],hy[i],Herb,i,L"달빛 약초 채집"});
		return targets;
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
		if(!speaker.empty()) {speaker.clear(); WorldScene::ClearInput(); return;}
		auto targets=Targets(); int id=Nearest(targets);
		if(id<0) return;
		Target t=targets[id];
		switch(t.type)
		{
		case Merchant:
			requested=true;
			if(delivered) Speak(L"약재상 · 연화",L"의원께서 고맙다는 말을 전해 달라 하셨네.",L"강가와 숲길을 천천히 둘러보고 오게. 언제든 쉬었다 가도 좋아.");
			else Speak(L"약재상 · 연화",L"마을 동쪽 화단에서 달빛 약초 세 포기를 구해 주겠나?",L"큰길을 동쪽으로 따라가다 북쪽 다리를 건너면 궁중 의원을 만날 수 있네.");
			break;
		case Notice:
			Speak(L"월하 고을 길 안내",L"동쪽 큰길 → 북쪽 돌다리 → 열린 궁문 → 궁중 정원",L"지역은 걸어서 자유롭게 오갑니다. E는 대화와 채집에만 사용합니다.");
			break;
		case Herb:
			picked[t.index]=true; ++herbs;
			Speak(L"약초 채집",L"달빛 약초를 약재 가방에 담았습니다.",L"세 포기를 모아 북동쪽 궁중 정원의 의원에게 전달하세요.");
			break;
		case Physician:
			requested=true;
			if(delivered) Speak(L"궁중 의원 · 서윤",L"덕분에 필요한 약재를 마련했습니다. 수고하셨습니다.",L"궁중 정원과 마을을 자유롭게 둘러보세요.");
			else if(herbs>=3)
			{
				herbs-=3; delivered=true;
				Speak(L"궁중 의원 · 서윤",L"달빛 약초 세 포기, 잘 받았습니다. 첫 심부름을 마쳤군요.",L"의뢰 완료 · 궁중 신뢰 +1");
			}
			else Speak(L"궁중 의원 · 서윤",L"마을에서 자라는 달빛 약초 세 포기가 필요합니다.",L"남쪽 궁문과 다리를 지나 서쪽 큰길을 따라가면 약방이 있습니다.");
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
		Ellipse(x+sway,y-12,10,14,{.85f,.24f,.19f,1});
		LightEllipse(x+sway,y-12,6,11,{1,.64f,.27f,1},3.8f+std::sin(elapsed*2.0f+x)*.25f);
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
			Rect(px,y+23,34,38,Wood); LightRect(px+3,y+26,28,32,{.95f,.70f,.33f,1},2.4f);
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

	void Person(float x,float y,Color robe,bool player)
	{
		// Each character is an authored 16 x 24 pixel sprite, drawn at 2 world units per pixel.
		static const char* sprite[]={
			"......GGGG......",".....GGGGGG.....","....GGGGGGGG....","..GGGGGGGGGGGG..",
			"....HHHHHHHH....","....HSSSSSSH....","....SISSSSIS....","....SSSSSSSS....",
			".....SSSSSS.....","....RRCCRRRR....","...RRRRCRRRRR...","..RRRRRCRRRRRR..",
			"..RRRRRRRRRRRR..","..SSRRRRRRRRSS..","....BBBBBBBB....","....RRRRRRRR....",
			"....RRRRRRRR....","...RRRRRRRRRR...","...RRRRRRRRRR...","...RRRRRRRRRR...",
			"....RRR..RRR....","....III..III....","....III..III....","................"};
		Ellipse(x,y,18,5,Alpha(Ink,.32f));
		int frame=player && moving ? static_cast<int>(walk*9)%4 : 0;
		float bob=frame%2==1 ? -2.0f : 0.0f;
		for(int row=0;row<24;++row) for(int col=0;col<16;++col)
		{
			char p=sprite[row][col];
			if(p=='.') continue;
			Color c=robe;
			switch(p)
			{
			case 'G': c=player ? Gold : Wood; break;
			case 'H': c=Wood; break;
			case 'S': c={.92f,.72f,.53f,1}; break;
			case 'I': c=Ink; break;
			case 'C': c=Cream; break;
			case 'B': c=Gold; break;
			default: if(col<7) {c.r*=.8f;c.g*=.8f;c.b*=.8f;} break;
			}
			if(player && facing==GLUT_KEY_UP && row>=4 && row<=8) c=Wood;
			if(player && row==6 && facing==GLUT_KEY_LEFT && col==10) c={.92f,.72f,.53f,1};
			if(player && row==6 && facing==GLUT_KEY_RIGHT && col==5) c={.92f,.72f,.53f,1};
			float footOffset=0;
			if(row>=20 && moving && player) footOffset=col<8 ? (frame==1?-2.0f:0) : (frame==3?-2.0f:0);
			Rect(x-16+col*2,y-46+row*2+bob+footOffset,2,2,c);
		}
		if(player) {Rect(x+10,y-22+bob,8,10,{.48f,.26f,.14f,1});Rect(x+10,y-22+bob,8,2,Gold);}
	}

	void HerbPlant(float x,float y)
	{
		for(int i=0;i<3;++i)
		{
			float dx=(i-1)*7.0f;
			Line(x,y,x+dx,y-19,2,{.38f,.65f,.33f,1});
			Ellipse(x+dx-3,y-10,6,3,{.38f,.75f,.50f,1});
			LightEllipse(x+dx,y-21,4,5,{.78f,.81f,.96f,1},2.5f);
		}
	}

	void Pave(float x,float y,float width,float height)
	{
		Rect(x,y,width,height,{.54f,.55f,.44f,1});
		float left=(std::max)(x,cameraX-32),right=(std::min)(x+width,cameraX+1032);
		float top=(std::max)(y,cameraY-32),bottom=(std::min)(y+height,cameraY+732);
		if(left>=right || top>=bottom) return;
		for(float px=std::ceil(left/32)*32;px<right;px+=32) Rect(px,top,2,bottom-top,{.45f,.47f,.38f,1});
		for(float py=std::ceil(top/32)*32;py<bottom;py+=32) Rect(left,py,right-left,2,{.62f,.62f,.49f,1});
	}
	void Pond(float x,float y,float width,float height)
	{
		if(!Visible(x+width*.5f,y+height*.5f,400)) return;
		Rect(x-8,y-8,width+16,height+16,{.45f,.51f,.43f,1});
		Rect(x,y,width,height,{.12f,.32f,.37f,1});
		Rect(x+8,y+8,width-16,height-16,{.16f,.40f,.43f,1});
		for(int i=0;i<30;++i)
		{
			float px=x+16+(i*61)%static_cast<int>(width-44),py=y+16+(i*37)%static_cast<int>(height-32);
			Rect(px+std::floor(std::sin(elapsed+i)*2)*2,py,12+(i%3)*4.0f,2,{.32f,.55f,.55f,1});
		}
		for(int i=0;i<7;++i)
		{
			float px=x+40+(i*41)%static_cast<int>(width-80),py=y+35+(i*29)%static_cast<int>(height-60);
			Ellipse(px,py,12,6,{.28f,.54f,.32f,1});
			Rect(px-3,py-7,6,6,{.94f,.58f,.70f,1}); Rect(px-1,py-9,2,2,Cream);
		}
	}
	void Bridge(float x)
	{
		Rect(x-96,RiverTop-16,192,RiverBottom-RiverTop+32,Wood);
		Rect(x-84,RiverTop-16,168,RiverBottom-RiverTop+32,{.60f,.43f,.29f,1});
		for(float y=RiverTop-16;y<RiverBottom+16;y+=16)
		{
			Rect(x-84,y,168,2,{.32f,.22f,.19f,1});
			Rect(x-100,y-7,8,16,Gold); Rect(x+92,y-7,8,16,Gold);
		}
		Rect(x-96,RiverTop-26,4,RiverBottom-RiverTop+32,Wood);
		Rect(x+92,RiverTop-26,4,RiverBottom-RiverTop+32,Wood);
	}
	void Background()
	{
		// Only visible ground tiles are submitted; all patterns are stable in world coordinates.
		int minX=(std::max)(0,static_cast<int>(cameraX/32)-1);
		int minY=(std::max)(0,static_cast<int>(cameraY/32)-1);
		for(int gy=minY;gy<=static_cast<int>((cameraY+700)/32)+1;++gy)
		for(int gx=minX;gx<=static_cast<int>((cameraX+1000)/32)+1;++gx)
		{
			auto seed=Hash(gx,gy);
			float x=gx*32.0f,y=gy*32.0f,v=static_cast<float>(seed%5)*.012f;
			Rect(x,y,32,32,{.25f+v,.40f+v,.30f+v*.5f,1});
			if(seed%3==0) {Rect(x+8,y+20,2,4,{.40f,.54f,.35f,1});Rect(x+12,y+22,2,4,{.40f,.54f,.35f,1});}
			if(seed%29==0) {Rect(x+23,y+10,2,2,{.78f,.67f,.42f,1});Rect(x+21,y+12,6,2,{.64f,.58f,.40f,1});}
		}
		Pave(600,2850,4450,140); Pave(1320,1550,160,1950); Pave(4270,1650,160,1320);
		Pave(3522,372,1656,1456);
		// Grass islands soften the large stone courtyard.
		Rect(3600,1160,480,360,{.24f,.39f,.31f,1}); Rect(4560,1160,480,360,{.24f,.39f,.31f,1});
		Pond(3660,1210,360,240); Pond(4630,1210,360,240);
		Rect(0,RiverTop-12,WorldWidth,RiverBottom-RiverTop+24,{.53f,.56f,.42f,1});
		Rect(0,RiverTop,WorldWidth,RiverBottom-RiverTop,{.11f,.30f,.36f,1});
		Rect(0,RiverTop+12,WorldWidth,RiverBottom-RiverTop-24,{.15f,.39f,.43f,1});
		for(int gx=static_cast<int>(cameraX/48);gx<static_cast<int>((cameraX+1000)/48)+1;++gx)
		for(int gy=0;gy<6;++gy)
		{
			float px=gx*48.0f+std::floor(std::sin(elapsed+gx+gy)*3)*2,py=RiverTop+20+gy*36.0f;
			Rect(px,py,14+(gx%3)*4.0f,2,{.29f,.49f,.52f,1});
		}
		Bridge(1400); Bridge(4350);
		Rect(1750,2960,240,150,{.23f,.30f,.22f,1});
		for(int row=0;row<5;++row) Rect(1760,2970+row*28.0f,220,2,{.35f,.40f,.27f,1});
	}
	void DrawDecoration(const Decoration& d)
	{
		switch(d.kind)
		{
		case Home:
		case PalaceHall:
			House(d.x,d.y-100,d.w,d.variant==1 || InPalace(d.x,d.y));
			if(d.kind==PalaceHall)
			{
				Roof(d.x+36,d.y-151,d.w-72,{.18f,.34f,.37f,1});
				Rect(d.x+d.w*.5f-52,d.y-83,104,25,Ink); Text(d.x+d.w*.5f-39,d.y-82,L"월하 궁중",16,Gold);
			}
			else if(d.variant==1) Text(d.x+75,d.y-72,L"달빛 약방",17,Ink);
			break;
		case GreenTree: case PinkTree: Tree(d.x,d.y,d.w,d.kind==PinkTree); break;
		case Lamp:
			Rect(d.x-3,d.y-80,6,80,Wood); Rect(d.x-12,d.y-82,24,4,Gold); Lantern(d.x,d.y-44);
			break;
		case Stall:
			Rect(d.x,d.y-48,d.w,48,Wood); Rect(d.x-6,d.y-76,d.w+12,24,{.71f,.34f,.30f,1});
			for(int i=0;i<7;++i) Rect(d.x-4+i*20.0f,d.y-76,10,24,Cream);
			for(int i=0;i<6;++i) {Rect(d.x+8+i*20.0f,d.y-43,14,10,Gold);Rect(d.x+9+i*20.0f,d.y-42,10,5,{.35f,.56f,.30f,1});}
			break;
		case Board:
			Rect(d.x-3,d.y-46,6,46,Wood); Rect(d.x-27,d.y-58,54,34,Wood); Rect(d.x-23,d.y-54,46,26,Cream);
			for(int i=0;i<3;++i) Rect(d.x-18,d.y-48+i*6.0f,34,2,Wood);
			break;
		case Wall:
			Rect(d.x,d.y-30,d.w,d.h+30,{.48f,.40f,.39f,1});
			Rect(d.x-2,d.y-34,d.w+4,6,{.18f,.28f,.31f,1});
			Rect(d.x,d.y-25,d.w,2,Gold);
			if(d.w>d.h)
			{
				for(float x=(std::max)(d.x,cameraX-48);x<d.x+d.w && x<cameraX+1048;x+=40)
					Rect(x,d.y-20,2,34,{.38f,.31f,.31f,1});
			}
			else for(float y=(std::max)(d.y,cameraY-32);y<d.y+d.h && y<cameraY+732;y+=32)
				Rect(d.x,y,d.w,2,Gold);
			break;
		case Arch:
			Rect(d.x-102,d.y-108,12,108,Wood); Rect(d.x+90,d.y-108,12,108,Wood);
			Roof(d.x-128,d.y-125,256,{.49f,.22f,.24f,1});
			Lantern(d.x-104,d.y-64); Lantern(d.x+104,d.y-64);
			break;
		}
	}
	void Actors()
	{
		struct Entry {float depth; int index;};
		std::vector<Entry> entries={{playerY,-1}};
		if(Visible(1300,2830)) entries.push_back({2830,-2});
		if(Visible(4280,1340)) entries.push_back({1340,-3});
		for(size_t i=0;i<decorations.size();++i)
		{
			const Decoration& d=decorations[i];
			bool visible=d.kind==Wall ?
				(d.x<cameraX+1000 && d.x+d.w>cameraX && d.y-40<cameraY+700 && d.y+d.h>cameraY) :
				Visible(d.x+((d.kind==Home||d.kind==PalaceHall)?d.w*.5f:0),d.y,
					(d.kind==Home||d.kind==PalaceHall)?d.w*.5f+180:240);
			if(visible) entries.push_back({d.y+d.h,static_cast<int>(i)});
		}
		std::stable_sort(entries.begin(),entries.end(),[](const Entry& a,const Entry& b){return a.depth<b.depth;});
		for(const Entry& entry:entries)
		{
			if(entry.index>=0) DrawDecoration(decorations[entry.index]);
			else if(entry.index==-1) Person(playerX,playerY,{.28f,.64f,.53f,1},true);
			else if(entry.index==-2) Person(1300,2830,{.69f,.39f,.34f,1},false);
			else Person(4280,1340,{.53f,.65f,.80f,1},false);
		}
	}
	void Particles()
	{
		// Particles belong to world cells and do not follow the camera.
		for(int cy=static_cast<int>(cameraY/256)-1;cy<=static_cast<int>((cameraY+700)/256);++cy)
		for(int cx=static_cast<int>(cameraX/256)-1;cx<=static_cast<int>((cameraX+1000)/256);++cx)
		{
			auto seed=Hash(cx,cy);
			float x=cx*256.0f+std::fmod(static_cast<float>(seed%250)+elapsed*7,256.0f);
			float y=cy*256.0f+std::fmod(static_cast<float>((seed>>8)%250)+elapsed*13,256.0f);
			Rect(x,y,4,2,{.91f,.62f,.69f,.80f});
			float fx=cx*256.0f+90+std::floor(std::sin(elapsed+cx)*5)*2;
			float fy=cy*256.0f+170+std::floor(std::cos(elapsed+cy)*4)*2;
			if(fy>RiverTop-90 && fy<RiverBottom+160)
				LightEllipse(fx,fy,2,2,Gold,2.4f+.5f*std::sin(elapsed*2+cx));
		}
	}
	void Minimap()
	{
		const float mx=804,my=94,mw=170,mh=119;
		Rect(mx-5,my-5,mw+10,mh+32,Alpha(Ink,.93f));
		Rect(mx,my,mw,mh,{.23f,.36f,.28f,1});
		auto box=[&](float x,float y,float w,float h,Color c) {Rect(mx+x/WorldWidth*mw,my+y/WorldHeight*mh,w/WorldWidth*mw,h/WorldHeight*mh,c);};
		box(0,RiverTop,WorldWidth,RiverBottom-RiverTop,{.19f,.47f,.53f,1});
		box(600,2850,4450,140,Gold); box(1320,1550,160,1950,Gold); box(4270,1650,160,1320,Gold);
		box(3500,350,1700,1500,{.63f,.44f,.38f,1});
		box(850,2500,1150,800,{.76f,.61f,.38f,1});
		float vx=mx+cameraX/WorldWidth*mw,vy=my+cameraY/WorldHeight*mh,vw=1000/WorldWidth*mw,vh=700/WorldHeight*mh;
		Rect(vx,vy,vw,1,Cream); Rect(vx,vy+vh,vw,1,Cream); Rect(vx,vy,1,vh,Cream); Rect(vx+vw,vy,1,vh,Cream);
		float px=mx+playerX/WorldWidth*mw,py=my+playerY/WorldHeight*mh;
		Rect(px-2,py-2,5,5,{.92f,.99f,.85f,1});
		Text(mx+2,my+mh+5,L"마을 ↗ 다리 ↗ 궁중",13,Gold);
	}


	void Interface(const std::vector<Target>& targets,int nearest)
	{
		const wchar_t* location=InPalace(playerX,playerY) ? L"월하 궁중" :
			(playerY>RiverTop-120 && playerY<RiverBottom+120 ? L"달빛 강가" :
			(playerX<2400 && playerY>2440 && playerY<3450 ? L"월하 마을" : L"월하 들녘"));
		Rect(0,0,1000,77,Alpha(Ink,.94f)); Rect(28,75,944,1,Alpha(Gold,.7f));
		Text(29,12,location,29,Gold);
		Text(30,48,L"열린 길을 따라 이어지는 픽셀 월드",15);
		Text(693,16,L"견습 약사  ·  궁중 신뢰 "+std::to_wstring(delivered?1:0),17);
		Text(693,44,L"약재 가방  ·  달빛 약초 "+std::to_wstring(herbs)+L" / 3",16,Gold);
		Rect(22,91,390,51,Alpha(Ink,.85f)); Rect(22,91,3,51,Gold);
		Text(35,97,delivered ? L"의뢰 완료 · 의원에게 약초 전달" :
			(requested ? (herbs>=3 ? L"목표 · 북동쪽 궁중 의원에게 약초 전달" : L"목표 · 마을 동쪽 화단에서 약초 채집") : L"첫 걸음 · 시장의 약재상과 대화"),16);
		Text(35,121,L"지역 이동은 걷기로 · E는 대화 / 채집",13,Alpha(Cream,.85f));
		Minimap();
		Rect(0,642,1000,58,Alpha(Ink,.95f)); Rect(28,642,944,1,Alpha(Gold,.6f));
		Text(28,649,L"방향키 이동   E 대화·채집   ESC 대화 닫기   |   미니맵의 흰 점: 현재 위치",15);
		const auto& fx=PostProcessing::Options();
		if(PostProcessing::Available())
		{
			std::wstring s=std::wstring(L"F1 후처리 ")+(fx.enabled?L"ON":L"OFF");
			s+=std::wstring(L"   F2 블룸 ")+(fx.bloom?L"ON":L"OFF");
			s+=std::wstring(L"   F3 비넷 ")+(fx.vignette?L"ON":L"OFF");
			s+=std::wstring(L"   F4 블러 ")+(fx.edgeBlur?L"ON":L"OFF");
			s+=L"   F5/F6 노출 "+std::to_wstring(static_cast<int>(fx.exposure*100+.5f))+L"%   F7 초기화";
			Text(28,674,s,13,Gold);
		}
		else Text(28,674,L"후처리 사용 불가 · 기본 렌더링 (콘솔 확인)",14,Gold);
		if(!speaker.empty())
		{
			Rect(36,525,928,111,Alpha(Ink,.97f)); Rect(36,525,4,111,Gold);
			Text(55,534,speaker,19,Gold); Text(55,565,message1,18); Text(55,592,message2,17);
			Text(833,535,L"E / ESC 닫기",15,Gold);
		}
		else if(nearest>=0)
		{
			Rect(315,600,370,32,Alpha(Ink,.93f));
			Text(330,604,std::wstring(L"[E] ")+targets[nearest].name,18,Gold);
		}
	}


}

bool WorldScene::Initialize()
{
	const char* vs=R"GLSL(#version 330 core
layout(location=0) in vec2 p;
layout(location=1) in vec4 c;
layout(location=2) in vec2 uv;
layout(location=3) in float radiance;
out vec4 color;
out vec2 texUV;
out float lightEnergy;
void main()
{
    gl_Position=vec4(p/vec2(1000,700)*vec2(2,-2)+vec2(-1,1),0,1);
    color=c; texUV=uv; lightEnergy=radiance;
}
)GLSL";
	const char* fs=R"GLSL(#version 330 core
in vec4 color;
in vec2 texUV;
in float lightEnergy;
layout(location=0) out vec4 result;
layout(location=1) out vec4 emissive;
uniform sampler2D image;
uniform int useTexture;
uniform int linearOutput;
uniform float fade;
vec3 srgbToLinear(vec3 c)
{
    return mix(pow((c+0.055)/1.055,vec3(2.4)),c/12.92,lessThanEqual(c,vec3(0.04045)));
}
void main()
{
    float a=color.a;
    if(useTexture!=0) a*=texture(image,texUV).r;
    vec3 rgb=linearOutput!=0 ? srgbToLinear(color.rgb)*lightEnergy : color.rgb;
    result=vec4(rgb*fade,a);
    // Zero-emission opaque objects also occlude lights behind them in this attachment.
    vec3 energy=linearOutput!=0 ? srgbToLinear(color.rgb)*max(lightEnergy-1.0,0.0) : vec3(0.0);
    emissive=vec4(energy,a);
}
)GLSL";
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
	glEnableVertexAttribArray(3); glVertexAttribPointer(3,1,GL_FLOAT,GL_FALSE,sizeof(Vertex),reinterpret_cast<void*>(8*sizeof(float)));
	textureMode=glGetUniformLocation(program,"useTexture"); fadeLocation=glGetUniformLocation(program,"fade");
	linearLocation=glGetUniformLocation(program,"linearOutput");
	glUseProgram(program); glUniform1i(glGetUniformLocation(program,"image"),0);
	glBindVertexArray(0); vertices.reserve(100000);
	BuildWorld();
	if(!PostProcessing::Initialize())
		std::cerr<<"HDR post-processing unavailable; using direct rendering.\n";
	return true;
}

void WorldScene::Shutdown()
{
	PostProcessing::Shutdown();
	for(const auto& t:textCache) if(t.second.id) glDeleteTextures(1,&t.second.id);
	textCache.clear();
	if(vbo) glDeleteBuffers(1,&vbo); if(vao) glDeleteVertexArrays(1,&vao); if(program) glDeleteProgram(program);
	vbo=vao=program=0;
}


void WorldScene::Render()
{
	if(!program) return;
	glBindFramebuffer(GL_FRAMEBUFFER,0);
	glViewport(viewportX,viewportY,viewportWidth,viewportHeight);
	glDisable(GL_FRAMEBUFFER_SRGB);
	glClearColor(.035f,.045f,.065f,1); glClear(GL_COLOR_BUFFER_BIT);
	glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA); glDisable(GL_DEPTH_TEST);
	const bool hdr=PostProcessing::BeginScene();
	vertices.clear(); labels.clear(); worldDrawing=true;
	Background();
	auto targets=Targets(); int nearest=Nearest(targets);
	for(const Target& t:targets) if(t.type==Herb && Visible(t.x,t.y)) HerbPlant(t.x,t.y);
	if(nearest>=0 && speaker.empty()) Ring(targets[nearest].x,targets[nearest].y,23,Gold);
	Actors(); Particles(); Flush(hdr);
	if(hdr) PostProcessing::EndScene(viewportX,viewportY,viewportWidth,viewportHeight,1.0f);
	vertices.clear(); labels.clear(); worldDrawing=false;
	Interface(targets,nearest); Flush();
}


void WorldScene::Update(float seconds)
{
	seconds=Clamp(seconds,0,.05f); elapsed+=seconds; moving=false;
	if(speaker.empty())
	{
		float dx=(arrows[2]?1.0f:0)-(arrows[0]?1.0f:0),dy=(arrows[3]?1.0f:0)-(arrows[1]?1.0f:0);
		float length=std::sqrt(dx*dx+dy*dy);
		if(length>0)
		{
			float distance=220*seconds/length;
			float nx=Clamp(playerX+dx*distance,24,WorldWidth-24),ny=Clamp(playerY+dy*distance,64,WorldHeight-24);
			float oldX=playerX,oldY=playerY;
			if(CanStand(nx,playerY)) playerX=nx;
			if(CanStand(playerX,ny)) playerY=ny;
			moving=oldX!=playerX || oldY!=playerY;
			facing=dy<0?GLUT_KEY_UP:(dy>0?GLUT_KEY_DOWN:(dx<0?GLUT_KEY_LEFT:GLUT_KEY_RIGHT));
		}
	}
	if(moving) walk+=seconds; else walk=0;
	const float amount=1-std::exp(-seconds*10);
	cameraSmoothX+=(Clamp(playerX-500,0,WorldWidth-1000)-cameraSmoothX)*amount;
	cameraSmoothY+=(Clamp(playerY-350,0,WorldHeight-700)-cameraSmoothY)*amount;
	cameraX=std::floor(cameraSmoothX/Pixel)*Pixel; cameraY=std::floor(cameraSmoothY/Pixel)*Pixel;
}

void WorldScene::Resize(int width,int height)
{
	width=(std::max)(1,width); height=(std::max)(1,height);
	float scale=(std::min)(width/1000.0f,height/700.0f);
	int w=(std::max)(1,static_cast<int>(1000*scale)), h=(std::max)(1,static_cast<int>(700*scale));
	if(width>=500 && height>=350)
	{
		const int pixelScale=(std::min)(width/500,height/350);
		w=500*pixelScale; h=350*pixelScale;
	}
	viewportX=(width-w)/2; viewportY=(height-h)/2; viewportWidth=w; viewportHeight=h;
	// Fixed low-resolution world, nearest-neighbor upscale, full-resolution UI.
	PostProcessing::Resize(500,350);
	glViewport(viewportX,viewportY,viewportWidth,viewportHeight);
}

void WorldScene::Arrow(int key,bool down)
{
	if(down)
	{
		PostProcessing::Settings& fx=PostProcessing::Options();
		switch(key)
		{
		case GLUT_KEY_F1: fx.enabled=!fx.enabled; return;
		case GLUT_KEY_F2: fx.bloom=!fx.bloom; return;
		case GLUT_KEY_F3: fx.vignette=!fx.vignette; return;
		case GLUT_KEY_F4: fx.edgeBlur=!fx.edgeBlur; return;
		case GLUT_KEY_F5: fx.exposure=Clamp(fx.exposure-.10f,.25f,4); return;
		case GLUT_KEY_F6: fx.exposure=Clamp(fx.exposure+.10f,.25f,4); return;
		case GLUT_KEY_F7: PostProcessing::Reset(); return;
		}
	}
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
