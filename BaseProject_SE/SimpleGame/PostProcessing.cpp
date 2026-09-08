#include "stdafx.h"
#include "Dependencies/glew.h"
#include "PostProcessing.h"
#include <algorithm>
#include <iostream>

namespace
{
	struct Target { GLuint framebuffer = 0, texture = 0, emissionTexture = 0; int width = 0, height = 0; };
	struct Buffers { Target scene, bright, bloom[2], halo[2], soft[2]; };
	Buffers buffers;
	PostProcessing::Settings settings;
	GLuint extractProgram = 0, blurProgram = 0, compositeProgram = 0, fullscreenVao = 0;
	struct ExtractUniforms { GLint threshold, knee; } extractUniforms;
	struct BlurUniforms { GLint direction; } blurUniforms;
	struct CompositeUniforms { GLint enabled, exposure, bloomStrength, vignetteStrength, edgeStrength, edgeRange, fade; } compositeUniforms;

	const char* fullscreenVS = R"GLSL(#version 330 core
out vec2 uv;
void main()
{
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    uv = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)GLSL";
	const char* extractFS = R"GLSL(#version 330 core
in vec2 uv;
out vec4 result;
uniform sampler2D source;
uniform float threshold;
uniform float knee;
vec3 bright(vec3 c)
{
    float luminance = dot(c, vec3(0.2126, 0.7152, 0.0722));
    float k = max(knee, 0.001);
    float soft = clamp(luminance - threshold + k, 0.0, 2.0*k);
    soft = soft*soft / (4.0*k);
    float contribution = max(luminance-threshold, soft) / max(luminance, 0.0001);
    return c * contribution;
}
void main()
{
    // Extract BEFORE downsampling so small emissive particles retain energy.
    vec2 d = 0.5 / vec2(textureSize(source, 0));
    vec3 c = bright(texture(source, uv+vec2(-d.x,-d.y)).rgb);
    c += bright(texture(source, uv+vec2(d.x,-d.y)).rgb);
    c += bright(texture(source, uv+vec2(-d.x,d.y)).rgb);
    c += bright(texture(source, uv+vec2(d.x,d.y)).rgb);
    result = vec4(c*0.25, 1.0);
}
)GLSL";
	const char* blurFS = R"GLSL(#version 330 core
in vec2 uv;
out vec4 result;
uniform sampler2D source;
uniform vec2 direction;
void main()
{
    vec3 c = texture(source,uv).rgb * 0.2270270270;
    c += texture(source,uv+direction*1.3846153846).rgb * 0.3162162162;
    c += texture(source,uv-direction*1.3846153846).rgb * 0.3162162162;
    c += texture(source,uv+direction*3.2307692308).rgb * 0.0702702703;
    c += texture(source,uv-direction*3.2307692308).rgb * 0.0702702703;
    result = vec4(c,1.0);
}
)GLSL";
	const char* compositeFS = R"GLSL(#version 330 core
in vec2 uv;
out vec4 result;
uniform sampler2D sceneImage;
uniform sampler2D bloomImage;
uniform sampler2D haloImage;
uniform sampler2D softImage;
uniform int enabled;
uniform float exposure;
uniform float bloomStrength;
uniform float vignetteStrength;
uniform float edgeStrength;
uniform vec2 edgeRange;
uniform float fade;
vec3 filmic(vec3 x)
{
    return clamp((x*(2.51*x+0.03))/(x*(2.43*x+0.59)+0.14),0.0,1.0);
}
vec3 linearToSRGB(vec3 x)
{
    x = max(x,vec3(0.0));
    return mix(1.055*pow(x,vec3(1.0/2.4))-0.055,12.92*x,lessThanEqual(x,vec3(0.0031308)));
}
void main()
{
    // Preserve pixel-art edges; blur and bloom are sampled smoothly and independently.
    ivec2 dimensions=textureSize(sceneImage,0);
    ivec2 pixel=clamp(ivec2(uv*vec2(dimensions)),ivec2(0),dimensions-ivec2(1));
    vec3 c = texelFetch(sceneImage,pixel,0).rgb;
    if(enabled!=0)
    {
        // Elliptical falloff in viewport coordinates, excluding letterbox bars.
        float radial = length(uv*2.0-1.0);
        float edge = smoothstep(edgeRange.x,edgeRange.y,radial);
        if(edgeStrength>0.0)
            c = mix(c,texture(softImage,uv).rgb,clamp(edge*edgeStrength,0.0,1.0));
        if(bloomStrength>0.0)
            c += (texture(bloomImage,uv).rgb*0.72+texture(haloImage,uv).rgb*0.28)*bloomStrength;
        c = filmic(c*exposure);
        c *= 1.0-clamp(vignetteStrength,0.0,0.9)*edge;
    }
    // Explicit SDR output transform; GL_FRAMEBUFFER_SRGB is disabled.
    result = vec4(clamp(linearToSRGB(c),0.0,1.0)*fade,1.0);
}
)GLSL";

	float Clamp(float value,float lo,float hi) { return (std::max)(lo,(std::min)(hi,value)); }
	GLuint Compile(GLenum type,const char* source)
	{
		GLuint shader=glCreateShader(type);
		if(!shader) return 0;
		glShaderSource(shader,1,&source,NULL); glCompileShader(shader);
		GLint ok=0; glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);
		if(!ok)
		{
			char log[2048]={}; glGetShaderInfoLog(shader,sizeof(log),NULL,log);
			std::cerr<<"Post-processing shader: "<<log<<std::endl;
			glDeleteShader(shader); return 0;
		}
		return shader;
	}
	GLuint Link(const char* fragment)
	{
		GLuint vs=Compile(GL_VERTEX_SHADER,fullscreenVS), fs=Compile(GL_FRAGMENT_SHADER,fragment);
		if(!vs || !fs) { if(vs) glDeleteShader(vs); if(fs) glDeleteShader(fs); return 0; }
		GLuint p=glCreateProgram();
		if(!p) { glDeleteShader(vs); glDeleteShader(fs); return 0; }
		glAttachShader(p,vs); glAttachShader(p,fs); glLinkProgram(p);
		glDetachShader(p,vs); glDetachShader(p,fs); glDeleteShader(vs); glDeleteShader(fs);
		GLint ok=0; glGetProgramiv(p,GL_LINK_STATUS,&ok);
		if(!ok)
		{
			char log[2048]={}; glGetProgramInfoLog(p,sizeof(log),NULL,log);
			std::cerr<<"Post-processing link: "<<log<<std::endl; glDeleteProgram(p); return 0;
		}
		return p;
	}
	void Delete(Target& t)
	{
		if(t.framebuffer) glDeleteFramebuffers(1,&t.framebuffer);
		if(t.texture) glDeleteTextures(1,&t.texture);
		if(t.emissionTexture) glDeleteTextures(1,&t.emissionTexture);
		t=Target();
	}
	void Delete(Buffers& b)
	{
		Delete(b.scene); Delete(b.bright);
		for(int i=0;i<2;++i) { Delete(b.bloom[i]); Delete(b.halo[i]); Delete(b.soft[i]); }
	}
	bool Allocate(Target& t,int width,int height,bool withEmission=false)
	{
		t.width=width; t.height=height;
		glGenTextures(1,&t.texture); glBindTexture(GL_TEXTURE_2D,t.texture);
		glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
		glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA16F,width,height,0,GL_RGBA,GL_FLOAT,NULL);
		glGenFramebuffers(1,&t.framebuffer); glBindFramebuffer(GL_FRAMEBUFFER,t.framebuffer);
		glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,t.texture,0);
		const GLenum attachments[]={GL_COLOR_ATTACHMENT0,GL_COLOR_ATTACHMENT1};
		if(withEmission)
		{
			glGenTextures(1,&t.emissionTexture); glBindTexture(GL_TEXTURE_2D,t.emissionTexture);
			glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
			glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA16F,width,height,0,GL_RGBA,GL_FLOAT,NULL);
			glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT1,GL_TEXTURE_2D,t.emissionTexture,0);
		}
		glDrawBuffers(withEmission?2:1,attachments);
		return t.texture && t.framebuffer && glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE;
	}
	void BindTexture(GLuint texture,int unit)
	{
		glActiveTexture(GL_TEXTURE0+unit); glBindTexture(GL_TEXTURE_2D,texture);
	}
	void Destination(const Target& target)
	{
		glBindFramebuffer(GL_FRAMEBUFFER,target.framebuffer); glViewport(0,0,target.width,target.height);
	}
	void Blur(const Target& source,const Target& destination,float dx,float dy)
	{
		Destination(destination); glUseProgram(blurProgram); BindTexture(source.texture,0);
		glUniform2f(blurUniforms.direction,dx/source.width,dy/source.height);
		glDrawArrays(GL_TRIANGLES,0,3);
	}
}

PostProcessing::Settings& PostProcessing::Options() { return settings; }
void PostProcessing::Reset() { settings=Settings(); }

bool PostProcessing::Initialize()
{
	extractProgram=Link(extractFS); blurProgram=Link(blurFS); compositeProgram=Link(compositeFS);
	if(!extractProgram || !blurProgram || !compositeProgram) { Shutdown(); return false; }
	glGenVertexArrays(1,&fullscreenVao);
	if(!fullscreenVao) { Shutdown(); return false; }
	glUseProgram(extractProgram); glUniform1i(glGetUniformLocation(extractProgram,"source"),0);
	extractUniforms={glGetUniformLocation(extractProgram,"threshold"),glGetUniformLocation(extractProgram,"knee")};
	glUseProgram(blurProgram); glUniform1i(glGetUniformLocation(blurProgram,"source"),0);
	blurUniforms.direction=glGetUniformLocation(blurProgram,"direction");
	glUseProgram(compositeProgram);
	glUniform1i(glGetUniformLocation(compositeProgram,"sceneImage"),0);
	glUniform1i(glGetUniformLocation(compositeProgram,"bloomImage"),1);
	glUniform1i(glGetUniformLocation(compositeProgram,"softImage"),2);
	glUniform1i(glGetUniformLocation(compositeProgram,"haloImage"),3);
	compositeUniforms={glGetUniformLocation(compositeProgram,"enabled"),glGetUniformLocation(compositeProgram,"exposure"),
		glGetUniformLocation(compositeProgram,"bloomStrength"),glGetUniformLocation(compositeProgram,"vignetteStrength"),
		glGetUniformLocation(compositeProgram,"edgeStrength"),glGetUniformLocation(compositeProgram,"edgeRange"),
		glGetUniformLocation(compositeProgram,"fade")};
	glUseProgram(0);
	return true;
}

bool PostProcessing::Resize(int width,int height)
{
	if(!compositeProgram || width<1 || height<1) return false;
	GLint maxSize=0; glGetIntegerv(GL_MAX_TEXTURE_SIZE,&maxSize);
	if(width>maxSize || height>maxSize)
	{
		std::cerr<<"Post-processing size exceeds GL_MAX_TEXTURE_SIZE; retaining previous buffers.\n";
		return false;
	}
	if(buffers.scene.width==width && buffers.scene.height==height) return true;
	Buffers replacement;
	glActiveTexture(GL_TEXTURE0);
	const int halfW=(std::max)(1,(width+1)/2),halfH=(std::max)(1,(height+1)/2);
	const int quarterW=(std::max)(1,(halfW+1)/2),quarterH=(std::max)(1,(halfH+1)/2);
	bool ok=Allocate(replacement.scene,width,height,true) && Allocate(replacement.bright,halfW,halfH);
	for(int i=0;i<2 && ok;++i) ok=Allocate(replacement.bloom[i],halfW,halfH) &&
		Allocate(replacement.halo[i],quarterW,quarterH) && Allocate(replacement.soft[i],halfW,halfH);
	glBindFramebuffer(GL_FRAMEBUFFER,0); glBindTexture(GL_TEXTURE_2D,0);
	if(!ok)
	{
		Delete(replacement);
		std::cerr<<"Could not allocate HDR framebuffers; retaining previous buffers or direct rendering.\n";
		return false;
	}
	Delete(buffers); buffers=replacement;
	return true;
}

bool PostProcessing::Available() { return compositeProgram && buffers.scene.framebuffer; }

bool PostProcessing::BeginScene()
{
	if(!Available()) return false;
	Destination(buffers.scene);
	glDisable(GL_FRAMEBUFFER_SRGB);
	glClearColor(0,0,0,1); glClear(GL_COLOR_BUFFER_BIT);
	return true;
}

void PostProcessing::EndScene(int x,int y,int width,int height,float fade)
{
	if(!Available()) return;
	glDisable(GL_BLEND); glDisable(GL_DEPTH_TEST); glDisable(GL_FRAMEBUFFER_SRGB);
	glBindVertexArray(fullscreenVao);
	if(settings.enabled && settings.bloom)
	{
		Destination(buffers.bright); glUseProgram(extractProgram); BindTexture(buffers.scene.emissionTexture,0);
		glUniform1f(extractUniforms.threshold,Clamp(settings.bloomThreshold,0,20));
		glUniform1f(extractUniforms.knee,Clamp(settings.bloomKnee,.001f,5));
		glDrawArrays(GL_TRIANGLES,0,3);
		Blur(buffers.bright,buffers.bloom[0],1,0);
		Blur(buffers.bloom[0],buffers.bloom[1],0,1);
		// A compact core and a weaker, wide halo are generated from the same emission image.
		Blur(buffers.bloom[1],buffers.halo[0],4,0);
		Blur(buffers.halo[0],buffers.halo[1],0,2);
	}
	if(settings.enabled && settings.edgeBlur)
	{
		const float radius=Clamp(settings.edgeBlurRadius,0,12);
		// First pass downsamples from full resolution, hence twice the source-pixel step.
		Blur(buffers.scene,buffers.soft[0],radius*2,0);
		Blur(buffers.soft[0],buffers.soft[1],0,radius);
	}
	glBindFramebuffer(GL_FRAMEBUFFER,0); glViewport(x,y,width,height); glUseProgram(compositeProgram);
	BindTexture(buffers.scene.texture,0);
	BindTexture(settings.enabled && settings.bloom ? buffers.bloom[1].texture : buffers.scene.texture,1);
	BindTexture(settings.enabled && settings.edgeBlur ? buffers.soft[1].texture : buffers.scene.texture,2);
	BindTexture(settings.enabled && settings.bloom ? buffers.halo[1].texture : buffers.scene.texture,3);
	glUniform1i(compositeUniforms.enabled,settings.enabled ? 1 : 0);
	glUniform1f(compositeUniforms.exposure,Clamp(settings.exposure,.25f,4));
	glUniform1f(compositeUniforms.bloomStrength,settings.bloom ? Clamp(settings.bloomStrength,0,3) : 0);
	glUniform1f(compositeUniforms.vignetteStrength,settings.vignette ? Clamp(settings.vignetteStrength,0,.9f) : 0);
	glUniform1f(compositeUniforms.edgeStrength,settings.edgeBlur ? Clamp(settings.edgeBlurStrength,0,1) : 0);
	const float start=Clamp(settings.edgeStart,0,1.4f);
	glUniform2f(compositeUniforms.edgeRange,start,Clamp(settings.edgeEnd,start+.01f,2));
	glUniform1f(compositeUniforms.fade,Clamp(fade,0,1)); glDrawArrays(GL_TRIANGLES,0,3);
	for(int i=3;i>=0;--i) BindTexture(0,i);
	glBindVertexArray(0); glUseProgram(0);
	glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
}

void PostProcessing::Shutdown()
{
	glBindFramebuffer(GL_FRAMEBUFFER,0); Delete(buffers);
	if(fullscreenVao) glDeleteVertexArrays(1,&fullscreenVao);
	if(extractProgram) glDeleteProgram(extractProgram);
	if(blurProgram) glDeleteProgram(blurProgram);
	if(compositeProgram) glDeleteProgram(compositeProgram);
	fullscreenVao=extractProgram=blurProgram=compositeProgram=0;
}
