#include "Skybox.h"



Skybox::Skybox()
{
}

Skybox::Skybox(std::vector<std::string> faceLocations)
{
	skyShader = new Shader();
	skyShader->CreateFromFiles("shaders/skybox.vert", "shaders/skybox.frag");
	uniformProjection = skyShader->GetProjectionLocation();
	uniformView = skyShader->GetViewLocation();
	uniformTint = skyShader->GetUniformLocation("skyTint");
	tint = glm::vec3(1.0f, 1.0f, 1.0f);

	glGenTextures(1, &textureId);
	glBindTexture(GL_TEXTURE_CUBE_MAP, textureId);
	int width, height, bitDepth;
	// Las caras se guardan sin voltear (Texture.cpp activa el flip global de stb)
	stbi_set_flip_vertically_on_load(false);
	for (size_t i = 0; i < 6; i++)
	{
		unsigned char *texData = (i < faceLocations.size())
			? stbi_load(faceLocations[i].c_str(), &width, &height, &bitDepth, 0) : nullptr;
		if (!texData)
		{
			// Sin textura: cara de color solido (orden: +X -X -Y +Y +Z -Z) para que el
			// skybox y su tint (ciclo dia/noche) sigan funcionando.
			if (i < faceLocations.size())
				printf("No se encontro : %s\n", faceLocations[i].c_str());
			const unsigned char sky[3]    = { 150, 195, 240 };
			const unsigned char top[3]    = {  90, 150, 235 };
			const unsigned char ground[3] = {  90,  90,  90 };
			const unsigned char* c = (i == 3) ? top : (i == 2) ? ground : sky;
			glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
			glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB, 1, 1, 0, GL_RGB, GL_UNSIGNED_BYTE, c);
			continue;
		}
		GLenum fmt  = (bitDepth == 4) ? GL_RGBA : GL_RGB;
		GLenum ifmt = (bitDepth == 4) ? GL_RGBA : GL_RGB;
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, ifmt, width, height, 0, fmt, GL_UNSIGNED_BYTE, texData);
		stbi_image_free(texData);
	}
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	unsigned int skyboxIndices[] = {
		// front
		0, 1, 2,
		2, 1, 3,
		// right
		2, 3, 5,
		5, 3, 7,
		// back
		5, 7, 4,
		4, 7, 6,
		// left
		4, 6, 0,
		0, 6, 1,
		// top
		4, 0, 5,
		5, 0, 2,
		// bottom
		1, 6, 3,
		3, 6, 7
	};

	float skyboxVertices[] = {
		-1.0f,  1.0f, -1.0f,	0.0f, 0.0f,	0.0f, 0.0f, 0.0f,
		-1.0f, -1.0f, -1.0f,	0.0f, 0.0f,	0.0f, 0.0f, 0.0f,
		 1.0f,  1.0f, -1.0f,	0.0f, 0.0f,	0.0f, 0.0f, 0.0f,
		 1.0f, -1.0f, -1.0f,	0.0f, 0.0f,	0.0f, 0.0f, 0.0f,
		-1.0f,  1.0f,  1.0f,	0.0f, 0.0f,	0.0f, 0.0f, 0.0f,
		 1.0f,  1.0f,  1.0f,	0.0f, 0.0f,	0.0f, 0.0f, 0.0f,
		-1.0f, -1.0f,  1.0f,	0.0f, 0.0f,	0.0f, 0.0f, 0.0f,
		 1.0f, -1.0f,  1.0f,	0.0f, 0.0f,	0.0f, 0.0f, 0.0f
	};
	skyMesh = new Mesh();
	skyMesh->CreateMesh(skyboxVertices, skyboxIndices, 64, 36);
}

void Skybox::DrawSkybox(glm::mat4 viewMatrix, glm::mat4 projectionMatrix)
{
	viewMatrix = glm::mat4(glm::mat3(viewMatrix));

	glDepthMask(false);
	skyShader->UseShader();
	glUniformMatrix4fv(uniformProjection, 1, GL_FALSE, glm::value_ptr(projectionMatrix));
	glUniformMatrix4fv(uniformView, 1, GL_FALSE, glm::value_ptr(viewMatrix));
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_CUBE_MAP, textureId);
	glUniform3f(uniformTint, tint.r, tint.g, tint.b);
	skyMesh->RenderMesh();
	glDepthMask(true);
}

void Skybox::SetTint(GLfloat r, GLfloat g, GLfloat b)
{
	tint = glm::vec3(r, g, b);
}

Skybox::~Skybox()
{
}
