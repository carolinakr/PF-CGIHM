#pragma once
#include<glew.h>
class Texture
{
public:
	Texture();
	Texture(const char* FileLoc);
	bool LoadTexture();
	bool LoadTextureA();
	bool LoadTextureFromMemory(const unsigned char* data, int dataLen);
	void UseTexture();
	int GetWidth() const { return width; }
	int GetHeight() const { return height; }
	void ClearTexture();
	~Texture();
private:
	GLuint textureID;
	int width, height, bitDepth;
	const char *fileLocation;

};

