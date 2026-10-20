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

	// cutBelowY: descarta los triangulos que quedan ENTEROS por debajo de esa altura (en unidades del modelo).
	// Sirve para quitar bases/losas negras que traen algunos .glb.
	void LoadModel(const std::string& fileName, float cutBelowY = -1e30f);
	void RenderModel(GLuint colorLocation = 0);
	void ClearModel();

	// Caja envolvente en el espacio del modelo (ya con las transformaciones de nodos aplicadas)
	glm::vec3 GetBoundsMin() const { return boundsMin; }
	glm::vec3 GetBoundsMax() const { return boundsMax; }
	bool IsLoaded() const { return !MeshList.empty(); }

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
	float cutBelowY = -1e30f;
	glm::vec3 boundsMin = glm::vec3(1e30f);
	glm::vec3 boundsMax = glm::vec3(-1e30f);
};

