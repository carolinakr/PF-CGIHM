#pragma once
#include <vector>
#include<string>
#include<assimp/Importer.hpp>
#include<assimp/scene.h>
#include<assimp/postprocess.h>

#include <glm.hpp>
#include "Mesh.h"
#include "Texture.h"

class Model
{
public:
	Model();

	void LoadModel(const std::string& fileName);
	void RenderModel(GLuint colorLocation = 0);
	void ClearModel();

	~Model();

private:
	void LoadNode(aiNode* node, const aiScene* scene);
	void LoadMesh(aiMesh* mesh, const aiScene* scene);
	void LoadMaterials(const aiScene* scene);
	std::vector<Mesh*>MeshList;
	std::vector<Texture*>TextureList;
	std::vector<unsigned int>meshTotex;
	std::vector<glm::vec3>materialColors;
	std::string modelDir;
};

