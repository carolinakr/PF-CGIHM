#include "Model.h"
#include <unordered_map>
#include <vector>
#include <gtc\type_ptr.hpp>


Model::Model()
{
}

void Model::LoadModel(const std::string & fileName)
{
	Assimp::Importer importer;
	const aiScene *scene = importer.ReadFile(fileName, aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_JoinIdenticalVertices);
	if (!scene)
	{
		printf("Fall en cargar el modelo: %s \n", fileName.c_str());
		return;
	}
	size_t slashPos = fileName.find_last_of("/\\");
	modelDir = (slashPos != std::string::npos) ? fileName.substr(0, slashPos + 1) : "";
	LoadNode(scene->mRootNode, scene);
	LoadMaterials(scene);
}

void Model::ClearModel()
{
	for (unsigned int i = 0; i < MeshList.size(); i++)
	{
		if (MeshList[i])
		{
			delete MeshList[i];
			MeshList[i] = nullptr;

		}
	}

	for (unsigned int i = 0; i < TextureList.size(); i++)
	{
		if (TextureList[i])
		{
			delete TextureList[i];
			TextureList[i] = nullptr;
		}
	}

}

void Model::RenderModel(GLuint colorLocation)
{
	for (unsigned int i = 0; i < MeshList.size(); i++)
	{
		unsigned int materialIndex = meshTotex[i];
		if (materialIndex < TextureList.size() && TextureList[materialIndex])
		{
			TextureList[materialIndex]->UseTexture();
		}
		if (colorLocation != 0 && materialIndex < materialColors.size())
		{
			glUniform3fv(colorLocation, 1, glm::value_ptr(materialColors[materialIndex]));
		}
		MeshList[i]->RenderMesh();
	}
}


Model::~Model()
{
}

void Model::LoadNode(aiNode * node, const aiScene * scene)
{
	for (unsigned int i = 0; i <node->mNumMeshes; i++)
	{
		LoadMesh(scene->mMeshes[node->mMeshes[i]], scene);
	}
	for (unsigned int i = 0; i < node->mNumChildren; i++)
	{
		LoadNode(node->mChildren[i], scene);
	}
}

void Model::LoadMesh(aiMesh * mesh, const aiScene * scene)
{

	std::vector<GLfloat> vertices;
	std::vector<unsigned int> indices;
	for (unsigned int i = 0; i < mesh->mNumVertices; i++)
	{
		vertices.insert(vertices.end(), { mesh->mVertices[i].x,mesh->mVertices[i].y ,mesh->mVertices[i].z });
		//UV
		if (mesh->mTextureCoords[0])//si tiene coordenadas de texturizado
		{
			vertices.insert(vertices.end(), { mesh->mTextureCoords[0][i].x,mesh->mTextureCoords[0][i].y});
		}
		else
		{
			vertices.insert(vertices.end(), { 0.0f,0.0f });
		}
		//Normals importante, las normales son negativas porque la luz interact�a con ellas de esa forma, c�mo se vio con el dado/cubo
		
		vertices.insert(vertices.end(), { -mesh->mNormals[i].x,-mesh->mNormals[i].y ,-mesh->mNormals[i].z });
	}
	for (unsigned int i = 0; i < mesh->mNumFaces; i++)
	{
		aiFace face = mesh->mFaces[i];
		for (unsigned int j = 0; j < face.mNumIndices; j++)
		{
			indices.push_back(face.mIndices[j]);
		}
	}

	Mesh* newMesh = new Mesh();
	newMesh->CreateMesh(&vertices[0], &indices[0], vertices.size(), indices.size());
	MeshList.push_back(newMesh);
	meshTotex.push_back(mesh->mMaterialIndex);
}

void Model::LoadMaterials(const aiScene * scene)
{
	std::unordered_map<std::string, Texture*> loadedTextures;
	TextureList.resize(scene->mNumMaterials);
	materialColors.resize(scene->mNumMaterials, glm::vec3(1.0f));
	for (unsigned int i = 0; i < scene->mNumMaterials; i++)
	{
		aiMaterial* material = scene->mMaterials[i];
		TextureList[i] = nullptr;

		// Extraer color difuso del material
		aiColor4D diffuse;
		if (aiGetMaterialColor(material, AI_MATKEY_COLOR_DIFFUSE, &diffuse) == AI_SUCCESS)
			materialColors[i] = glm::vec3(diffuse.r, diffuse.g, diffuse.b);
		else if (aiGetMaterialColor(material, AI_MATKEY_BASE_COLOR, &diffuse) == AI_SUCCESS)
			materialColors[i] = glm::vec3(diffuse.r, diffuse.g, diffuse.b);

		aiTextureType texType = aiTextureType_NONE;
		if (material->GetTextureCount(aiTextureType_DIFFUSE))
			texType = aiTextureType_DIFFUSE;
		else if (material->GetTextureCount(aiTextureType_BASE_COLOR))
			texType = aiTextureType_BASE_COLOR;

		if (texType != aiTextureType_NONE)
		{
			aiString path;
			if (material->GetTexture(texType, 0, &path) == AI_SUCCESS)
			{
				std::string pathStr = std::string(path.data);

				// ── Textura embebida: Assimp usa "*N" como nombre ─────────
				if (pathStr.size() > 0 && pathStr[0] == '*')
				{
					int embIdx = std::atoi(pathStr.c_str() + 1);
					std::string embKey = "__embedded__" + std::to_string(embIdx);
					auto it = loadedTextures.find(embKey);
					if (it != loadedTextures.end())
					{
						TextureList[i] = it->second;
					}
					else if (scene->mNumTextures > (unsigned int)embIdx)
					{
						aiTexture* aitex = scene->mTextures[embIdx];
						Texture* newTex = new Texture("(embedded)");
						bool loaded = false;
						if (aitex->mHeight == 0)
						{
							// Formato comprimido (PNG/JPG): mWidth = tamanio en bytes
							loaded = newTex->LoadTextureFromMemory(
								reinterpret_cast<const unsigned char*>(aitex->pcData),
								(int)aitex->mWidth);
						}
						else
						{
							// ARGB8888 sin comprimir: convertir a RGBA
							int npix = (int)(aitex->mWidth * aitex->mHeight);
							std::vector<unsigned char> rgba(npix * 4);
							for (int p = 0; p < npix; p++)
							{
								rgba[p*4+0] = aitex->pcData[p].r;
								rgba[p*4+1] = aitex->pcData[p].g;
								rgba[p*4+2] = aitex->pcData[p].b;
								rgba[p*4+3] = aitex->pcData[p].a;
							}
							loaded = newTex->LoadTextureFromMemory(rgba.data(), (int)rgba.size());
						}
						if (loaded)
						{
							TextureList[i] = newTex;
							loadedTextures[embKey] = newTex;
						}
						else
						{
							printf("No se pudo cargar textura embebida %d\n", embIdx);
							delete newTex;
						}
					}
				}
				else
				{
					// ── Textura en archivo ────────────────────────────────
					std::string fullpath = pathStr;
					std::string filename;
					std::replace(fullpath.begin(), fullpath.end(), '\\', '/');
					size_t sepIdx = fullpath.rfind('/');
					filename = (sepIdx != std::string::npos) ? fullpath.substr(sepIdx + 1) : fullpath;
					if (filename.find('.') == std::string::npos)
						filename += ".png";

					std::vector<std::string> candidatos = {
						modelDir + fullpath,
						modelDir + filename,
						std::string("Textures/") + filename
					};

					auto it = loadedTextures.find(filename);
					if (it != loadedTextures.end())
					{
						TextureList[i] = it->second;
					}
					else
					{
						std::string ext = filename.substr(filename.find_last_of('.') + 1);
						bool loaded = false;
						for (auto& ruta : candidatos)
						{
							Texture* newTex = new Texture(ruta.c_str());
							if (ext == "tga" || ext == "png")
								loaded = newTex->LoadTextureA();
							else
								loaded = newTex->LoadTexture();
							if (loaded)
							{
								TextureList[i] = newTex;
								loadedTextures[filename] = newTex;
								break;
							}
							delete newTex;
						}
						if (!loaded)
							printf("No se encontro la Textura: %s\n", filename.c_str());
					}
				}
			}
		}
		if (!TextureList[i])
		{
			TextureList[i] = new Texture("Textures/plain.png");
			TextureList[i]->LoadTextureA();
		}
	}
}
