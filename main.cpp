/*
Proyecto Final - CGIHM
Gion Matsuri: noches de Yoiyama (Equipo 14). Recorrido nocturno por una calle de Kioto.
Base: motor + logica de ProyectoTeoria (camara, luces, skybox, keyframes, audio).

Controles:
  W A S D + mouse : caminar (limitado a la calle, a la altura de los ojos)
  1 - 3           : puntos de vista (entrada / centro / fondo)
  V               : camara libre (sin limites)
  L               : linterna
  M               : muestra/oculta los numeros de los modelos y el muestrario de texturas
  ESC             : salir
*/

// stb_image: la implementacion se define solo aqui (los demas .cpp solo ven declaraciones)
#define STB_IMAGE_IMPLEMENTATION

#include <stdio.h>
#include <string.h>
#include <cmath>
#include <vector>
#include <string>
#include <math.h>
#include <random>
#include <map>
#include <functional>
#include <algorithm>
#include <filesystem>

#include "miniaudio.h"

#include <glew.h>
#include <glfw3.h>

#include <glm.hpp>
#include <gtc\matrix_transform.hpp>
#include <gtc\type_ptr.hpp>

#include "Window.h"
#include "Mesh.h"
#include "Shader_light.h"
#include "Camera.h"
#include "Texture.h"
#include "Sphere.h"
#include "Model.h"
#include "Skybox.h"

#include "CommonValues.h"
#include "DirectionalLight.h"
#include "PointLight.h"
#include "SpotLight.h"
#include "Material.h"

const float toRadians = 3.14159265f / 180.0f;
const float PI_F      = 3.14159265f;
const float STREET_HALF_W = 8.0f;     // media anchura de la calle: 16 m en total (las posiciones laterales dependen de esto)

// ============================================================
//  ESTADO GLOBAL
// ============================================================
Window mainWindow;
std::vector<Mesh*> meshList;
std::vector<Shader> shaderList;
Camera camera;

GLfloat deltaTime = 0.0f;
GLfloat lastTime  = 0.0f;

// Texturas (si el archivo no existe, Texture usa una textura blanca 1x1 de reemplazo)
Texture plainTexture;
Texture pisoTexture;

Skybox skybox;

Material Material_brillante;
Material Material_opaco;

DirectionalLight mainLight;
PointLight pointLights[MAX_POINT_LIGHTS];
SpotLight  spotLights[MAX_SPOT_LIGHTS];

static const char* vShader = "shaders/shader_light.vert";
static const char* fShader = "shaders/shader_light.frag";

ma_engine audioEngine;

// Indices de meshes procedurales (se asignan en CreateObjects)
int MESH_CAJA     = -1; // caja unitaria [0,1]^3
int MESH_CILINDRO = -1; // cilindro radio 1, altura 1 (solo lateral)
int MESH_PISO     = -1; // quad horizontal con UVs repetidas
int MESH_PIRAMIDE = -1; // piramide con normales promediadas
int MESH_SUELO   = -1; // suelo grande alrededor de la calle
int MESH_RUEDA   = -1; // rueda de madera (radio 1, eje X)
int MESH_TINA    = -1; // pared de la pileta (duelas de madera)
int MESH_DISCO   = -1; // disco horizontal (fondo y agua de la pileta)
int MESH_ARO     = -1; // aro de la red (poi), radio 1 en el plano XZ
int MESH_TAZON   = -1; // tazon donde caen los peces
// Atlas Textures/UI/hud_digitos.png (1000 x 120): los glifos no estan en una rejilla regular, se miden uno por uno
const int DIG_X0[10] = {   9, 106, 185, 288, 395, 504, 610, 716, 807, 912 };
const int DIG_X1[10] = {  89, 163, 265, 372, 481, 585, 688, 791, 890, 994 };
int MESH_DIG[10];
float digitAspect[10];   // ancho / alto de cada glifo
int MESH_QUAD     = -1; // quad vertical unitario [0,1] (flyers/billboards)

// ============================================================
//  UTILIDADES
// ============================================================

// Deteccion de flanco de subida: true una sola vez por pulsacion
// Detecta el momento en que una tecla pasa de estar liberada a presionada; evita repetir acciones mientras se mantiene pulsada.
bool KeyPressedOnce(int key)
{
	static bool prev[1024] = { false };
	bool* keys = mainWindow.getsKeys();
	bool pressed = keys[key] && !prev[key];
	prev[key] = keys[key];
	return pressed;
}

// Frustum de la camara: seis planos sacados de proyeccion * vista. Un objeto (esfera) fuera de la vista no se dibuja.
struct Frustum { glm::vec4 pl[6]; };
// Construye los seis planos del volumen de visión de la cámara a partir de la matriz de proyección por vista.
Frustum MakeFrustum(const glm::mat4& vp)
{
	Frustum f;
	glm::vec4 r0(vp[0][0], vp[1][0], vp[2][0], vp[3][0]), r1(vp[0][1], vp[1][1], vp[2][1], vp[3][1]);
	glm::vec4 r2(vp[0][2], vp[1][2], vp[2][2], vp[3][2]), r3(vp[0][3], vp[1][3], vp[2][3], vp[3][3]);
	f.pl[0] = r3 + r0; f.pl[1] = r3 - r0; f.pl[2] = r3 + r1; f.pl[3] = r3 - r1; f.pl[4] = r3 + r2; f.pl[5] = r3 - r2;
	for (auto& q : f.pl) q /= glm::length(glm::vec3(q));
	return f;
}
// Comprueba si una esfera delimitadora está dentro del campo de visión; permite omitir objetos que no se ven.
bool Visible(const Frustum& f, const glm::vec3& c, float r)
{
	for (const auto& q : f.pl) if (q.x * c.x + q.y * c.y + q.z * c.z + q.w < -r) return false;
	return true;
}

// Promedio de normales para sombreado suave (Phong).
// vLength = floats por vertice (8: pos3 + uv2 + normal3), normalOffset = 5.
// Calcula y promedia las normales de los vértices para conseguir un sombreado más suave en las mallas.
void calcAverageNormals(unsigned int* indices, unsigned int indiceCount, GLfloat* vertices, unsigned int verticeCount,
	unsigned int vLength, unsigned int normalOffset)
{
	for (size_t i = 0; i < indiceCount; i += 3)
	{
		unsigned int in0 = indices[i] * vLength;
		unsigned int in1 = indices[i + 1] * vLength;
		unsigned int in2 = indices[i + 2] * vLength;
		glm::vec3 v1(vertices[in1] - vertices[in0], vertices[in1 + 1] - vertices[in0 + 1], vertices[in1 + 2] - vertices[in0 + 2]);
		glm::vec3 v2(vertices[in2] - vertices[in0], vertices[in2 + 1] - vertices[in0 + 1], vertices[in2 + 2] - vertices[in0 + 2]);
		glm::vec3 normal = glm::normalize(glm::cross(v1, v2));

		in0 += normalOffset; in1 += normalOffset; in2 += normalOffset;
		vertices[in0] += normal.x; vertices[in0 + 1] += normal.y; vertices[in0 + 2] += normal.z;
		vertices[in1] += normal.x; vertices[in1 + 1] += normal.y; vertices[in1 + 2] += normal.z;
		vertices[in2] += normal.x; vertices[in2 + 1] += normal.y; vertices[in2 + 2] += normal.z;
	}

	for (size_t i = 0; i < verticeCount / vLength; i++)
	{
		unsigned int nOffset = i * vLength + normalOffset;
		glm::vec3 vec = glm::normalize(glm::vec3(vertices[nOffset], vertices[nOffset + 1], vertices[nOffset + 2]));
		vertices[nOffset] = vec.x; vertices[nOffset + 1] = vec.y; vertices[nOffset + 2] = vec.z;
	}
}

// Offset de UV para una hoja de sprites (spritesheet) de cols x rows.
// Reemplaza la tabla if/else de los numeros: indice 0 = arriba-izquierda, avanza por filas.
// Se manda al shader en el uniform "toffset" (el vertex shader hace TexCoord = tex + toffset).
// Obtiene el desplazamiento UV de una celda de la hoja de sprites según su índice, número de columnas y filas.
glm::vec2 SpriteSheetOffset(int index, int cols, int rows)
{
	return glm::vec2((index % cols) / (float)cols, -(index / cols) / (float)rows);
}

// ============================================================
//  GEOMETRIA PROCEDURAL  (vertice = x y z  u v  nx ny nz)
// ============================================================

// Cilindro: solo cuerpo lateral, radio 1, altura 1 (escalar con glm::scale)
// Genera un cilindro procedural mediante segmentos alrededor de su eje y lo registra en la lista de mallas.
int MakeCylinder(int sectors)
{
	std::vector<GLfloat> verts;
	std::vector<unsigned int> inds;

	for (int i = 0; i < sectors; i++) {
		float a0 = 2.0f * PI_F * i / sectors;
		float a1 = 2.0f * PI_F * (i + 1) / sectors;
		float x0 = cosf(a0), z0 = sinf(a0);
		float x1 = cosf(a1), z1 = sinf(a1);
		float u0 = (float)i / sectors, u1 = (float)(i + 1) / sectors;
		verts.insert(verts.end(), { x0, 0.0f, z0,  u0, 0.0f,  -x0, 0.0f, -z0 });
		verts.insert(verts.end(), { x1, 0.0f, z1,  u1, 0.0f,  -x1, 0.0f, -z1 });
		verts.insert(verts.end(), { x1, 1.0f, z1,  u1, 1.0f,  -x1, 0.0f, -z1 });
		verts.insert(verts.end(), { x0, 1.0f, z0,  u0, 1.0f,  -x0, 0.0f, -z0 });
		unsigned b = i * 4;
		inds.insert(inds.end(), { b, b + 2, b + 1,  b, b + 3, b + 2 });
	}

	Mesh* m = new Mesh();
	m->CreateMesh(verts.data(), inds.data(), (unsigned)verts.size(), (unsigned)inds.size());
	meshList.push_back(m);
	return (int)meshList.size() - 1;
}

// Caja unitaria [0,1]^3 con normales por cara
// Construye una caja unitaria con vértices, coordenadas UV y normales independientes para cada cara.
int MakeBox()
{
	GLfloat v[] = {
		// frontal (z=1)
		0,0,1,  0,0,  0,0,1,
		1,0,1,  1,0,  0,0,1,
		1,1,1,  1,1,  0,0,1,
		0,1,1,  0,1,  0,0,1,
		// trasera (z=0)
		1,0,0,  0,0,  0,0,-1,
		0,0,0,  1,0,  0,0,-1,
		0,1,0,  1,1,  0,0,-1,
		1,1,0,  0,1,  0,0,-1,
		// izquierda (x=0)
		0,0,0,  0,0,  -1,0,0,
		0,0,1,  1,0,  -1,0,0,
		0,1,1,  1,1,  -1,0,0,
		0,1,0,  0,1,  -1,0,0,
		// derecha (x=1)
		1,0,1,  0,0,  1,0,0,
		1,0,0,  1,0,  1,0,0,
		1,1,0,  1,1,  1,0,0,
		1,1,1,  0,1,  1,0,0,
		// abajo (y=0)
		0,0,0,  0,0,  0,-1,0,
		1,0,0,  1,0,  0,-1,0,
		1,0,1,  1,1,  0,-1,0,
		0,0,1,  0,1,  0,-1,0,
		// arriba (y=1)
		0,1,1,  0,0,  0,1,0,
		1,1,1,  1,0,  0,1,0,
		1,1,0,  1,1,  0,1,0,
		0,1,0,  0,1,  0,1,0,
	};
	// Convencion del shader: la normal apunta hacia ADENTRO de la superficie (igual que Model::LoadMesh)
	for (int i = 0; i < 24; i++) { v[i * 8 + 5] = -v[i * 8 + 5]; v[i * 8 + 6] = -v[i * 8 + 6]; v[i * 8 + 7] = -v[i * 8 + 7]; }
	unsigned int idx[] = {
		 0, 1, 2,  0, 2, 3,
		 4, 5, 6,  4, 6, 7,
		 8, 9,10,  8,10,11,
		12,13,14, 12,14,15,
		16,17,18, 16,18,19,
		20,21,22, 20,22,23,
	};
	Mesh* m = new Mesh();
	m->CreateMesh(v, idx, 24 * 8, 36);
	meshList.push_back(m);
	return (int)meshList.size() - 1;
}

// Agrega una caja unitaria centrada en el origen, transformada por M, a una malla en construccion.
// Normales hacia adentro (convencion del shader), igual que MakeBox.
// Añade una caja transformada a una malla existente; se utiliza para construir objetos compuestos como ruedas y aros.
void AppendBox(std::vector<GLfloat>& verts, std::vector<unsigned int>& inds, const glm::mat4& M)
{
	static const glm::vec3 N[6] = { {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1} };
	static const glm::vec3 A[6] = { {0,1,0}, {0,0,1},  {0,0,1}, {1,0,0},  {1,0,0}, {0,1,0} };
	static const glm::vec3 B[6] = { {0,0,1}, {0,1,0},  {1,0,0}, {0,0,1},  {0,1,0}, {1,0,0} };
	glm::mat3 nm = glm::mat3(glm::transpose(glm::inverse(M)));
	for (int f = 0; f < 6; f++) {
		unsigned int base = (unsigned int)(verts.size() / 8);
		const glm::vec3 c[4] = {
			N[f] * 0.5f - A[f] * 0.5f - B[f] * 0.5f, N[f] * 0.5f + A[f] * 0.5f - B[f] * 0.5f,
			N[f] * 0.5f + A[f] * 0.5f + B[f] * 0.5f, N[f] * 0.5f - A[f] * 0.5f + B[f] * 0.5f };
		const float uv[4][2] = { {0, 0}, {1, 0}, {1, 1}, {0, 1} };
		glm::vec3 n = glm::normalize(nm * (-N[f]));
		for (int k = 0; k < 4; k++) {
			glm::vec3 p = glm::vec3(M * glm::vec4(c[k], 1.0f));
			verts.insert(verts.end(), { p.x, p.y, p.z, uv[k][0], uv[k][1], n.x, n.y, n.z });
		}
		inds.insert(inds.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
	}
}

// Rueda de madera de radio 1 con el eje a lo largo de X: llanta de 24 tramos, 12 radios y un cubo.
// Construye una rueda de madera combinando segmentos para la llanta, radios y cubo central.
int MakeWheel()
{
	std::vector<GLfloat> v;
	std::vector<unsigned int> idx;
	const float TAU = 2.0f * PI_F;
	const glm::vec3 X(1.0f, 0.0f, 0.0f);

	const int RIM = 24;                      // llanta
	for (int k = 0; k < RIM; k++) {
		float ph = TAU * k / RIM;
		glm::mat4 M = glm::translate(glm::mat4(1.0f), 0.93f * glm::vec3(0.0f, cosf(ph), sinf(ph)));
		M = glm::rotate(M, ph, X);
		M = glm::scale(M, glm::vec3(0.34f, 0.15f, 0.26f));   // ancho, grosor radial, largo del tramo
		AppendBox(v, idx, M);
	}
	const int SPOKES = 12;                   // radios
	for (int k = 0; k < SPOKES; k++) {
		float ph = TAU * k / SPOKES + TAU / 48.0f;
		glm::mat4 M = glm::translate(glm::mat4(1.0f), 0.46f * glm::vec3(0.0f, cosf(ph), sinf(ph)));
		M = glm::rotate(M, ph, X);
		M = glm::scale(M, glm::vec3(0.13f, 0.80f, 0.10f));
		AppendBox(v, idx, M);
	}
	for (int k = 0; k < 3; k++) {            // cubo: tres prismas girados 30 grados = casi un cilindro
		glm::mat4 M = glm::rotate(glm::mat4(1.0f), glm::radians(30.0f * k), X);
		M = glm::scale(M, glm::vec3(0.50f, 0.30f, 0.30f));
		AppendBox(v, idx, M);
	}

	Mesh* m = new Mesh();
	m->CreateMesh(v.data(), idx.data(), (unsigned)v.size(), (unsigned)idx.size());
	meshList.push_back(m);
	return (int)meshList.size() - 1;
}

// Pared de una tina de duelas de madera: 'staves' cajas alrededor de un circulo (radio interior 'radius').
// Genera una tina circular a partir de duelas o piezas de madera distribuidas alrededor de un círculo.
int MakeTub(float radius, float height, int staves, float thick)
{
	std::vector<GLfloat> v;
	std::vector<unsigned int> idx;
	const float TAU = 2.0f * PI_F;
	float w = TAU * radius / staves * 1.05f;
	for (int k = 0; k < staves; k++) {
		float phi = TAU * k / staves;
		float rc = radius + thick * 0.5f;
		glm::mat4 M = glm::translate(glm::mat4(1.0f), glm::vec3(rc * cosf(phi), height * 0.5f, rc * sinf(phi)));
		M = glm::rotate(M, PI_F * 0.5f - phi, glm::vec3(0.0f, 1.0f, 0.0f));   // +Z local = hacia afuera
		M = glm::scale(M, glm::vec3(w, height, thick));
		AppendBox(v, idx, M);
	}
	Mesh* m = new Mesh();
	m->CreateMesh(v.data(), idx.data(), (unsigned)v.size(), (unsigned)idx.size());
	meshList.push_back(m);
	return (int)meshList.size() - 1;
}

// Disco horizontal en y = 0 con 'rings' anillos (para que las ondas del shader se vean) y UV 0..1
// Crea un disco horizontal dividido en anillos y sectores, útil para superficies como el agua de la pileta.
int MakeDisc(float radius, int rings, int sectors)
{
	std::vector<GLfloat> v;
	std::vector<unsigned int> idx;
	for (int r = 0; r <= rings; r++)
		for (int s = 0; s < sectors; s++) {
			float rr = radius * r / rings, a = 2.0f * PI_F * s / sectors;
			float x = rr * cosf(a), z = rr * sinf(a);
			v.insert(v.end(), { x, 0.0f, z, 0.5f + 0.5f * x / radius, 0.5f + 0.5f * z / radius, 0.0f, -1.0f, 0.0f });
		}
	for (int r = 0; r < rings; r++)
		for (int s = 0; s < sectors; s++) {
			unsigned int a = r * sectors + s, b = r * sectors + (s + 1) % sectors;
			unsigned int c = (r + 1) * sectors + s, d = (r + 1) * sectors + (s + 1) % sectors;
			idx.insert(idx.end(), { a, c, b, b, c, d });
		}
	Mesh* m = new Mesh();
	m->CreateMesh(v.data(), idx.data(), (unsigned)v.size(), (unsigned)idx.size());
	meshList.push_back(m);
	return (int)meshList.size() - 1;
}

// Aro de radio 1 en el plano XZ hecho con 'segs' cajas (el grosor 'thick' esta en unidades de radio)
// Construye un aro en el plano XZ utilizando pequeñas cajas distribuidas alrededor de una circunferencia.
int MakeRing(int segs, float thick)
{
	std::vector<GLfloat> v;
	std::vector<unsigned int> idx;
	const float TAU = 2.0f * PI_F;
	float w = TAU / segs * 1.1f;
	for (int k = 0; k < segs; k++) {
		float phi = TAU * k / segs;
		glm::mat4 M = glm::translate(glm::mat4(1.0f), glm::vec3(cosf(phi), 0.0f, sinf(phi)));
		M = glm::rotate(M, PI_F * 0.5f - phi, glm::vec3(0.0f, 1.0f, 0.0f));
		M = glm::scale(M, glm::vec3(w, thick, thick));
		AppendBox(v, idx, M);
	}
	Mesh* m = new Mesh();
	m->CreateMesh(v.data(), idx.data(), (unsigned)v.size(), (unsigned)idx.size());
	meshList.push_back(m);
	return (int)meshList.size() - 1;
}

// Crea y registra las mallas procedurales que se reutilizan durante el armado y dibujo de la escena.
void CreateObjects()
{
	MESH_CAJA     = MakeBox();
	MESH_CILINDRO = MakeCylinder(18);
	MESH_RUEDA    = MakeWheel();
	MESH_TINA     = MakeTub(0.70f, 0.30f, 40, 0.045f);
	MESH_DISCO    = MakeDisc(0.70f, 12, 40);
	MESH_ARO      = MakeRing(24, 0.10f);
	MESH_TAZON    = MakeTub(0.20f, 0.13f, 24, 0.02f);
	for (int d = 0; d < 10; d++) {   // quad de cada digito con su rectangulo de UV exacto
		float u0 = (DIG_X0[d] - 2) / 1000.0f, u1 = (DIG_X1[d] + 3) / 1000.0f;
		float v0 = 1.0f - 96.0f / 120.0f, v1 = 1.0f - 16.0f / 120.0f;
		unsigned int idx[] = { 0, 1, 2, 0, 2, 3 };
		GLfloat v[] = { 0.0f, 0.0f, 0.0f, u0, v0, 0.0f, 0.0f, -1.0f,
		                1.0f, 0.0f, 0.0f, u1, v0, 0.0f, 0.0f, -1.0f,
		                1.0f, 1.0f, 0.0f, u1, v1, 0.0f, 0.0f, -1.0f,
		                0.0f, 1.0f, 0.0f, u0, v1, 0.0f, 0.0f, -1.0f };
		Mesh* m = new Mesh();
		m->CreateMesh(v, idx, 32, 6);
		meshList.push_back(m);
		MESH_DIG[d] = (int)meshList.size() - 1;
		digitAspect[d] = (DIG_X1[d] - DIG_X0[d] + 1 + 5) / 80.0f;
	}

	// Piso de la calle: 10 m (X) x 60 m (Z); UV = metros / STONE_TILE (1 repeticion cada 2 m)
	{
		const float W = STREET_HALF_W, Z0 = -30.0f, Z1 = 30.0f, STONE_TILE = 2.0f;
		unsigned int idx[] = { 0, 2, 1,  1, 2, 3 };
		GLfloat v[] = {
			// x    y     z      u                     v                        nx    ny    nz
			-W, 0.0f, Z0,   0.0f,                 0.0f,                    0.0f, -1.0f, 0.0f,
			 W, 0.0f, Z0,   2 * W / STONE_TILE,   0.0f,                    0.0f, -1.0f, 0.0f,
			-W, 0.0f, Z1,   0.0f,                 (Z1 - Z0) / STONE_TILE, 0.0f, -1.0f, 0.0f,
			 W, 0.0f, Z1,   2 * W / STONE_TILE,   (Z1 - Z0) / STONE_TILE, 0.0f, -1.0f, 0.0f,
		};
		Mesh* m = new Mesh();
		m->CreateMesh(v, idx, 32, 6);
		meshList.push_back(m);
		MESH_PISO = (int)meshList.size() - 1;
	}

	// Suelo grande alrededor de la calle (no transitable): 300 x 300 m, 1 repeticion cada 2 m
	{
		const float R = 150.0f, TILE = 2.0f;
		unsigned int idx[] = { 0, 2, 1,  1, 2, 3 };
		GLfloat v[] = {
			-R, 0.0f, -R,   0.0f,          0.0f,          0.0f, -1.0f, 0.0f,
			 R, 0.0f, -R,   2 * R / TILE,  0.0f,          0.0f, -1.0f, 0.0f,
			-R, 0.0f,  R,   0.0f,          2 * R / TILE,  0.0f, -1.0f, 0.0f,
			 R, 0.0f,  R,   2 * R / TILE,  2 * R / TILE,  0.0f, -1.0f, 0.0f,
		};
		Mesh* m = new Mesh();
		m->CreateMesh(v, idx, 32, 6);
		meshList.push_back(m);
		MESH_SUELO = (int)meshList.size() - 1;
	}

	// Piramide: normales en 0 y luego promediadas con calcAverageNormals
	{
		unsigned int idx[] = { 0, 3, 1,  1, 3, 2,  2, 3, 0,  0, 1, 2 };
		GLfloat v[] = {
			-1.0f, -1.0f, -0.6f,   0.0f, 0.0f,   0.0f, 0.0f, 0.0f,
			 0.0f, -1.0f,  1.0f,   0.5f, 0.0f,   0.0f, 0.0f, 0.0f,
			 1.0f, -1.0f, -0.6f,   1.0f, 0.0f,   0.0f, 0.0f, 0.0f,
			 0.0f,  1.0f,  0.0f,   0.5f, 1.0f,   0.0f, 0.0f, 0.0f,
		};
		calcAverageNormals(idx, 12, v, 32, 8, 5);
		Mesh* m = new Mesh();
		m->CreateMesh(v, idx, 32, 12);
		meshList.push_back(m);
		MESH_PIRAMIDE = (int)meshList.size() - 1;
	}

	// Quad vertical unitario [0,1] mirando a +Z (flyers, billboards, hojas de sprites)
	{
		unsigned int idx[] = { 0, 1, 2,  0, 2, 3 };
		GLfloat v[] = {
			0.0f, 0.0f, 0.0f,  0.0f, 0.0f,  0.0f, 0.0f, -1.0f,
			1.0f, 0.0f, 0.0f,  1.0f, 0.0f,  0.0f, 0.0f, -1.0f,
			1.0f, 1.0f, 0.0f,  1.0f, 1.0f,  0.0f, 0.0f, -1.0f,
			0.0f, 1.0f, 0.0f,  0.0f, 1.0f,  0.0f, 0.0f, -1.0f,
		};
		Mesh* m = new Mesh();
		m->CreateMesh(v, idx, 32, 6);
		meshList.push_back(m);
		MESH_QUAD = (int)meshList.size() - 1;
	}
}

// Carga los shaders principales y los almacena en la lista que utiliza el renderizado.
void CreateShaders()
{
	Shader* shader1 = new Shader();
	shader1->CreateFromFiles(vShader, fShader);
	shaderList.push_back(*shader1);
}

// ============================================================
//  ANIMACION POR KEYFRAMES (interpolacion lineal por pasos fijos)
//
//  Mismo algoritmo que dron/paloma/profesor de ProyectoTeoria, pero reutilizable:
//    - kf[i]      : valores de las N variables en el keyframe i
//    - steps[i]   : cuantos frames (pasos) tarda el segmento i -> i+1
//    - el ultimo keyframe debe ser igual al primero para cerrar el loop limpio
//    - Step() se llama una vez por frame; value[] se lee al renderizar
//  Al terminar el ultimo segmento se fuerza value = kf[0] (evita error acumulado).
//  Es por frames (no por deltaTime): la velocidad depende del FPS, como en las practicas.
// ============================================================
class KeyframePath
{
public:
	KeyframePath() : idx(0), cur(0) {}

	KeyframePath(const std::vector<std::vector<float>>& keyframes, const std::vector<int>& stepsPerSeg)
		: kf(keyframes), steps(stepsPerSeg), idx(0), cur(0)
	{
		value = kf[0];
		inc.assign(kf[0].size(), 0.0f);
		Interpolate();
	}

	void Step()
	{
		if (kf.empty()) return;
		if (cur >= steps[idx]) {
			idx++;
			if (idx >= (int)kf.size() - 1) {
				idx = 0;
				value = kf[0];
			}
			cur = 0;
			Interpolate();
		}
		else {
			for (size_t i = 0; i < value.size(); i++)
				value[i] += inc[i];
			cur++;
		}
	}

	float operator[](int i) const { return value[i]; }
	int Segment() const { return idx; }   // segmento actual (cuadro clave de partida)

private:
	void Interpolate()
	{
		for (size_t i = 0; i < value.size(); i++)
			inc[i] = (kf[idx + 1][i] - kf[idx][i]) / steps[idx];
	}

	std::vector<std::vector<float>> kf;
	std::vector<int> steps;
	std::vector<float> value, inc;
	int idx, cur;
};

// ============================================================
//  CALLE DE YOIYAMA  (1 unidad = 1 m)
//  Calle peatonal de 10 m x 60 m a lo largo de Z; el usuario entra por +Z mirando a -Z.
// ============================================================
const float STREET_Z_MIN  = -30.0f;   // fondo (fuegos artificiales)
const float STREET_Z_MAX  =  30.0f;   // entrada (torii)
const float EYE_HEIGHT    = 1.7f;
const float WALK_MARGIN   = 0.4f;     // distancia minima a las fachadas / extremos

// Camara: posiciones predefinidas (teclas 1-3). Camera(pos, up, yaw, pitch, moveSpeed, turnSpeed)
// yaw: -90 mira a -Z, 90 a +Z, 0 a +X, 180 a -X
struct CamPreset { glm::vec3 pos; float yaw, pitch; };
const CamPreset CAM_PRESETS[3] = {
	{ glm::vec3(0.0f, EYE_HEIGHT, 28.0f),  -90.0f, 0.0f },  // 1: entrada
	{ glm::vec3(0.0f, EYE_HEIGHT,  0.0f),  -90.0f, 0.0f },  // 2: centro de la calle
	{ glm::vec3(0.0f, EYE_HEIGHT, -27.0f),  90.0f, 0.0f },  // 3: fondo, mirando a la entrada
};
const float CAM_MOVE_SPEED = 3.5f;
const float CAM_TURN_SPEED = 0.25f;
float moonAmbientOverride = -1.0f; // >= 0 reemplaza MOON_AMBIENT (capturas aereas)
float fogDensityOverride = -1.0f; // >= 0 reemplaza FOG_DENSITY (usado por las capturas aereas)
bool freeCamera = false; // tecla V: camara libre (sin limites de calle ni altura fija)

// Aplica una de las posiciones de cámara predefinidas para recorrer la entrada, el centro o el fondo de la calle.
void SetCameraPreset(const CamPreset& p)
{
	camera = Camera(p.pos, glm::vec3(0.0f, 1.0f, 0.0f), p.yaw, p.pitch, CAM_MOVE_SPEED, CAM_TURN_SPEED);
}

// Mantiene al usuario dentro de la calle y a la altura de los ojos
// Limita el movimiento de la cámara a los límites de la calle y mantiene la altura de los ojos, salvo en modo libre.
void ConstrainCamera()
{
	if (freeCamera) return;
	glm::vec3 p = camera.getCameraPosition();
	p.x = glm::clamp(p.x, -STREET_HALF_W + WALK_MARGIN, STREET_HALF_W - WALK_MARGIN);
	p.z = glm::clamp(p.z, STREET_Z_MIN + WALK_MARGIN, STREET_Z_MAX - WALK_MARGIN);
	p.y = EYE_HEIGHT;
	camera.setPosition(p);
}

// ============================================================
//  AMBIENTE NOCTURNO  (el Yoiyama es de noche: no hay ciclo dia/noche)
//  Skybox nocturno fijo + luz direccional de luna azul tenue; la escena se ilumina con los faroles.
// ============================================================
const glm::vec3 MOON_COLOR(0.42f, 0.52f, 0.95f);
const glm::vec3 MOON_DIR(-0.30f, -0.80f, -0.50f);   // direccion en la que viaja la luz
const float     MOON_AMBIENT = 0.20f;
const float     MOON_DIFFUSE = 0.16f;
const glm::vec3 SKY_TINT(1.0f, 1.0f, 1.15f);

// Niebla exponencial: f = exp(-densidad * distancia). El color coincide con el horizonte del skybox.
const float     FOG_DENSITY = 0.014f;
const glm::vec3 FOG_COLOR(0.035f, 0.040f, 0.070f);

// ============================================================
//  FAROLES CHOCHIN  (animacion procedural)
//  60 faroles en 12 cuerdas cruzando la calle. Cada uno pendula con
//      theta(t) = 4 deg * sin(1.6 t + phi_i)      (phi_i aleatoria: no se mueven al unisono)
//  y su luz parpadea con  I(t) = I0 * (0.92 + 0.08 * ruido(t)).
//  Matriz de modelo: T(anclaje) * R(theta) * T(-L).
//  Solo los MAX_POINT_LIGHTS (8) mas cercanos a la camara se envian al shader.
// ============================================================
struct Lantern {
	glm::vec3 anchor;   // punto de la cuerda de donde cuelga
	float phase;        // phi_i
	float axisAngle;    // direccion (en XZ) del eje de giro
	float scale = 1.0f; // tamano relativo (los faroles del templo son mas grandes)
};
std::vector<Lantern> lanterns;

const int   ROPE_COUNT        = 12;
const int   LANTERNS_PER_ROPE = 7;
const float ROPE_HEIGHT       = 6.2f;   // sobre los carros (5.1 m)
const float LANTERN_CORD_L    = 0.30f;  // L: cuerda hasta el farol
const float LANTERN_BODY_H    = 0.45f;
const float LANTERN_BODY_R    = 0.22f;
const float LANTERN_SWING_DEG = 4.0f;
const float LANTERN_SWING_W   = 1.6f;
const glm::vec3 LANTERN_COLOR(1.0f, 0.62f, 0.30f);

// Ruido de valor 1D suave en [-1, 1]
// Genera un valor pseudoaleatorio determinista a partir de un entero; sirve como base para el ruido de las luces.
static float Hash1D(int n)
{
	unsigned int x = (unsigned int)n;
	x = (x << 13) ^ x;
	x = x * (x * x * 15731u + 789221u) + 1376312589u;
	return 1.0f - (float)(x & 0x7fffffffu) / 1073741824.0f;
}
// Suaviza la transición entre valores pseudoaleatorios para producir variaciones continuas de intensidad.
float Noise1D(float x)
{
	int i = (int)floorf(x);
	float f = x - (float)i;
	f = f * f * (3.0f - 2.0f * f);
	return Hash1D(i) * (1.0f - f) + Hash1D(i + 1) * f;
}

// Distribuye los faroles en las cuerdas que cruzan la calle y asigna fases aleatorias reproducibles.
void InitLanterns()
{
	std::mt19937 rng(14); // semilla fija: la misma calle en cada ejecucion
	std::uniform_real_distribution<float> rnd(0.0f, 2.0f * PI_F);
	const float step = (STREET_Z_MAX - STREET_Z_MIN - 2.0f) / (ROPE_COUNT - 1);
	for (int r = 0; r < ROPE_COUNT; r++) {
		float z = STREET_Z_MIN + 1.0f + r * step;
		for (int k = 0; k < LANTERNS_PER_ROPE; k++) {
			float x = -STREET_HALF_W + (k + 0.5f) * (2.0f * STREET_HALF_W / LANTERNS_PER_ROPE);
			lanterns.push_back({ glm::vec3(x, ROPE_HEIGHT, z), rnd(rng), rnd(rng) });
		}
	}
}

// Calcula el ángulo de balanceo de un farol en función del tiempo y de su fase individual.
float LanternSwing(const Lantern& l, float t) // grados
{
	return LANTERN_SWING_DEG * sinf(LANTERN_SWING_W * t + l.phase);
}
// Calcula una pequeña variación de intensidad para simular el parpadeo de la luz de cada farol.
float LanternFlicker(const Lantern& l, float t)
{
	return 0.92f + 0.08f * Noise1D(t * 6.0f + l.phase * 10.0f);
}
// Construye la transformación de giro alrededor del punto del que cuelga el farol.
glm::mat4 LanternPivotMatrix(const Lantern& l, float t) // T(anclaje) * R(theta)
{
	glm::mat4 m = glm::translate(glm::mat4(1.0f), l.anchor);
	glm::vec3 axis(cosf(l.axisAngle), 0.0f, sinf(l.axisAngle));
	return glm::rotate(m, glm::radians(LanternSwing(l, t)), axis);
}

// ============================================================
//  MODELOS DE PRUEBA + MARCADORES NUMERADOS
//  Cada modelo se ajusta solo: se escala a 'targetH' metros de alto, se centra en X/Z y se apoya en Y=0.
//  Cada uno lleva un numero de color flotando encima (tecla M los muestra/oculta) para poder
//  decir "el 3 esta girado / muy grande / en mal lugar". Edita la tabla PROPS para corregirlos.
//    side : -1 = lado izquierdo de la calle, +1 = derecho, 0 = centrado en X (usa 'z' absoluta)
//    z    : posicion a lo largo de la calle
//    yaw  : giro en grados sobre Y. Los de los lados miran a la calle con +-90 (el "frente" del
//           modelo se supone en su +Z local; si esta al reves, suma 180).
// ============================================================
struct Prop {
	int id;                // numero del TIPO de modelo (todas las copias de un mismo modelo comparten numero)
	const char* file;
	int side;              // -1 izquierda, +1 derecha, 0 centrado
	float z;               // posicion a lo largo de la calle
	float targetH;         // alto final en metros
	float yaw;             // giro en grados sobre Y
	float xOff = 0.0f;     // desplazamiento extra en X (util con side = 0)
	float posYaw = 1e9f;   // yaw con el que se calcula la separacion al borde (por defecto = yaw)
	float cutBelowY = -1e30f; // quita bases/losas: descarta triangulos enteros por debajo de esta altura (unidades del modelo)
	float yOff = 0.0f;     // desplazamiento vertical en metros (negativo = hundirlo en el suelo)
	Model* model = nullptr;
	glm::mat4 matrix = glm::mat4(1.0f);
	glm::vec3 pos = glm::vec3(0.0f);
	float topY = 0.0f;
	glm::vec3 cullC = glm::vec3(0.0f); float cullR = 1e9f;   // esfera para descartar lo que no se ve
};

// ESCENA: calle de Yoiyama armada con los modelos disponibles. Orden: entrada (+Z) -> fondo (-Z).
//  #1 casa psx   #2 tienda angosta   #3 casa de 2 pisos   #4 kiosco de flores   #5 puesto de ramen
//  #6 altar      #7 edificio de fondo   #8 torii de entrada   #9 torii interior   #10 farol de piedra
const char* M_PSX    = "Models/psx_japanese_house.glb";
const char* M_STORE  = "Models/japanese_store.glb";
const char* M_HOUSE  = "Models/house.glb";
const char* M_FLOWER = "Models/japanese_flower_store.glb";
const char* M_RAMEN  = "Models/stylized_japanese_ramen_street_food_stal.glb";
const char* M_ALTAR  = "Models/stylized_japanese_altar.glb";
const char* M_TEMPLE = "Models/japanese_temple.glb";
const char* M_NOODLE = "Models/japanese_noodle_stand.glb";
const char* M_CHOCO  = "Models/japanese_chocolate_banana_stall.glb";
const char* M_ICE    = "Models/japanese_shaved_ice_stall.glb";
const char* M_RESTO  = "Models/japan_house_restaurant_by_night.glb";
const char* M_TOKYO  = "Models/japanese_shop_-_tokyo.glb";
const char* M_TORII1 = "Models/japanese_torii_gate_game_asset.glb";
const char* M_TORII2 = "Models/japanese_torii_gate.glb";
const char* M_LANTRN = "Models/stone_japanese_lantern.glb";

std::vector<Prop> props = {
	// --- entrada: torii y faroles de piedra a los lados ---
	{  8, M_TORII1,  0,  27.0f, 7.0f,   0.0f },
	{ 10, M_LANTRN,  0,  24.5f, 1.8f,  90.0f, -(STREET_HALF_W - 2.8f), 1e9f, 64.0f },
	{ 10, M_LANTRN,  0,  24.5f, 1.8f,  90.0f,  (STREET_HALF_W - 2.8f), 1e9f, 64.0f },

	// --- lado izquierdo (yaw 90 = de frente a la calle) ---
	{  1, M_PSX,    -1,  20.5f, 6.25f,   0.0f },
	{ 15, M_TOKYO,  -1,  11.8f, 6.3f,   90.0f },
	{  2, M_STORE,  -1,   8.0f, 5.0f,   90.0f },
	{  3, M_HOUSE,  -1,   2.5f, 6.5f,   90.0f },
	{  2, M_STORE,  -1,  -2.3f, 5.0f,   90.0f },
	{ 15, M_TOKYO,  -1,  -6.3f, 6.3f,   90.0f },
	{  6, M_ALTAR,  -1, -10.4f, 2.1f,   90.0f },
	{  2, M_STORE,  -1, -13.8f, 5.0f,   90.0f },
	{  3, M_HOUSE,  -1, -18.5f, 6.5f,   90.0f },
	{  2, M_STORE,  -1, -23.4f, 5.0f,   90.0f },
	{ 14, M_RESTO,  -1, -29.5f, 4.5f,   90.0f },   // restaurante al fondo

	// --- lado derecho (yaw -90) ---
	{  2, M_STORE,  +1,  24.0f, 5.0f,  -90.0f },
	{ 15, M_TOKYO,  +1,  20.2f, 6.3f,  -90.0f },
	{  2, M_STORE,  +1,  16.4f, 5.0f,  -90.0f },
	{  4, M_FLOWER, +1,   9.5f, 4.4f,  -45.0f, 0.0f, -90.0f, 0.09f },
	{  3, M_HOUSE,  +1,   0.0f, 6.5f,  -90.0f },
	{  5, M_RAMEN,  +1,  -8.0f, 3.75f,-135.0f, 0.0f, -90.0f },
	{ 14, M_RESTO,  +1, -16.8f, 4.5f,  -90.0f },
	{ 15, M_TOKYO,  +1, -22.7f, 6.3f,  -90.0f },
	{  1, M_PSX,    +1, -30.5f, 6.25f, 180.0f },

	// --- stands en el borde de la calle (xOff = x; yaw mira al centro) ---
	{ 11, M_NOODLE,  0,  14.5f, 2.4f, -90.0f,  (STREET_HALF_W - 1.0f) },
	{ 13, M_ICE,     0,  -3.5f, 2.5f, -90.0f,  (STREET_HALF_W - 1.0f) },
	{ 12, M_CHOCO,   0, -12.0f, 2.5f, -90.0f,  (STREET_HALF_W - 1.0f) },
	{ 12, M_CHOCO,   0,  15.0f, 2.5f,  90.0f, -(STREET_HALF_W - 1.0f) },
	{ 13, M_ICE,     0,   7.0f, 2.5f,  90.0f, -(STREET_HALF_W - 1.0f) },
	{ 11, M_NOODLE,  0, -12.5f, 2.4f,  90.0f, -(STREET_HALF_W - 1.0f) },

	// --- centro / fondo ---
	{  9, M_TORII2,  0, -31.5f, 7.0f,  90.0f },   // torii antes del templo
	{  7, M_TEMPLE,  0, -40.3f, 20.0f,  0.0f },   // templo al final de la calle (20 m de alto)
};

const glm::vec3 MARKER_COLORS[10] = {
	{1.00f, 0.10f, 0.10f}, // 1 rojo
	{1.00f, 0.55f, 0.00f}, // 2 naranja
	{1.00f, 0.95f, 0.10f}, // 3 amarillo
	{0.55f, 1.00f, 0.10f}, // 4 verde lima
	{0.10f, 0.85f, 0.25f}, // 5 verde
	{0.10f, 0.90f, 0.95f}, // 6 cian
	{0.15f, 0.40f, 1.00f}, // 7 azul
	{0.65f, 0.25f, 1.00f}, // 8 morado
	{1.00f, 0.20f, 0.80f}, // 9 rosa
	{1.00f, 1.00f, 1.00f}, // 10 blanco
};
const glm::vec3& MarkerColor(int id) { return MARKER_COLORS[(id - 1) % 10]; }

// Vegetacion: arces y sakuras en dos hileras irregulares detras de los edificios y alrededor del templo, mas arbustos en el
// borde de la entrada y a los lados del templo. Rompe el horizonte plano. Posiciones pseudoaleatorias fijas (semilla 11).
const char* M_MAPLE  = "Models/maple_tree.glb";
const char* M_SAKURA = "Models/sakura_tree_01_-_low_poly_model.glb";
const char* M_BUSH   = "Models/stylized_bush.glb";
// Añade árboles y arbustos alrededor de la calle con posiciones pseudoaleatorias fijas para completar el entorno.
void AddScenery()
{
	std::mt19937 rng(11);
	auto U = [&](float lo, float hi) { return std::uniform_real_distribution<float>(lo, hi)(rng); };
	auto tree = [&](float x, float z, float hMin, float hMax) {
		bool sakura = U(0.0f, 1.0f) < 0.3f;
		props.push_back({ sakura ? 17 : 16, sakura ? M_SAKURA : M_MAPLE, 0, z, U(hMin, hMax), U(0.0f, 360.0f), x });
	};
	auto bush = [&](float x, float z, float h) { props.push_back({ 18, M_BUSH, 0, z, h, U(0.0f, 360.0f), x }); };
	// hileras a los lados (a partir de los edificios) a lo largo de toda la calle
	for (int side = -1; side <= 1; side += 2) {
		for (float z = 34.0f; z > -52.0f; z -= 5.5f) {
			tree(side * U(19.5f, 24.0f), z + U(-1.5f, 1.5f), 10.0f, 14.0f);            // hilera cercana
			if (U(0.0f, 1.0f) < 0.8f) tree(side * U(28.0f, 38.0f), z + U(-2.5f, 2.5f), 12.0f, 17.0f);   // hilera lejana
			if (U(0.0f, 1.0f) < 0.6f) bush(side * U(17.5f, 21.0f), z + U(-2.0f, 2.0f), U(1.0f, 1.8f));
		}
	}
	// detras del templo (enmarcan su silueta)
	for (float x = -34.0f; x <= 34.0f; x += 6.0f)
		tree(x + U(-1.5f, 1.5f), U(-62.0f, -51.0f), 11.0f, 16.0f);
	for (float x = -28.0f; x <= 28.0f; x += 8.0f)
		tree(x + U(-2.0f, 2.0f), U(-76.0f, -66.0f), 14.0f, 19.0f);
	// detras de la entrada (si alguien mira hacia atras)
	for (float x = -30.0f; x <= 30.0f; x += 7.0f)
		if (fabsf(x) > 6.0f) tree(x + U(-2.0f, 2.0f), U(38.0f, 48.0f), 10.0f, 14.0f);
	// arbustos: flancos del templo y de la entrada
	for (int side = -1; side <= 1; side += 2) {
		bush(side * 11.5f, -36.0f, 1.4f); bush(side * 13.5f, -38.5f, 1.8f); bush(side * 16.0f, -41.0f, 2.0f);
		bush(side * 7.4f, 29.5f, 1.1f);   bush(side * 7.6f, 32.0f, 1.4f);
		// plaza del templo y torii: en hileras bajas a los lados de la escalinata y de la calle
		for (float z = -28.0f; z > -39.5f; z -= 1.9f) bush(side * U(7.0f, 7.8f), z + U(-0.4f, 0.4f), U(1.0f, 1.6f));
		bush(side * 5.2f, -33.2f, 1.2f); bush(side * 6.0f, -36.5f, 1.5f); bush(side * 4.6f, -38.4f, 1.3f); bush(side * 9.0f, -39.0f, 1.7f);
		for (float z = 27.0f; z > 12.0f; z -= 2.6f) bush(side * U(7.2f, 7.8f), z + U(-0.5f, 0.5f), U(0.9f, 1.4f));   // borde de la calle de entrada
	}
	for (Prop& pr : props) if (pr.id >= 16) pr.cutBelowY = -1e30f;
}

// Carga los modelos de la escena, reutiliza modelos repetidos y calcula sus escalas, posiciones y límites espaciales.
void LoadProps()
{
	printf("\n=== MODELOS (numero de color sobre cada uno; tecla M = mostrar/ocultar) ===\n");
	std::map<std::string, Model*> cache; // un mismo .glb se carga una sola vez aunque aparezca en varias filas
	for (Prop& pr : props) {
		std::string key = std::string(pr.file) + "|" + std::to_string(pr.cutBelowY);
		auto it = cache.find(key);
		if (it == cache.end()) {
			Model* mdl = new Model();
			mdl->LoadModel(pr.file, pr.cutBelowY);
			if (mdl->IsLoaded()) printf("  [malla] %s: %u mallas\n", pr.file, mdl->GetMeshCount());
			it = cache.emplace(key, mdl).first;
		}
		pr.model = it->second;
		if (!pr.model->IsLoaded()) { printf("  #%d NO CARGO: %s\n", pr.id, pr.file); continue; }

		glm::vec3 mn = pr.model->GetBoundsMin(), mx = pr.model->GetBoundsMax();
		glm::vec3 size = mx - mn;
		float scale = pr.targetH / size.y;
		float cx = 0.5f * (mn.x + mx.x), cz = 0.5f * (mn.z + mx.z);

		float py = (pr.posYaw > 1e8f) ? pr.yaw : pr.posYaw;
		bool sideways = fabsf(sinf(py * toRadians)) > 0.5f; // girado +-90: su profundidad queda sobre X
		float halfX = 0.5f * (sideways ? size.z : size.x) * scale;
		float x = ((pr.side == 0) ? 0.0f : pr.side * (STREET_HALF_W + 0.3f + halfX)) + pr.xOff;

		glm::mat4 m = glm::translate(glm::mat4(1.0f), glm::vec3(x, pr.yOff, pr.z));
		m = glm::rotate(m, glm::radians(pr.yaw), glm::vec3(0.0f, 1.0f, 0.0f));
		m = glm::scale(m, glm::vec3(scale));
		m = glm::translate(m, glm::vec3(-cx, -mn.y, -cz));
		pr.matrix = m;
		pr.pos = glm::vec3(x, 0.0f, pr.z);
		pr.topY = pr.targetH;
		pr.cullC = glm::vec3(m * glm::vec4(cx, mn.y + 0.5f * size.y, cz, 1.0f));
		pr.cullR = 0.5f * glm::length(size) * scale * 1.05f + 0.5f;
		printf("  #%-2d  alto %.2f m  yaw %+4.0f  x %+5.1f  z %+5.1f   %s\n", pr.id, pr.targetH, pr.yaw, x, pr.z, pr.file);
	}
}

// Muestrario de texturas TEMPORALES (hechas a partir de los mapas de desplazamiento _disp_)
struct SwatchDef { const char* file; const char* name; };
const SwatchDef SWATCHES[5] = {
	{ "Textures/Temporal/piedra.jpg", "piedra (piso)" },
	{ "Textures/Temporal/pino.jpg",   "pino" },
	{ "Textures/Temporal/arce.jpg",   "arce" },
	{ "Textures/Temporal/tatami.jpg", "tatami" },
	{ "Textures/Temporal/tapiz.jpg",  "tapiz (quatrefoil)" },
};
Texture swatchTex[5];

// ============================================================
//  CARROS (kankoboko.glb) CON RUEDAS Y VIGAS DE MADERA GENERADAS EN CODIGO
//  El modelo viene sin ruedas: se le agregan 4 ruedas (2 delanteras, 2 traseras) y dos vigas
//  longitudinales, una por lado, que unen la rueda delantera con la trasera.
//  Los carros dan vueltas a un circuito: van al templo pegados al lado izquierdo, hacen una curva suave
//  de media vuelta cerca del templo y regresan a la entrada pegados al lado derecho, donde giran de nuevo.
//  Sistema local de cada carro: el frente mira a -Z, X es el ancho.
// ============================================================
const char* M_CART = "Models/kankoboko.glb";
const float CART_MODEL_YAW = 90.0f; // giro (grados): pone el lado largo del modelo a lo largo de Z (avance)
const float CART_SCALE     = 0.50f; // escala del modelo (1.0 = tamano original de 10 m de alto)
const float CART_BODY_Y    = 0.14f; // altura del fondo del cuerpo: casi al ras del suelo
const float WHEEL_R        = 0.50f; // radio de las ruedas
const float BEAM_T         = 0.11f; // grosor de las vigas
const int   CART_COUNT     = 4;
const float CART_GAP       = 6.5f;  // separacion entre carros (m): van en caravana
const float CART_SPEED     = 0.2f;  // m/s (muy lento)
const float CART_LANE      = 2.6f;  // carril izquierdo en x = -2.6 y derecho en x = +2.6 (tambien es el radio de la curva)
const float CART_Z_FAR     = -20.5f; // centro de la curva del lado del templo
const float CART_Z_NEAR    =  21.0f; // centro de la curva del lado de la entrada

struct CartRig {
	Model* model = nullptr;
	glm::mat4 bodyMatrix = glm::mat4(1.0f); // modelo -> espacio local del carro
	float width = 0.0f, length = 0.0f;
	float wheelX = 0.0f, wheelZ = 0.0f, beamX = 0.0f;
};
CartRig cartRig;
Texture woodTexture;
bool woodLoaded = false;

float cartDist  = 30.0f;   // distancia recorrida por el carro 0 sobre el circuito (m)
float cartWheel = 0.0f;   // angulo acumulado de las ruedas (grados)

// Longitud del circuito: dos rectas y dos medias vueltas
float CartLoopLength() { return 2.0f * (CART_Z_NEAR - CART_Z_FAR) + 2.0f * PI_F * CART_LANE; }

// Posicion y rumbo (grados, 0 = mirando a -Z) sobre el circuito a la distancia s
// Calcula la posición y orientación de un carro sobre su recorrido cerrado a partir de la distancia recorrida.
glm::vec3 CartPath(float s, float& headingDeg)
{
	const float straight = CART_Z_NEAR - CART_Z_FAR, arc = PI_F * CART_LANE, L = CartLoopLength();
	s = fmodf(s, L); if (s < 0.0f) s += L;
	glm::vec2 pos, tan;                      // (x, z) y tangente unitaria
	if (s < straight) {                      // ida por la izquierda, hacia el templo (-Z)
		pos = glm::vec2(-CART_LANE, CART_Z_NEAR - s);  tan = glm::vec2(0.0f, -1.0f);
	} else if (s < straight + arc) {         // curva del lado del templo
		float f = (s - straight) / CART_LANE;
		pos = glm::vec2(-CART_LANE * cosf(f), CART_Z_FAR - CART_LANE * sinf(f));  tan = glm::vec2(sinf(f), -cosf(f));
	} else if (s < 2.0f * straight + arc) {  // regreso por la derecha, hacia la entrada (+Z)
		pos = glm::vec2(CART_LANE, CART_Z_FAR + (s - straight - arc));  tan = glm::vec2(0.0f, 1.0f);
	} else {                                 // curva del lado de la entrada
		float f = (s - 2.0f * straight - arc) / CART_LANE;
		pos = glm::vec2(CART_LANE * cosf(f), CART_Z_NEAR + CART_LANE * sinf(f));  tan = glm::vec2(-sinf(f), cosf(f));
	}
	headingDeg = glm::degrees(atan2f(-tan.x, -tan.y));   // el frente del carro (-Z local) mira a lo largo de la tangente
	return glm::vec3(pos.x, 0.0f, pos.y);
}

// Carga el modelo del carro y prepara sus transformaciones para que pueda desplazarse y girar correctamente.
void LoadCart()
{
	cartRig.model = new Model();
	cartRig.model->LoadModel(M_CART);
	if (!cartRig.model->IsLoaded()) { printf("NO CARGO el carro: %s\n", M_CART); cartRig.model = nullptr; return; }

	glm::vec3 mn = cartRig.model->GetBoundsMin(), mx = cartRig.model->GetBoundsMax();
	glm::mat4 R = glm::rotate(glm::mat4(1.0f), glm::radians(CART_MODEL_YAW), glm::vec3(0.0f, 1.0f, 0.0f));
	R = glm::scale(R, glm::vec3(CART_SCALE));
	glm::vec3 lo(1e30f), hi(-1e30f);
	for (int i = 0; i < 8; i++) {   // caja envolvente del modelo ya girado y escalado
		glm::vec3 c((i & 1) ? mx.x : mn.x, (i & 2) ? mx.y : mn.y, (i & 4) ? mx.z : mn.z);
		glm::vec3 p = glm::vec3(R * glm::vec4(c, 1.0f));
		lo = glm::min(lo, p); hi = glm::max(hi, p);
	}
	cartRig.width  = hi.x - lo.x;
	cartRig.length = hi.z - lo.z;
	cartRig.bodyMatrix = glm::translate(glm::mat4(1.0f),
		glm::vec3(-0.5f * (lo.x + hi.x), CART_BODY_Y - lo.y, -0.5f * (lo.z + hi.z))) * R;

	// de adentro hacia afuera: cuerpo, viga, rueda (el cubo de la rueda toca la viga)
	const float wheelHalfW = 0.17f * WHEEL_R;
	cartRig.beamX  = cartRig.width * 0.5f + BEAM_T * 0.5f - 0.01f;
	cartRig.wheelX = cartRig.beamX + BEAM_T * 0.5f + wheelHalfW;
	cartRig.wheelZ = glm::max(cartRig.length * 0.5f - 0.35f, 0.3f);

	woodTexture = Texture("Textures/Temporal/pino.jpg");
	woodLoaded = woodTexture.LoadTextureA();
	printf("  CARRO  %.2f x %.2f x %.2f m (ancho x alto x largo)  circuito de %.1f m\n",
		cartRig.width, hi.y - lo.y, cartRig.length, CartLoopLength());
}

// Actualiza el avance de los carros y el giro de sus ruedas usando el tiempo transcurrido entre cuadros.
void UpdateCarts(float dt)
{
	if (!cartRig.model) return;
	cartDist += CART_SPEED * dt;
	cartWheel -= glm::degrees(CART_SPEED * dt / WHEEL_R);   // rueda sin deslizar: omega = v / r
}

// ============================================================
//  MUSICO DE TAIKO  -  animacion por KEYFRAMES (obligatoria en la propuesta)
//  Modelo dividido en 10 piezas (Models/Animaciones/Taiko/Partes/): cuerpo, tambor y, por lado,
//  brazo, antebrazo, mano y baqueta; cada pieza tiene su origen en la articulacion, asi que la jerarquia es
//      cuerpo (inclinacion en la cadera) -> brazo (hombro) -> antebrazo (codo) -> mano (muñeca) -> baqueta
//  Unidades del modelo: 1.0 = altura de la persona. El personaje mira a +X y su derecha es +Z.
//  Ciclo de 72 pasos a 60 Hz = 1.2 s: golpe derecho, rebote, golpe izquierdo, rebote y reposo.
//  Variables por cuadro clave: { hombroD, codoD, muñecaD, hombroI, codoI, muñecaI, inclinacion, piel }
//  (grados; rotan alrededor del eje Z; + levanta la mano, - la baja). piel = hundimiento del tambor (0..1).
// ============================================================
const char* TAIKO_DIR   = "Models/Animaciones/Taiko/Partes/";
const int TAIKO_COUNT = 3;                         // tres musicos en fila, con la misma animacion
const glm::vec3 TAIKO_POS[TAIKO_COUNT] = {        // lado izquierdo de la calle, mirando al centro
	glm::vec3(-(STREET_HALF_W - 2.1f), 0.0f, -4.4f), glm::vec3(-(STREET_HALF_W - 2.1f), 0.0f, -5.5f), glm::vec3(-(STREET_HALF_W - 2.1f), 0.0f, -6.6f) };
const float TAIKO_YAW   = 0.0f;
const float TAIKO_SCALE = 1.7f;                   // metros por unidad del modelo
const glm::vec3 TAIKO_HIP(-0.20f, 0.48f, 0.0f);   // eje de la inclinacion del torso
const float TAIKO_DRUM_PRESS = 0.03f;             // la piel se hunde 3 % al golpear

struct TaikoArm { glm::vec3 shoulder, elbow, wrist, grip; };
const TaikoArm TAIKO_ARM[2] = {
	{ glm::vec3(-0.245f, 0.770f,  0.150f), glm::vec3(-0.225f, 0.640f,  0.190f), glm::vec3(-0.125f, 0.585f,  0.175f), glm::vec3(-0.100f, 0.5763f,  0.1411f) },  // derecha (+Z)
	{ glm::vec3(-0.245f, 0.770f, -0.150f), glm::vec3(-0.225f, 0.640f, -0.190f), glm::vec3(-0.125f, 0.585f, -0.135f), glm::vec3(-0.100f, 0.5413f, -0.1182f) },  // izquierda (-Z)
};
// indices de taikoModel: 0 cuerpo, 1 tambor, y por lado (0 der, 1 izq): 2+4*lado = brazo, +1 antebrazo, +2 mano, +3 baqueta
const char* TAIKO_FILES[10] = {
	"taiko_cuerpo.glb", "taiko_tambor.glb",
	"taiko_brazo_der.glb", "taiko_antebrazo_der.glb", "taiko_mano_der.glb", "taiko_baqueta_der.glb",
	"taiko_brazo_izq.glb", "taiko_antebrazo_izq.glb", "taiko_mano_izq.glb", "taiko_baqueta_izq.glb" };
Model* taikoModel[10] = { nullptr };
KeyframePath taikoPath;
float taikoAccum = 0.0f;
int taikoPrevSeg = -1;
ma_sound* taikoHitSound = nullptr;   // golpe de taiko (Audio/taiko_hit.wav, opcional)

// Carga y prepara los modelos y recursos necesarios para la animación de los músicos de taiko.
void LoadTaiko()
{
	for (int i = 0; i < 10; i++) {
		taikoModel[i] = new Model();
		taikoModel[i]->LoadModel(std::string(TAIKO_DIR) + TAIKO_FILES[i]);
		if (!taikoModel[i]->IsLoaded()) { printf("NO CARGO parte del taiko: %s\n", TAIKO_FILES[i]); taikoModel[0] = nullptr; return; }
	}
	taikoPath = KeyframePath(
		{
			//  hombD  codoD  muneD  hombI  codoI  muneI  incl  piel
			{   0.0f,  0.0f,  0.0f,  0.0f,  0.0f,  0.0f,  0.0f, 0.0f },  // 0 reposo
			{  28.0f, 22.0f,  8.0f,  0.0f,  0.0f,  0.0f, -2.0f, 0.0f },  // 1 brazo derecho arriba
			{   4.0f,  0.0f,-20.0f,  0.0f,  0.0f,  0.0f,  4.0f, 1.0f },  // 2 GOLPE derecho
			{  14.0f, 10.0f, -4.0f,  0.0f,  0.0f,  0.0f,  0.0f, 0.0f },  // 3 rebote derecho
			{   0.0f,  0.0f,  0.0f, 26.0f, 20.0f,  8.0f, -2.0f, 0.0f },  // 4 brazo izquierdo arriba
			{   0.0f,  0.0f,  0.0f,  3.0f,  0.0f,-12.0f,  4.0f, 1.0f },  // 5 GOLPE izquierdo
			{   0.0f,  0.0f,  0.0f, 12.0f,  8.0f, -4.0f,  0.0f, 0.0f },  // 6 rebote izquierdo
			{   0.0f,  0.0f,  0.0f,  0.0f,  0.0f,  0.0f,  0.0f, 0.0f },  // 7 = 0, cierra el ciclo
		},
		{ 12, 9, 7, 10, 9, 7, 18 });   // suma 72 pasos = 1.2 s
}

// Actualiza el estado de la animación de los músicos y la sincronización de sus movimientos.
void UpdateTaiko(float dt)
{
	if (!taikoModel[0]) return;
	taikoAccum = glm::min(taikoAccum + dt, 0.25f);          // paso fijo de 60 Hz: misma velocidad con cualquier FPS
	while (taikoAccum >= 1.0f / 60.0f) {
		taikoAccum -= 1.0f / 60.0f;
		taikoPath.Step();
		int seg = taikoPath.Segment();
		if (seg != taikoPrevSeg) {
			taikoPrevSeg = seg;
			if ((seg == 2 || seg == 5) && taikoHitSound) {   // cuadro de impacto: suena el golpe
				ma_sound_seek_to_pcm_frame(taikoHitSound, 0);
				ma_sound_start(taikoHitSound);
			}
		}
	}
}

// ============================================================
//  PILETA DEL KINGYO-SUKUI + PECES + DUENOS
//  Pileta de madera (tina de duelas) con agua translucida que ondea (shader), 8 peces dorados y dos
//  personas en kimono que atienden el puesto.
//  Cada pez sigue una curva de Lissajous  x = A sin(a t + d),  z = B sin(b t)  con a != b (la ruta no se repite),
//  mira hacia su velocidad (atan2) y su aleta oscila a 6 Hz: la cola se dobla en el vertex shader.
// ============================================================
const glm::vec3 POOL_C(STREET_HALF_W - 2.1f, 0.0f, 4.5f);   // centro de la pileta (lado derecho de la calle)
const float POOL_R       = 0.70f;           // radio interior (m)
const float POOL_H       = 0.30f;           // altura de la pared
const float POOL_WATER_Y = 0.24f;           // nivel del agua
const int   FISH_COUNT   = 8;
const float FISH_LEN     = 0.18f;           // largo de un pez de tamano 1 (m)
const float FISH_TAIL_HZ = 6.0f;            // frecuencia de la aleta (propuesta: 6 Hz)
const float FISH_TAIL_DEG = 20.0f;          // amplitud de la aleta (propuesta: +-20 grados)

struct Fish {
	float A, B, a, b, delta;   // Lissajous
	float depth, size;
	glm::vec3 tint;
	float ft = 0.0f;            // tiempo propio del pez (corre al doble de velocidad cuando huye)
	int state = 0;              // 0 nada, 1 salta al tazon, 2 esta en el tazon (reaparece despues)
	float jt = 0.0f, scared = 0.0f, respawn = 0.0f;
	glm::vec3 jfrom = glm::vec3(0.0f);
};
std::vector<Fish> fish;
Model* fishModel = nullptr;
glm::vec3 fishCenter(0.0f);
float fishScale = 1.0f, fishTailZ0 = 0.0f, fishTailZ1 = 0.0f, fishTailAmp = 0.0f;

struct Owner {
	const char* file;
	float height;
	float x, z, yaw;
	float phase;               // desfase de la animacion de reposo
	Model* model = nullptr;
	glm::vec3 center = glm::vec3(0.0f);
	float minY = 0.0f, scale = 1.0f;
};
// Los dos duenos flanquean la pileta, mirando hacia el centro de la calle (-X). yaw = -90 si el frente del modelo es +Z.
std::vector<Owner> owners = {
	{ "Models/kimono_nagase.glb",    1.55f, STREET_HALF_W - 0.95f, 5.1f, -90.0f, 0.0f },
	{ "Models/toda_yuki_kimono.glb", 1.50f, STREET_HALF_W - 0.95f, 3.9f, -90.0f, 2.1f },
};

// Los dos duenos de la pileta estan separados en piezas articuladas (Models/Animaciones/Vendedores/, hechas con una herramienta
// propia que corta la malla escaneada por zonas): base (piernas y kimono) -> torso con brazos (se corta en el obi) -> cabeza (se corta
// en el cuello); el de rojo trae ademas el abanico con sus manos, hijo del torso. Cada pieza trae su origen en la articulacion.
// Pivotes en unidades del modelo respecto a su raiz (centro X/Z, base en Y); el escalado es el mismo del Owner.
struct NpcRig {
	Model *base = nullptr, *torso = nullptr, *head = nullptr, *fan = nullptr;
	glm::vec3 pTorso, pHead, pFan;
	float bowDeg, headYawDeg, fanDeg;    // amplitudes de la animacion de cada personaje
};
NpcRig npcRigs[2];     // 0: kimono gris (Nagase), 1: kimono rojo (Toda) -> mismo orden que los dos primeros de `owners`

// Prepara las estructuras y modelos de los personajes que participan en la escena.
void LoadNpcRigs()
{
	auto ld = [&](const char* f) { Model* m = new Model(); m->LoadModel(f); if (!m->IsLoaded()) { printf("NO CARGO pieza: %s\n", f); delete m; return (Model*)nullptr; } return m; };
	NpcRig& a = npcRigs[0];
	a.base = ld("Models/Animaciones/Vendedores/nagase_base.glb");
	a.torso = ld("Models/Animaciones/Vendedores/nagase_torso.glb");
	a.head = ld("Models/Animaciones/Vendedores/nagase_cabeza.glb");
	a.pTorso = glm::vec3(0.6718f, 14.4475f, -0.4475f);
	a.pHead = glm::vec3(0.6218f, 21.5475f, -0.3975f);
	a.bowDeg = 9.0f; a.headYawDeg = 28.0f; a.fanDeg = 0.0f;
	NpcRig& b = npcRigs[1];
	b.base = ld("Models/Animaciones/Vendedores/toda_base.glb");
	b.torso = ld("Models/Animaciones/Vendedores/toda_torso.glb");
	b.head = ld("Models/Animaciones/Vendedores/toda_cabeza.glb");
	b.fan = ld("Models/Animaciones/Vendedores/toda_abanico.glb");
	b.pTorso = glm::vec3(0.0050f, 8.7515f, 0.0471f);
	b.pHead = glm::vec3(-0.0450f, 14.9515f, -0.1529f);
	b.pFan = glm::vec3(-0.5450f, 11.7515f, 1.1471f);
	b.bowDeg = 16.0f; b.headYawDeg = 20.0f; b.fanDeg = 1.0f;
	for (NpcRig& r : npcRigs) if (!r.base || !r.torso || !r.head) { r.base = nullptr; }   // sin piezas completas: se usa el modelo entero
}

// Carga y configura la pileta, los peces y los elementos necesarios para el minijuego de pesca.
void LoadPool()
{
	fishModel = new Model();
	fishModel->LoadModel("Models/Animaciones/Goldfish game/goldfish.glb");
	if (!fishModel->IsLoaded()) { printf("NO CARGO el pez dorado\n"); fishModel = nullptr; }
	else {
		glm::vec3 mn = fishModel->GetBoundsMin(), mx = fishModel->GetBoundsMax();
		fishCenter = 0.5f * (mn + mx);
		fishScale = FISH_LEN / (mx.z - mn.z);              // el pez mide 95.6 unidades a lo largo de Z (cabeza en +Z)
		fishTailZ0 = mn.z + 0.30f * (mx.z - mn.z);        // la cola empieza a doblarse a 30 % del largo desde la cola
		fishTailZ1 = mn.z;
		fishTailAmp = (fishTailZ0 - fishTailZ1) * tanf(glm::radians(FISH_TAIL_DEG));  // desplazamiento de la punta
	}

	std::mt19937 rng(7);
	auto U = [&](float lo, float hi) { return std::uniform_real_distribution<float>(lo, hi)(rng); };
	const glm::vec3 tints[4] = { glm::vec3(1.0f), glm::vec3(1.0f, 0.82f, 0.65f), glm::vec3(1.0f, 0.62f, 0.52f), glm::vec3(1.0f, 0.95f, 0.85f) };
	for (int i = 0; i < FISH_COUNT; i++) {
		Fish f;
		f.A = U(0.18f, 0.36f); f.B = U(0.18f, 0.36f);
		f.a = U(0.45f, 0.90f); f.b = f.a * U(1.15f, 1.45f);       // a != b
		f.delta = U(0.0f, 2.0f * PI_F);
		f.depth = U(0.08f, 0.17f); f.size = U(0.8f, 1.2f);
		f.tint = tints[i % 4];
		fish.push_back(f);
	}

	// Vendedores: uno detras de cada puesto (stands ids 11, 12, 13). Se reparten los NPC de VENDOR_FILES por turnos;
	// para agregar personajes nuevos basta con anadirlos a esa lista (nombre del .glb y alto en metros).
	static const struct { const char* file; float height; } VENDOR_FILES[] = {
		{ "Models/toda_yuki_kimono.glb", 1.50f },
		{ "Models/kimono_nagase.glb",    1.55f },
		{ "Models/geisha_in_a_satin_floral_kimono.glb", 1.55f },
		{ "Models/morgans_pink_lobster_kimono.glb",     1.50f },
	};
	const int nVendorModels = (int)(sizeof(VENDOR_FILES) / sizeof(VENDOR_FILES[0]));
	int vi = 0;
	for (const Prop& pr : props) {
		if (pr.id < 11 || pr.id > 13) continue;
		float outward = (pr.xOff >= 0.0f) ? 1.0f : -1.0f;        // hacia la pared, detras del mostrador
		const auto& vf = VENDOR_FILES[vi % nVendorModels];
		owners.push_back({ vf.file, vf.height, pr.xOff + 0.3f * outward, pr.z, pr.yaw, 0.9f * vi });
		vi++;
	}

	LoadNpcRigs();
	std::map<std::string, Model*> npcCache;   // cada .glb se carga una sola vez
	for (Owner& o : owners) {
		auto it = npcCache.find(o.file);
		if (it == npcCache.end()) {
			Model* m = new Model();
			m->LoadModel(o.file);
			it = npcCache.emplace(o.file, m).first;
		}
		o.model = it->second;
		if (!o.model->IsLoaded()) { printf("NO CARGO: %s\n", o.file); o.model = nullptr; continue; }
		glm::vec3 mn = o.model->GetBoundsMin(), mx = o.model->GetBoundsMax();
		o.center = 0.5f * (mn + mx);
		o.minY = mn.y;
		o.scale = o.height / (mx.y - mn.y);
	}
}

// ============================================================
//  JUEGO KINGYO-SUKUI (animacion 5: parametrica + interaccion)
//  Cerca de la pileta, E inicia la partida: la camara se desliza a una vista desde arriba, el cursor se libera y
//  el mouse mueve la red (poi). Clic izquierdo (mantenido) baja la red al agua. Partida de 60 s con marcador.
//   - Los peces nadan con sus curvas de Lissajous (ft = tiempo propio del pez).
//   - Si la red se acerca a menos de FLEE_R y se mueve rapido, el pez se asusta: nada al doble de velocidad 1.2 s.
//   - Si la red esta abajo y un pez tranquilo queda a menos de CATCH_R, lo atrapa: salta en parabola (0.6 s) al tazon.
//   - Al terminar la partida se muestra el resultado 3.5 s y la camara regresa al lugar donde estabas. ESC cancela.
// ============================================================
enum GameState { GS_IDLE, GS_ENTER, GS_PLAY, GS_RESULT, GS_LEAVE };
GameState gameState = GS_IDLE;
float gameT = 0.0f, gameTimeLeft = 60.0f, gameSecs = 60.0f;
int gameScore = 0, bowlCount = 0;
bool autoGame = false;                       // pruebas: una "IA" mueve la red (--gameshot)
const float GAME_SECS = 60.0f, GAME_ENTER_SECS = 0.9f, GAME_RESULT_SECS = 3.5f, GAME_NEAR_R = 3.2f;
const float NET_R = 0.11f, FLEE_R = 0.15f, CATCH_R = 0.09f, FLEE_SPEED = 0.55f;   // se asustan si la red va a mas de 0.55 m/s
const float JUMP_SECS = 0.6f, JUMP_H = 0.45f, POOL_PLAY_R = 0.58f;
const glm::vec3 BOWL_C = POOL_C + glm::vec3(-1.05f, 0.0f, 0.35f);   // tazon junto a la pileta, del lado de la calle
const float BOWL_R = 0.20f, BOWL_H = 0.13f;

struct CamPose { glm::vec3 pos; float yaw, pitch; };
CamPose camSaved = { glm::vec3(0.0f), 0.0f, 0.0f };
const CamPose camGame = { POOL_C + glm::vec3(0.0f, 1.45f, 1.05f), -90.0f, -53.5f };   // vista desde arriba de la pileta

glm::vec3 netPos = POOL_C;                   // (x, z) de la red sobre el agua
float netY = POOL_WATER_Y + 0.12f, netSpeed = 0.0f;
bool netDown = false;

// Calcula la posición actual de un pez siguiendo su trayectoria paramétrica y, opcionalmente, su orientación.
glm::vec3 FishPos(const Fish& f, float* yawOut)
{
	float x = f.A * sinf(f.a * f.ft + f.delta), z = f.B * sinf(f.b * f.ft);
	float vx = f.A * f.a * cosf(f.a * f.ft + f.delta), vz = f.B * f.b * cosf(f.b * f.ft);
	if (yawOut) *yawOut = atan2f(vx, vz);
	return POOL_C + glm::vec3(x, f.depth + 0.012f * sinf(1.3f * f.ft), z);
}

// Punto del plano del agua bajo el cursor (rayo desde la camara)
// Convierte la posición del cursor en un punto del plano del agua mediante un rayo proyectado desde la cámara.
glm::vec3 MouseOnWater(const glm::mat4& proj, const glm::mat4& view)
{
	double mx, my; int ww, wh;
	mainWindow.getCursor(&mx, &my);
	mainWindow.getWindowSize(&ww, &wh);
	float nx = 2.0f * (float)mx / ww - 1.0f, ny = 1.0f - 2.0f * (float)my / wh;
	glm::mat4 inv = glm::inverse(proj * view);
	glm::vec4 a = inv * glm::vec4(nx, ny, -1.0f, 1.0f), b = inv * glm::vec4(nx, ny, 1.0f, 1.0f);
	glm::vec3 o = glm::vec3(a / a.w), d = glm::normalize(glm::vec3(b / b.w) - o);
	if (fabsf(d.y) < 1e-4f) return glm::vec3(POOL_C.x, POOL_WATER_Y, POOL_C.z);
	return o + d * ((POOL_WATER_Y - o.y) / d.y);
}

float Smooth01(float k) { k = glm::clamp(k, 0.0f, 1.0f); return k * k * (3.0f - 2.0f * k); }

void SetCamLerp(const CamPose& a, const CamPose& b, float k)
{
	float dy = fmodf(b.yaw - a.yaw + 540.0f, 360.0f) - 180.0f;   // camino corto entre angulos
	camera.setView(glm::mix(a.pos, b.pos, k), a.yaw + dy * k, glm::mix(a.pitch, b.pitch, k));
}

// Inicializa una nueva partida del minijuego y reinicia sus variables de estado.
void StartGame()
{
	camSaved = { camera.getCameraPosition(), camera.getYaw(), camera.getPitch() };
	gameState = GS_ENTER; gameT = 0.0f;
	gameScore = 0; bowlCount = 0; gameTimeLeft = gameSecs;
	for (Fish& f : fish) { f.state = 0; f.scared = 0.0f; }
	netPos = POOL_C; netDown = false; netSpeed = 0.0f;
	mainWindow.setCursorVisible(true);
	mainWindow.escapeCloses = false;
	printf("\nJUEGO: mueve el mouse para llevar la red, mantén el clic izquierdo para bajarla. ESC sale.\n");
}

// Actualiza el movimiento de los peces y sus transiciones de comportamiento a lo largo del tiempo.
void UpdateFish(float dt)
{
	for (Fish& f : fish) {
		if (f.state == 0) {
			f.ft += dt * (f.scared > 0.0f ? 2.0f : 1.0f);
			f.scared = glm::max(0.0f, f.scared - dt);
		}
		else if (f.state == 1) {
			f.jt += dt;
			if (f.jt >= JUMP_SECS) { f.state = 2; f.respawn = 1.5f; bowlCount++; gameScore++; }
		}
		else if (f.state == 2) {
			f.respawn -= dt;
			if (f.respawn <= 0.0f) { f.state = 0; f.ft += 7.0f; f.scared = 0.0f; }   // un pez nuevo reaparece en otra parte de su curva
		}
	}
}

// Gestiona la lógica del minijuego: interacción con la red, movimiento, captura de peces y resultados.
void UpdateGame(float dt, const glm::mat4& proj, bool ePressed)
{
	UpdateFish(dt);
	glm::vec3 cp = camera.getCameraPosition();

	switch (gameState) {
	case GS_IDLE:
		if (ePressed && glm::length(glm::vec2(cp.x - POOL_C.x, cp.z - POOL_C.z)) < GAME_NEAR_R) StartGame();
		break;

	case GS_ENTER:
		gameT += dt;
		SetCamLerp(camSaved, camGame, Smooth01(gameT / GAME_ENTER_SECS));
		if (gameT >= GAME_ENTER_SECS) { gameState = GS_PLAY; gameT = 0.0f; }
		break;

	case GS_PLAY: {
		gameTimeLeft -= dt;
		glm::vec3 target; bool down;
		if (autoGame) {                       // IA de prueba: persigue al pez tranquilo mas cercano y baja la red sobre el
			target = netPos; down = false;
			float best = 1e9f;
			for (const Fish& f : fish) {
				if (f.state != 0 || f.scared > 0.0f) continue;
				glm::vec3 p = FishPos(f, nullptr);
				float d = glm::length(glm::vec2(p.x - netPos.x, p.z - netPos.z));
				if (d < best) { best = d; target = p; }
			}
			down = best < 0.05f;
			glm::vec2 dir(target.x - netPos.x, target.z - netPos.z);
			float len = glm::length(dir), step = glm::min(len, (best > 0.30f ? 0.60f : 0.40f) * dt);
			if (len > 1e-5f) { dir = dir / len * step; target = glm::vec3(netPos.x + dir.x, 0.0f, netPos.z + dir.y); }
			else target = netPos;
		}
		else {
			target = MouseOnWater(proj, camera.calculateViewMatrix());
			down = mainWindow.mouseDown();
		}
		glm::vec2 c(target.x - POOL_C.x, target.z - POOL_C.z);     // la red no sale de la pileta
		float len = glm::length(c);
		if (len > POOL_PLAY_R) c *= POOL_PLAY_R / len;
		glm::vec3 np(POOL_C.x + c.x, 0.0f, POOL_C.z + c.y);
		float inst = glm::length(glm::vec2(np.x - netPos.x, np.z - netPos.z)) / glm::max(dt, 1e-4f);
		netSpeed = glm::mix(netSpeed, inst, 0.3f);
		netPos = np; netDown = down;
		netY = glm::mix(netY, down ? POOL_WATER_Y - 0.03f : POOL_WATER_Y + 0.12f, glm::min(1.0f, dt * 14.0f));

		for (Fish& f : fish) {
			if (f.state != 0) continue;
			glm::vec3 p = FishPos(f, nullptr);
			float d = glm::length(glm::vec2(p.x - netPos.x, p.z - netPos.z));
			if (d < FLEE_R && netSpeed > FLEE_SPEED) f.scared = 1.2f;                // red rapida cerca: huye al doble de velocidad
			if (netDown && netY < POOL_WATER_Y + 0.01f && d < CATCH_R && f.scared <= 0.0f) {
				f.state = 1; f.jt = 0.0f; f.jfrom = p;                               // atrapado: salta al tazon
			}
		}
		if (gameTimeLeft <= 0.0f) { gameTimeLeft = 0.0f; gameState = GS_RESULT; gameT = 0.0f; printf("Puntos: %d\n", gameScore); }
		else if (KeyPressedOnce(GLFW_KEY_ESCAPE)) { gameState = GS_LEAVE; gameT = 0.0f; }
		break; }

	case GS_RESULT:
		gameT += dt;
		netY = glm::mix(netY, POOL_WATER_Y + 0.12f, glm::min(1.0f, dt * 6.0f));
		if (gameT >= GAME_RESULT_SECS || KeyPressedOnce(GLFW_KEY_ESCAPE)) { gameState = GS_LEAVE; gameT = 0.0f; }
		break;

	case GS_LEAVE:
		gameT += dt;
		SetCamLerp(camGame, camSaved, Smooth01(gameT / GAME_ENTER_SECS));
		if (gameT >= GAME_ENTER_SECS) {
			gameState = GS_IDLE; gameT = 0.0f; autoGame = false;
			mainWindow.setCursorVisible(false);
			mainWindow.escapeCloses = true;
		}
		break;
	}
}

// ============================================================
//  GENTE CAMINANDO (animacion de esqueleto "horneada")
//  Los .glb de Models/Caminantes/ salen de tools/bake_skinned_glb.py: cada cuadro del ciclo de caminata es una malla.
//  El motor dibuja el cuadro que toca segun el tiempo; la caminata va "en su lugar" y el programa los mueve por un
//  circuito en las orillas de la calle (carril izquierdo hacia el templo, derecho de regreso), entre los carros y los puestos.
//  La velocidad de la animacion se ajusta a la del paso para que no patinen:  rapidez = v / velocidad_de_paso.
// ============================================================
struct WalkerType {
	const char* file;
	float height;         // alto en metros
	float strideSpeed;    // m/s que avanza el personaje en la animacion original (movimiento de la cadera)
	float animSecs;       // duracion del ciclo
	int frames;           // cuadros horneados
	Model* model = nullptr;
	int P = 1;            // mallas por cuadro
	glm::vec3 center = glm::vec3(0.0f);
	float minY = 0.0f, scale = 1.0f;
};
WalkerType walkerTypes[4] = {
	{ "Models/Caminantes/caminante_hombre_japones.glb", 1.72f, 1.41f, 1.167f, 24 },
	{ "Models/Caminantes/caminante_mr_man.glb",         1.78f, 1.20f, 1.033f, 24 },
	{ "Models/Caminantes/caminante_hombre2.glb",        1.72f, 1.40f, 1.000f, 24 },
	{ "Models/Caminantes/caminante_mujer.glb",          1.62f, 1.40f, 1.000f, 24 },
};
struct Walker { int type; float s0, speed, phase, lateral; };
std::vector<Walker> walkers;
const int   WALKER_COUNT = 12;
const float WALK_LANE = 4.3f, WALK_Z_FAR = -23.5f, WALK_Z_NEAR = 20.5f;

// Circuito generico: recta izquierda (-Z), media vuelta, recta derecha (+Z), media vuelta. Devuelve (x, 0, z) y la tangente (x, z).
float LoopLen(float lane, float zFar, float zNear) { return 2.0f * (zNear - zFar) + 2.0f * PI_F * lane; }
// Calcula la posición y la tangente de un personaje que camina siguiendo un circuito cerrado.
glm::vec3 LoopPos(float s, float lane, float zFar, float zNear, glm::vec2& tan)
{
	const float straight = zNear - zFar, arc = PI_F * lane, L = LoopLen(lane, zFar, zNear);
	s = fmodf(s, L); if (s < 0.0f) s += L;
	glm::vec2 pos;
	if (s < straight)                  { pos = glm::vec2(-lane, zNear - s);  tan = glm::vec2(0.0f, -1.0f); }
	else if (s < straight + arc)       { float f = (s - straight) / lane;  pos = glm::vec2(-lane * cosf(f), zFar - lane * sinf(f));  tan = glm::vec2(sinf(f), -cosf(f)); }
	else if (s < 2.0f * straight + arc){ pos = glm::vec2(lane, zFar + (s - straight - arc));  tan = glm::vec2(0.0f, 1.0f); }
	else                               { float f = (s - 2.0f * straight - arc) / lane;  pos = glm::vec2(lane * cosf(f), zNear + lane * sinf(f));  tan = glm::vec2(-sinf(f), cosf(f)); }
	return glm::vec3(pos.x, 0.0f, pos.y);
}

// Carga los modelos de los peatones y obtiene los datos necesarios para reproducir sus ciclos de caminata.
void LoadWalkers()
{
	for (WalkerType& wt : walkerTypes) {
		wt.model = new Model();
		wt.model->LoadModel(wt.file, -1e30f, true);   // conservar una malla por cuadro
		if (!wt.model->IsLoaded()) { printf("NO CARGO caminante: %s\n", wt.file); wt.model = nullptr; continue; }
		wt.P = (int)wt.model->GetMeshCount() / wt.frames;
		glm::vec3 mn = wt.model->GetBoundsMin(), mx = wt.model->GetBoundsMax();
		wt.center = 0.5f * (mn + mx);
		wt.minY = mn.y;
		wt.scale = wt.height / (mx.y - mn.y);
	}
	std::mt19937 rng(21);
	auto U = [&](float lo, float hi) { return std::uniform_real_distribution<float>(lo, hi)(rng); };
	const float L = LoopLen(WALK_LANE, WALK_Z_FAR, WALK_Z_NEAR);
	for (int i = 0; i < WALKER_COUNT; i++)
		walkers.push_back({ i % 4, i * L / WALKER_COUNT + U(-2.0f, 2.0f), U(0.85f, 1.15f), U(0.0f, 1.0f), U(-0.30f, 0.0f) });
}

// Faroles del templo: cuelgan de los bordes de sus aleros (coordenadas del modelo del templo, antes de escalarlo).
// Alturas de los aleros medidas sobre la malla: 620, 1340 y 1940 de 3738; el frente del templo es +Z.
// Agrega faroles decorativos alrededor de la zona del templo para reforzar la iluminación ambiental.
void AddTempleLanterns()
{
	const Prop* tp = nullptr;
	for (const Prop& pr : props) if (pr.id == 7) tp = &pr;
	if (!tp || !tp->model || !tp->model->IsLoaded()) return;
	std::mt19937 rng(77);
	std::uniform_real_distribution<float> rnd(0.0f, 2.0f * PI_F);
	size_t before = lanterns.size();
	auto add = [&](float x, float y, float z) {
		Lantern l = { glm::vec3(tp->matrix * glm::vec4(x, y, z, 1.0f)), rnd(rng), rnd(rng) };
		l.scale = 1.2f;
		lanterns.push_back(l);
	};
	for (int k = 0; k < 7; k++) add(-800.0f + k * 266.7f, 620.0f, 775.0f);                 // alero 1: frente
	for (int k = 0; k < 4; k++) { float z = 600.0f - k * 400.0f; add(-855.0f, 620.0f, z); add(855.0f, 620.0f, z); }   // alero 1: costados
	for (int k = 0; k < 5; k++) add(-750.0f + k * 375.0f, 1340.0f, 725.0f);                // alero 2: frente
	for (int k = 0; k < 3; k++) { float z = 450.0f - k * 450.0f; add(-825.0f, 1340.0f, z); add(825.0f, 1340.0f, z); }
	for (int k = 0; k < 5; k++) add(-600.0f + k * 300.0f, 1940.0f, 560.0f);                // alero 3: frente
	printf("  %d faroles colgados del templo\n", (int)(lanterns.size() - before));
}

// ============================================================
//  INTERFAZ 2D (Textures/UI) Y CARTAS DE INFORMACION
//  - El aviso hud_aviso_e.png aparece abajo al centro cuando hay algo con lo que interactuar (la pileta o un punto
//    de informacion). En la pileta E inicia el juego; en los puntos de informacion E abre una carta flotante.
//  - Cartas: Textures/Cards/card_<id>.png (hoy son provisionales: basta reemplazar el archivo, mismo nombre).
//    La carta es un cartel que flota sobre el objeto, siempre de frente a la camara; se cierra con E o al alejarte.
// ============================================================
struct InfoPoint {
	const char* id; glm::vec3 pos; float radius, cardY, cardW, cardFwd;   // cardFwd: cuanto se adelanta la carta hacia la camara
	const char* folder; int first, count;                                  // carpeta de Textures/UI y numeracion de sus paginas (N.png)
};
InfoPoint infoPoints[] = {
	{ "historia",   glm::vec3(0.0f, 0.0f, 23.5f),   6.5f, 3.4f, 3.8f, 1.2f, "Gion Matsuri-historia",   1,  5 },   // entrada (torii)
	{ "fechas",     glm::vec3(0.0f, 0.0f, -25.0f),  10.0f, 3.1f, 4.2f, 2.4f, "Gion Matsuri-fechas",     6, 13 },   // templo
	{ "elementos",  glm::vec3(0.0f),                5.5f, 4.8f, 4.2f, 2.2f, "Gion Matsuri-elementos", 19,  8 },   // sigue al carro mas cercano
};
const int INFO_COUNT = (int)(sizeof(infoPoints) / sizeof(infoPoints[0]));
const int INFO_CART = 2;
// Paginas de todas las series, en orden; infoPoints[i] usa [first_idx, first_idx + count)
std::vector<std::string> cardPaths;     // (con reserve: Texture guarda el puntero al texto)
std::vector<Texture> cardPages;
std::vector<char> cardPageOK;
int infoPageBase[INFO_COUNT];
int infoNear = -1, infoOpen = -1, infoPage = 0;
float infoT = 0.0f;
glm::vec3 infoNearPos(0.0f), infoOpenPos(0.0f);

Texture uiTiempo, uiPuntos, uiAviso, uiInstr, uiResult, uiDigitos, uiNav, uiCtrl;
bool uiCtrlOK = false, controlsOn = true, msaaOn = true, qualityDirty = true;
bool uiOK = false, uiNavOK = false;

// Carga los recursos gráficos utilizados por los elementos de interfaz y paneles informativos.
void LoadUI()
{
	uiTiempo  = Texture("Textures/UI/hud_tiempo.png");        bool a = uiTiempo.LoadTextureA();
	uiPuntos  = Texture("Textures/UI/hud_puntos.png");        bool b = uiPuntos.LoadTextureA();
	uiAviso   = Texture("Textures/UI/hud_aviso_e.png");       bool c = uiAviso.LoadTextureA();
	uiInstr   = Texture("Textures/UI/hud_instrucciones.png"); bool d = uiInstr.LoadTextureA();
	uiResult  = Texture("Textures/UI/hud_resultado.png");     bool e = uiResult.LoadTextureA();
	uiDigitos = Texture("Textures/UI/hud_digitos.png");       bool f = uiDigitos.LoadTextureA();
	uiOK = a && b && c && d && e && f;
	if (!uiOK) printf("Faltan imagenes de Textures/UI: el menu no se mostrara\n");
	uiCtrl = Texture("Textures/UI/hud_controles.png");       uiCtrlOK = uiCtrl.LoadTextureA();
	uiNav = Texture("Textures/UI/hud_cartas_nav.png");        uiNavOK = uiNav.LoadTextureA();
	cardPaths.reserve(64); cardPages.reserve(64); cardPageOK.reserve(64);
	for (int i = 0; i < INFO_COUNT; i++) {
		infoPageBase[i] = (int)cardPages.size();
		for (int k = 0; k < infoPoints[i].count; k++) {
			cardPaths.push_back(std::string("Textures/UI/") + infoPoints[i].folder + "/" + std::to_string(infoPoints[i].first + k) + ".png");
			cardPages.push_back(Texture(cardPaths.back().c_str()));
			cardPageOK.push_back(cardPages.back().LoadTextureA() ? 1 : 0);
		}
	}
}

// Punto de informacion mas cercano dentro de su radio; abre/cierra la carta con E (la pileta tiene prioridad: ahi E es el juego)
// Actualiza la detección de puntos de interés cercanos y el estado de las tarjetas informativas.
void UpdateInfo(float dt, bool ePressed, int navDir)
{
	glm::vec3 cp = camera.getCameraPosition();
	if (gameState != GS_IDLE) { infoNear = -1; infoOpen = -1; return; }
	bool nearPool = glm::length(glm::vec2(cp.x - POOL_C.x, cp.z - POOL_C.z)) < GAME_NEAR_R;
	auto cartPos = [&](float& dist) {          // posicion del carro mas cercano a la camara
		glm::vec3 best(0.0f); dist = 1e9f;
		for (int ci = 0; ci < CART_COUNT; ci++) {
			float h; glm::vec3 p = CartPath(cartDist - ci * CART_GAP, h);
			float d = glm::length(glm::vec2(cp.x - p.x, cp.z - p.z));
			if (d < dist) { dist = d; best = p; }
		}
		return best;
	};
	infoNear = -1;
	float bestD = 1e9f;
	for (int i = 0; i < INFO_COUNT; i++) {
		glm::vec3 p = infoPoints[i].pos; float d;
		if (i == INFO_CART) p = cartPos(d);
		else d = glm::length(glm::vec2(cp.x - p.x, cp.z - p.z));
		if (d < infoPoints[i].radius && d < bestD) { bestD = d; infoNear = i; infoNearPos = p; }
	}
	if (nearPool) infoNear = -1;

	if (infoOpen >= 0) {
		if (infoOpen == INFO_CART) { float d; infoOpenPos = cartPos(d); }    // la carta acompana al carro
		infoT += dt;
		if (glm::length(glm::vec2(cp.x - infoOpenPos.x, cp.z - infoOpenPos.z)) > infoPoints[infoOpen].radius * 1.5f) infoOpen = -1;
	}
	if (infoOpen >= 0 && navDir != 0) {       // flechas: pagina siguiente / anterior de la misma serie (da la vuelta)
		int n = infoPoints[infoOpen].count;
		infoPage = ((infoPage + navDir) % n + n) % n;
		infoT = 0.0f;
	}
	if (ePressed && !nearPool) {
		if (infoOpen >= 0) infoOpen = -1;
		else if (infoNear >= 0) { infoOpen = infoNear; infoOpenPos = infoNearPos; infoT = 0.0f; infoPage = 0; }
	}
}

// ============================================================
//  FUEGOS ARTIFICIALES (hanabi)
//  En Gion se cierra la noche con hanabi: aqui suben cohetes detras del templo y estallan sobre el.
//  Sistema de particulas: cada cohete (Shell) sube con gravedad real y estalla en el apice; cada chispa (Spark) sale con
//  una velocidad inicial, pierde rapidez por friccion con el aire, cae y se apaga. Se dibujan como sprites con mezcla aditiva
//  (la luz suma, no tapa) y, mientras dura el destello, el estallido ilumina la calle como una luz puntual.
// ============================================================
struct Spark { glm::vec3 p, v, col; float age, life, size; };
struct Shell { glm::vec3 p, v, col; int type; float fuse, age; };
std::vector<Spark> sparks;
std::vector<Shell> shells;
float fwTimer = 0.4f;
std::mt19937 fwRng(2026);
glm::vec3 fwFlashPos(0.0f), fwFlashCol(1.0f);
float fwFlashAge = 99.0f;
Texture fwSprite;
bool fwSpriteOK = false;
bool fireworksOn = false;
int dbgSkipId = -1; int dbgLights = 5; bool dbgNoMsaa = false;
int quality = 1;   // 0 alta, 1 media (inicio), 2 baja: se cambia con F2
int dbgSkip = 0;   // --skip N: pruebas de rendimiento (1 props, 2 duenos, 4 caminantes, 8 taiko, 16 carros, 32 faroles, 64 arboles)     // se encienden con H
const glm::vec3 FW_COLORS[] = {
	{ 1.00f, 0.25f, 0.20f }, { 1.00f, 0.75f, 0.25f }, { 1.00f, 0.40f, 0.70f },
	{ 0.35f, 0.75f, 1.00f }, { 0.45f, 1.00f, 0.45f }, { 1.00f, 1.00f, 0.90f }, { 0.75f, 0.45f, 1.00f },
};

// Actualiza el lanzamiento y la evolución de los fuegos artificiales, incluyendo sus partículas.
void UpdateFireworks(float dt)
{
	if (!fireworksOn) { shells.clear(); sparks.clear(); fwFlashAge = 99.0f; return; }
	dt = glm::min(dt, 0.1f);
	auto U = [&](float lo, float hi) { return std::uniform_real_distribution<float>(lo, hi)(fwRng); };
	const float G = 9.8f;

	fwTimer -= dt;
	if (fwTimer <= 0.0f) {                      // nuevo cohete cada 1.5 a 3.5 s (a veces dos juntos)
		fwTimer = U(1.5f, 3.5f);
		int n = (U(0.0f, 1.0f) < 0.3f) ? 2 : 1;
		for (int i = 0; i < n; i++) {
			Shell sh;
			sh.p = glm::vec3(U(-15.0f, 15.0f), 0.0f, U(-38.0f, -33.0f));
			float apex = U(26.0f, 34.0f);
			float v0 = sqrtf(2.0f * G * apex);
			sh.v = glm::vec3(U(-1.5f, 1.5f), v0, U(-0.5f, 1.5f));
			sh.fuse = v0 / G * 0.97f;
			sh.age = 0.0f;
			sh.type = (int)U(0.0f, 2.999f);
			sh.col = FW_COLORS[(int)U(0.0f, 6.999f)];
			shells.push_back(sh);
		}
	}

	for (size_t i = 0; i < shells.size();) {
		Shell& sh = shells[i];
		sh.age += dt;
		sh.v.y -= G * dt;
		sh.p += sh.v * dt;
		for (int k = 0; k < 2; k++)             // estela dorada
			sparks.push_back({ sh.p, glm::vec3(U(-0.6f, 0.6f), U(-1.5f, -0.3f), U(-0.6f, 0.6f)), glm::vec3(1.0f, 0.75f, 0.35f), 0.0f, U(0.35f, 0.7f), 0.55f });
		if (sh.age >= sh.fuse) {                // estallido
			const int N = (sh.type == 2) ? 90 : (sh.type == 1 ? 64 : 110);
			glm::vec3 c2 = FW_COLORS[(int)U(0.0f, 6.999f)];
			glm::vec3 axis = glm::normalize(glm::vec3(U(-0.4f, 0.4f), U(-0.3f, 0.3f), 1.0f));   // normal del anillo
			glm::vec3 ax1 = glm::normalize(glm::cross(axis, glm::vec3(0.0f, 1.0f, 0.0f)));
			glm::vec3 ax2 = glm::cross(axis, ax1);
			for (int k = 0; k < N; k++) {
				glm::vec3 d; float speed, life, size = 1.1f;
				glm::vec3 col = (k % 3 == 0) ? c2 : sh.col;
				if (sh.type == 1) {             // anillo
					float a = 2.0f * PI_F * k / N;
					d = ax1 * cosf(a) + ax2 * sinf(a);
					speed = 20.0f; life = U(1.8f, 2.2f);
				} else {                        // esfera (uniforme)
					float z = U(-1.0f, 1.0f), a = U(0.0f, 2.0f * PI_F), r = sqrtf(1.0f - z * z);
					d = glm::vec3(r * cosf(a), z, r * sinf(a));
					if (sh.type == 2) { speed = U(10.0f, 17.0f); life = U(2.8f, 3.6f); col = glm::vec3(1.0f, 0.72f, 0.28f); size = 0.9f; }   // "sauce" dorado: cae lento
					else { speed = U(17.0f, 23.0f); life = U(1.8f, 2.4f); }
				}
				sparks.push_back({ sh.p, d * speed, col, 0.0f, life, size });
			}
			fwFlashPos = sh.p; fwFlashCol = sh.col; fwFlashAge = 0.0f;
			shells.erase(shells.begin() + i);
			continue;
		}
		i++;
	}

	for (size_t i = 0; i < sparks.size();) {
		Spark& sp = sparks[i];
		sp.age += dt;
		if (sp.age >= sp.life) { sparks[i] = sparks.back(); sparks.pop_back(); continue; }
		sp.v.y -= 5.0f * dt;                    // gravedad aparente (las chispas son ligeras)
		sp.v *= expf(-1.5f * dt);               // friccion con el aire
		sp.p += sp.v * dt;
		i++;
	}
	fwFlashAge += dt;
}

// ============================================================
//  MAIN
// ============================================================
// Guarda el framebuffer actual como .tga (24 bits). Sirve para revisar la escena sin depender de capturas de pantalla.
// Captura el contenido del framebuffer y lo guarda como una imagen TGA en la ruta indicada.
void SaveScreenshotTGA(const char* path)
{
	int w = (int)mainWindow.getBufferWidth(), h = (int)mainWindow.getBufferHeight();
	std::vector<unsigned char> px((size_t)w * h * 3);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadBuffer(GL_BACK);
	glReadPixels(0, 0, w, h, GL_BGR, GL_UNSIGNED_BYTE, px.data());
	unsigned char hdr[18] = { 0, 0, 2, 0,0,0,0,0, 0,0,0,0, (unsigned char)(w & 255), (unsigned char)(w >> 8), (unsigned char)(h & 255), (unsigned char)(h >> 8), 24, 0 };
	FILE* f = nullptr;
	if (fopen_s(&f, path, "wb") != 0 || !f) return;
	fwrite(hdr, 1, 18, f);
	fwrite(px.data(), 1, px.size(), f);
	fclose(f);
}

// Vistas para el modo de revision automatica:  ProyectoFinal.exe --shots <carpeta>
// Coloca la camara en cada vista, guarda una captura y cierra el programa.
const CamPreset AUTO_VIEWS[] = {
	{ glm::vec3(0.0f, EYE_HEIGHT,  31.0f), -90.0f,   3.0f },   // 0 afuera de la entrada
	{ glm::vec3(0.0f, EYE_HEIGHT,  18.0f), -90.0f,   0.0f },   // 1
	{ glm::vec3(0.0f, EYE_HEIGHT,   6.0f), -90.0f,   0.0f },   // 2
	{ glm::vec3(0.0f, EYE_HEIGHT,  -6.0f), -90.0f,   0.0f },   // 3
	{ glm::vec3(0.0f, EYE_HEIGHT, -20.0f), -90.0f,   0.0f },   // 4
	{ glm::vec3(0.0f, EYE_HEIGHT, -28.0f),  90.0f,   0.0f },   // 5 mirando de regreso a la entrada
	{ glm::vec3(0.0f, 24.0f,  14.0f),      -90.0f, -55.0f },   // 6 vista alta (mitad entrada)
	{ glm::vec3(0.0f, 24.0f, -14.0f),      -90.0f, -55.0f },   // 7 vista alta (mitad fondo)
	{ glm::vec3(0.0f, EYE_HEIGHT,  10.0f), 180.0f,   4.0f },   // 8 lado izquierdo
	{ glm::vec3(0.0f, EYE_HEIGHT,  10.0f),   0.0f,   4.0f },   // 9 lado derecho
	{ glm::vec3(0.0f, 2.5f, -24.0f),        90.0f,   4.0f },   // 10 frente de los carros
	{ glm::vec3(8.0f, 9.0f,  10.0f),      -133.0f, -23.0f },   // 11 carro desde arriba, 3/4
	{ glm::vec3(0.0f, EYE_HEIGHT, 30.0f), -90.0f,   8.0f },   // 12 desde la entrada hacia los carros
	{ glm::vec3(-0.6f, 1.5f, -5.5f),      180.0f,  -6.0f },   // 13 musico de taiko (de frente)
	{ glm::vec3(-0.6f, 1.5f, -5.5f),      180.0f,  -6.0f },   // 14 (fase distinta de la animacion)
	{ glm::vec3(-0.6f, 1.5f, -5.5f),      180.0f,  -6.0f },   // 15
	{ glm::vec3(-0.6f, 1.5f, -5.5f),      180.0f,  -6.0f },   // 16
	{ glm::vec3(-1.8f, 1.6f, -4.2f),     -141.0f, -16.0f },   // 17 musico, vista 3/4
	{ glm::vec3(1.0f, 1.6f, 6.6f),        -36.0f, -22.0f },   // 18 pileta desde la calle
	{ glm::vec3(3.7f, 2.3f, 6.3f),        -90.0f, -50.0f },   // 19 pileta desde arriba
	{ glm::vec3(3.7f, 2.3f, 6.3f),        -90.0f, -50.0f },   // 20 (otra fase de la animacion)
	{ glm::vec3(1.2f, 1.5f, 4.5f),          0.0f,  -3.0f },   // 21 pileta y duenos
	{ glm::vec3(0.0f, 1.6f, 27.0f),       -90.0f,   3.0f },   // 22 carros hacia la entrada
	{ glm::vec3(0.0f, 1.7f, -22.0f),      -90.0f,   5.0f },   // 23 templo al fondo
	{ glm::vec3(-2.5f, 1.6f, 11.0f),      180.0f,   0.0f },   // 24 stands izquierdos
	{ glm::vec3(2.5f, 1.6f, 6.0f),          0.0f,   0.0f },   // 25 stands derechos
	{ glm::vec3(0.0f, 40.0f, 0.0f),       -90.0f, -89.0f },   // 26 circuito desde arriba
	{ glm::vec3(-0.6f, 1.5f, -5.5f),      180.0f,  -6.0f },   // 27 tres taikos
	{ glm::vec3(0.0f, 1.6f, 22.0f),       -90.0f,   2.0f },   // 28 calle con gente caminando
	{ glm::vec3(0.0f, 2.2f, 8.0f),        -90.0f,  -4.0f },   // 29 idem, desde mas arriba
	{ glm::vec3(0.0f, 1.7f, -26.0f),      -90.0f,  12.0f },   // 30 templo grande con sus faroles
	{ glm::vec3(0.0f, 1.6f, 14.0f),         0.0f,   3.0f },   // 31 edificios del lado derecho
	{ glm::vec3(0.0f, 1.6f, -10.0f),      180.0f,   3.0f },   // 32 edificios del lado izquierdo
	{ glm::vec3(0.0f, 14.0f, -34.0f),     -90.0f, -18.0f },   // 33 templo desde arriba
};

// Tomas compuestas para capturas:  ProyectoFinal.exe --shots <carpeta> --views beauty [--size 1920 1080]
const CamPreset BEAUTY_VIEWS[] = {
	{ glm::vec3( 0.0f,  1.6f,  34.0f),  -90.0f,   6.0f },   //  0 entrada: el torii enmarca la calle
	{ glm::vec3( 0.0f,  0.7f,  31.5f),  -90.0f,  24.0f },   //  1 torii desde abajo
	{ glm::vec3( 0.0f,  1.7f,  12.0f),  -90.0f,   1.0f },   //  2 calle con faroles que se alejan
	{ glm::vec3( 0.0f,  1.5f,   2.0f),  -90.0f,  32.0f },   //  3 cuerdas de faroles contra el cielo
	{ glm::vec3( 3.2f,  1.6f,  -3.0f),  -49.0f,  -3.0f },   //  4 puesto de ramen
	{ glm::vec3( 2.5f,  1.6f,  14.0f),  -33.0f,  -3.0f },   //  5 kiosco de flores
	{ glm::vec3(-1.5f,  1.4f,  -3.0f), -130.0f,  -3.0f },   //  6 altar
	{ glm::vec3( 0.5f,  1.6f,  12.0f),  -68.0f,   3.0f },   //  7 tiendas del lado derecho
	{ glm::vec3( 0.0f, 30.0f,  40.0f),  -90.0f, -38.0f },   //  8 vista aerea general
	{ glm::vec3(22.0f, 16.0f,  26.0f), -130.0f, -25.0f },   //  9 aerea 3/4
	{ glm::vec3( 0.0f,  1.7f, -27.0f),   90.0f,   3.0f },   // 10 de regreso hacia la entrada
	{ glm::vec3( 0.0f,  1.6f, -12.0f),  -90.0f,   3.0f },   // 11 torii interior y edificio del fondo
	{ glm::vec3( 1.0f,  3.3f,  24.0f),  -90.0f,   6.0f },   // 12 faroles de cerca
};

// Punto de entrada: inicializa ventana, OpenGL, recursos, modelos, audio y el ciclo principal de actualización y renderizado.
int main(int argc, char** argv)
{
	const char* shotsDir = nullptr;
	const char* gameShotDir = nullptr;   // --gameshot <carpeta>: prueba automatica del minijuego
	int gsFrame = 0;
	const char* cardShotDir = nullptr;   // --cardshot <carpeta>: prueba automatica de las cartas flotantes
	int csFrame = 0, fsFrame = 0;
	const char* fireShotDir = nullptr;   // --fireshot <carpeta>: prueba automatica de los fuegos artificiales
	float gsT0 = 0.0f;
	bool beauty = false;
	int winW = 1366, winH = 768;
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--shots") == 0 && i + 1 < argc) shotsDir = argv[i + 1];
		if (strcmp(argv[i], "--gameshot") == 0 && i + 1 < argc) gameShotDir = argv[i + 1];
		if (strcmp(argv[i], "--cardshot") == 0 && i + 1 < argc) cardShotDir = argv[i + 1];
		if (strcmp(argv[i], "--skip") == 0 && i + 1 < argc) dbgSkip = atoi(argv[i + 1]);
		if (strcmp(argv[i], "--lights") == 0 && i + 1 < argc) dbgLights = atoi(argv[i + 1]);
		if (strcmp(argv[i], "--nomsaa") == 0) dbgNoMsaa = true;
		if (strcmp(argv[i], "--skipid") == 0 && i + 1 < argc) dbgSkipId = atoi(argv[i + 1]);
		if (strcmp(argv[i], "--hq") == 0) gUseOptimizedModels = false;   // modelos originales (sin Models/Opt)
		if (strcmp(argv[i], "--fireshot") == 0 && i + 1 < argc) fireShotDir = argv[i + 1];
		if (strcmp(argv[i], "--views") == 0 && i + 1 < argc && strcmp(argv[i + 1], "beauty") == 0) beauty = true;
		if (strcmp(argv[i], "--size") == 0 && i + 2 < argc) { winW = atoi(argv[i + 1]); winH = atoi(argv[i + 2]); }
	}
	int shotFrame = 0;
	mainWindow = Window(winW, winH);
	if (mainWindow.Initialise() != 0)
		return 1;

	// ─── Audio (opcional): si no existe el archivo, simplemente no suena ───
	ma_sound bgm, taikoTrack;
	bool bgmLoaded = false, taikoTrackLoaded = false;
	bool audioReady = (ma_engine_init(NULL, &audioEngine) == MA_SUCCESS);
	if (audioReady)
	{
		// Pistas de Audio/: la de "ohayashi" es la musica de fondo; la de taiko/koto ("kaazoom", "taiko", "koto") suena en 3D
		// desde los tambores (se oye mas fuerte al acercarse). Si solo hay una pista, es la de fondo.
		std::string bgmFile, taikoFile;
		try {
			for (const auto& en : std::filesystem::directory_iterator("Audio")) {
				std::string ext = en.path().extension().string(), nm = en.path().stem().string();
				for (char& ch : ext) ch = (char)tolower((unsigned char)ch);
				for (char& ch : nm) ch = (char)tolower((unsigned char)ch);
				if (nm == "taiko_hit" || nm == "pez_salto" || nm == "red_agua") continue;
				if (ext != ".mp3" && ext != ".ogg" && ext != ".wav" && ext != ".flac") continue;
				std::string f = en.path().generic_string();
				bool isTaiko = nm.find("kaazoom") != std::string::npos || nm.find("taiko") != std::string::npos || nm.find("koto") != std::string::npos;
				if (isTaiko && nm.find("ohayashi") == std::string::npos) { if (taikoFile.empty()) taikoFile = f; }
				else if (bgmFile.empty()) bgmFile = f;
			}
		} catch (...) {}
		if (bgmFile.empty() && !taikoFile.empty()) { bgmFile = taikoFile; taikoFile.clear(); }
		if (bgmFile.empty()) printf("No hay musica en Audio/ (pon un .mp3 o .ogg ahi)\n");
		else printf("Musica de fondo: %s\n", bgmFile.c_str());
		if (!taikoFile.empty() && ma_sound_init_from_file(&audioEngine, taikoFile.c_str(), MA_SOUND_FLAG_STREAM, NULL, NULL, &taikoTrack) == MA_SUCCESS) {
			ma_sound_set_position(&taikoTrack, TAIKO_POS[1].x, 1.2f, TAIKO_POS[1].z);
			ma_sound_set_attenuation_model(&taikoTrack, ma_attenuation_model_inverse);
			ma_sound_set_min_distance(&taikoTrack, 2.5f);
			ma_sound_set_max_distance(&taikoTrack, 45.0f);
			ma_sound_set_rolloff(&taikoTrack, 1.4f);
			ma_sound_set_looping(&taikoTrack, MA_TRUE);
			ma_sound_set_volume(&taikoTrack, 1.0f);
			ma_sound_start(&taikoTrack);
			taikoTrackLoaded = true;
			printf("Musica de taiko (3D): %s\n", taikoFile.c_str());
		}
		if (!bgmFile.empty() && ma_sound_init_from_file(&audioEngine, bgmFile.c_str(),
			MA_SOUND_FLAG_STREAM, NULL, NULL, &bgm) == MA_SUCCESS)
		{
			ma_sound_set_looping(&bgm, MA_TRUE);
			ma_sound_set_volume(&bgm, 0.3f);
			ma_sound_start(&bgm);
			bgmLoaded = true;
		}
	}

	// Golpe de taiko con posicion 3D (opcional: si no existe Audio/taiko_hit.wav no suena)
	ma_sound taikoHit;
	bool taikoHitLoaded = false;
	if (audioReady && ma_sound_init_from_file(&audioEngine, "Audio/taiko_hit.wav", MA_SOUND_FLAG_DECODE, NULL, NULL, &taikoHit) == MA_SUCCESS) {
		ma_sound_set_position(&taikoHit, TAIKO_POS[1].x + 0.3f * TAIKO_SCALE, 0.9f, TAIKO_POS[1].z);
		ma_sound_set_min_distance(&taikoHit, 1.5f);
		ma_sound_set_max_distance(&taikoHit, 25.0f);
		ma_sound_set_attenuation_model(&taikoHit, ma_attenuation_model_inverse);
		taikoHitSound = &taikoHit;
		taikoHitLoaded = true;
	}

	CreateObjects();
	CreateShaders();
	InitLanterns();
	AddScenery();
	LoadProps();
	AddTempleLanterns();
	LoadCart();
	LoadTaiko();
	LoadPool();
	fwSprite = Texture("Textures/Efectos/chispa.png"); fwSpriteOK = fwSprite.LoadTextureA();
	LoadWalkers();
	LoadUI();

	SetCameraPreset(CAM_PRESETS[0]);

	// ─── Texturas ─── (sin archivo: textura blanca + color de reemplazo)
	plainTexture = Texture("Textures/plain.png");
	plainTexture.LoadTextureA();
	pisoTexture = Texture("Textures/Calle/piedra.jpg");
	bool pisoLoaded = pisoTexture.LoadTextureA();
	for (int i = 0; i < 5; i++) {
		swatchTex[i] = Texture(SWATCHES[i].file);
		swatchTex[i].LoadTextureA();
	}
	bool showMarkers = false; // tecla M: numeros de los modelos + muestrario de texturas

	// ─── Skybox nocturno (generado desde el HDRI con tools/hdri_to_cubemap.py) ───
	// Orden de los indices del cubemap en OpenGL: +X, -X, +Y, -Y, +Z, -Z  =  rt, lf, up, dn, bk, ft
	// (el script tools/hdri_to_cubemap.py nombra up = +Y y dn = -Y)
	std::vector<std::string> skyboxFaces;
	skyboxFaces.push_back("Textures/Skybox/rt.png");
	skyboxFaces.push_back("Textures/Skybox/lf.png");
	skyboxFaces.push_back("Textures/Skybox/up.png");
	skyboxFaces.push_back("Textures/Skybox/dn.png");
	skyboxFaces.push_back("Textures/Skybox/bk.png");
	skyboxFaces.push_back("Textures/Skybox/ft.png");
	skybox = Skybox(skyboxFaces);

	Material_brillante = Material(4.0f, 256);
	Material_opaco     = Material(0.3f, 4);

	mainLight = DirectionalLight(1.0f, 1.0f, 1.0f, 0.3f, 0.3f, 0.0f, 0.0f, -1.0f);

	// Linterna de la camara: apagada por defecto (tecla L); la escena se ilumina con faroles
	unsigned int spotLightCount = 0;
	spotLights[0] = SpotLight(1.0f, 1.0f, 1.0f, 0.0f, 2.0f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 5.0f);
	bool flashlightOn = false;

	GLuint uniformProjection = 0, uniformModel = 0, uniformView = 0, uniformEyePosition = 0,
		uniformSpecularIntensity = 0, uniformShininess = 0, uniformTextureOffset = 0;
	GLuint uniformColor = 0, uniformAlpha = 0, uniformNoLighting = 0;

	glm::mat4 projection = glm::perspective(45.0f, (GLfloat)mainWindow.getBufferWidth() / mainWindow.getBufferHeight(), 0.1f, 1000.0f);

	glm::vec3 color(1.0f);
	glm::vec2 toffset(0.0f);

	glEnable(GL_CULL_FACE);

	// ─── Loop principal ───────────────────────────────────────────────
	while (!mainWindow.getShouldClose())
	{
		GLfloat now = (GLfloat)glfwGetTime();
		deltaTime = now - lastTime;
		lastTime = now;
		float t = (float)glfwGetTime(); // tiempo absoluto para las animaciones procedurales

		glfwPollEvents();

		// --- Entrada ---
		if (KeyPressedOnce(GLFW_KEY_V)) freeCamera = !freeCamera;
		if (KeyPressedOnce(GLFW_KEY_M)) showMarkers = !showMarkers;
		if (KeyPressedOnce(GLFW_KEY_L)) flashlightOn = !flashlightOn;
		if (KeyPressedOnce(GLFW_KEY_1) && gameState == GS_IDLE) SetCameraPreset(CAM_PRESETS[0]);
		if (KeyPressedOnce(GLFW_KEY_2) && gameState == GS_IDLE) SetCameraPreset(CAM_PRESETS[1]);
		if (KeyPressedOnce(GLFW_KEY_3) && gameState == GS_IDLE) SetCameraPreset(CAM_PRESETS[2]);

		if (gameShotDir) {                    // prueba automatica del juego: colocar al jugador junto a la pileta e iniciar
			gsFrame++;
			if (gsFrame == 2) camera = Camera(glm::vec3(POOL_C.x - 1.6f, EYE_HEIGHT, POOL_C.z + 1.2f), glm::vec3(0.0f, 1.0f, 0.0f), -20.0f, -25.0f, CAM_MOVE_SPEED, CAM_TURN_SPEED);
			if (gsFrame == 40) { gameSecs = 14.0f; autoGame = true; StartGame(); }
			if (gsFrame == 40) gsT0 = (float)glfwGetTime();
		}
		if (cardShotDir) {                    // prueba automatica de las cartas: acercarse al taiko y pulsar E
			csFrame++;
			if (csFrame == 2) camera = Camera(glm::vec3(0.0f, EYE_HEIGHT, -17.0f), glm::vec3(0.0f, 1.0f, 0.0f), -90.0f, 8.0f, CAM_MOVE_SPEED, CAM_TURN_SPEED);
		}
		if (fireShotDir) {
			fsFrame++;
			if (fsFrame == 2) camera = Camera(glm::vec3(5.0f, EYE_HEIGHT, -5.5f), glm::vec3(0.0f, 1.0f, 0.0f), -22.0f, -3.0f, CAM_MOVE_SPEED, CAM_TURN_SPEED);
			if (fsFrame == 100) camera = Camera(glm::vec3(-3.9f, EYE_HEIGHT, -3.2f), glm::vec3(0.0f, 1.0f, 0.0f), -160.0f, -8.0f, CAM_MOVE_SPEED, CAM_TURN_SPEED);
		}
		bool ePressed = KeyPressedOnce(GLFW_KEY_E) || (cardShotDir && csFrame == 22);
		UpdateGame(deltaTime, projection, ePressed);
		int navDir = 0;
		if (KeyPressedOnce(GLFW_KEY_RIGHT)) navDir++;
		if (KeyPressedOnce(GLFW_KEY_LEFT)) navDir--;
		if (cardShotDir && csFrame == 40) navDir = 1;
		UpdateInfo(deltaTime, ePressed, navDir);
		if (gameState == GS_IDLE) {
			camera.keyControl(mainWindow.getsKeys(), deltaTime);
			camera.mouseControl(mainWindow.getXChange(), mainWindow.getYChange());
			ConstrainCamera();
		}
		else { mainWindow.getXChange(); mainWindow.getYChange(); }   // durante el juego la camara es fija y el mouse mueve la red

		UpdateCarts(deltaTime);
		if (KeyPressedOnce(GLFW_KEY_H)) fireworksOn = !fireworksOn;
		if (KeyPressedOnce(GLFW_KEY_G)) { quality = (quality + 1) % 3; qualityDirty = true; }
		if (qualityDirty) {
			qualityDirty = false;
			dbgLights = (quality == 0) ? 8 : (quality == 1 ? 5 : 3);
			msaaOn = (quality == 0);
			if (msaaOn) glEnable(GL_MULTISAMPLE); else glDisable(GL_MULTISAMPLE);
			printf("Calidad grafica: %s (luces %d, antialiasing %s)\n", quality == 0 ? "alta" : (quality == 1 ? "media" : "baja"), dbgLights, msaaOn ? "si" : "no");
		}
		if (KeyPressedOnce(GLFW_KEY_F1)) controlsOn = !controlsOn;
		UpdateFireworks(deltaTime);
		UpdateTaiko(deltaTime);

		if (audioReady) {   // el oyente sigue a la camara: el volumen y el paneo cambian con la posicion
			glm::vec3 cp = camera.getCameraPosition(), cd = camera.getCameraDirection();
			ma_engine_listener_set_position(&audioEngine, 0, cp.x, cp.y, cp.z);
			ma_engine_listener_set_direction(&audioEngine, 0, cd.x, cd.y, cd.z);
		}

		// --- Luz de luna (fija) ---
		mainLight = DirectionalLight(MOON_COLOR.r, MOON_COLOR.g, MOON_COLOR.b,
			moonAmbientOverride >= 0.0f ? moonAmbientOverride : MOON_AMBIENT, MOON_DIFFUSE, MOON_DIR.x, MOON_DIR.y, MOON_DIR.z);

		// --- Luces de los 8 faroles mas cercanos a la camara ---
		glm::vec3 camPos = camera.getCameraPosition();
		std::vector<int> order(lanterns.size());
		for (size_t i = 0; i < order.size(); i++) order[i] = (int)i;
		auto dist2 = [&](int i) { glm::vec3 d = lanterns[i].anchor - camPos; return glm::dot(d, d); };
		unsigned int pointLightCount = (unsigned int)std::min((size_t)std::min(MAX_POINT_LIGHTS, dbgLights), order.size());
		std::partial_sort(order.begin(), order.begin() + pointLightCount, order.end(),
			[&](int a, int b) { return dist2(a) < dist2(b); });
		for (unsigned int i = 0; i < pointLightCount; i++) {
			const Lantern& l = lanterns[order[i]];
			float I = 2.5f * (1.0f + 0.6f * (l.scale - 1.0f)) * LanternFlicker(l, t); // I0 * (0.92 + 0.08 ruido)
			glm::vec3 p = glm::vec3(LanternPivotMatrix(l, t) *
				glm::vec4(0.0f, -(LANTERN_CORD_L + LANTERN_BODY_H * 0.5f) * l.scale, 0.0f, 1.0f));
			pointLights[i] = PointLight(LANTERN_COLOR.r, LANTERN_COLOR.g, LANTERN_COLOR.b,
				0.02f, I, p.x, p.y, p.z, 1.0f, 0.22f, 0.08f);
		}

		// El destello de un estallido ilumina la calle (usa la ultima de las 8 luces puntuales)
		if (fwFlashAge < 1.3f && pointLightCount > 0)
			pointLights[pointLightCount - 1] = PointLight(fwFlashCol.r, fwFlashCol.g, fwFlashCol.b, 0.0f, 1.8f * expf(-fwFlashAge * 2.4f),
				fwFlashPos.x, fwFlashPos.y, fwFlashPos.z, 1.0f, 0.0f, 0.0009f);

		// --- Limpiar + skybox ---
		glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		skybox.SetTint(SKY_TINT.r, SKY_TINT.g, SKY_TINT.b);
		skybox.DrawSkybox(camera.calculateViewMatrix(), projection);

		// --- Shader principal y uniforms ---
		shaderList[0].UseShader();
		uniformModel         = shaderList[0].GetModelLocation();
		uniformProjection    = shaderList[0].GetProjectionLocation();
		uniformView          = shaderList[0].GetViewLocation();
		uniformEyePosition   = shaderList[0].GetEyePositionLocation();
		uniformColor         = shaderList[0].getColorLocation();
		uniformTextureOffset = shaderList[0].getOffsetLocation();
		uniformAlpha         = shaderList[0].getAlphaLocation();
		uniformNoLighting    = shaderList[0].getNoLightingLocation();
		uniformSpecularIntensity = shaderList[0].GetSpecularIntensityLocation();
		uniformShininess     = shaderList[0].GetShininessLocation();

		glUniform1f(uniformAlpha, 1.0f);
		glUniform1i(uniformNoLighting, 0);
		glUniform1f(shaderList[0].GetUniformLocation("fogDensity"), fogDensityOverride >= 0.0f ? fogDensityOverride : FOG_DENSITY);
		glUniform3f(shaderList[0].GetUniformLocation("fogColor"), FOG_COLOR.r, FOG_COLOR.g, FOG_COLOR.b);
		glUniformMatrix4fv(uniformProjection, 1, GL_FALSE, glm::value_ptr(projection));
		glUniformMatrix4fv(uniformView, 1, GL_FALSE, glm::value_ptr(camera.calculateViewMatrix()));
		const Frustum frustum = MakeFrustum(projection * camera.calculateViewMatrix());
		glUniform3f(uniformEyePosition, camPos.x, camPos.y, camPos.z);

		glm::vec3 flashPos = camPos; flashPos.y -= 0.3f;
		spotLights[0].SetFlash(flashPos, camera.getCameraDirection());
		spotLightCount = flashlightOn ? 1 : 0;

		shaderList[0].SetDirectionalLight(&mainLight);
		shaderList[0].SetPointLights(pointLights, pointLightCount);
		shaderList[0].SetSpotLights(spotLights, spotLightCount);

		toffset = glm::vec2(0.0f);
		glUniform2fv(uniformTextureOffset, 1, glm::value_ptr(toffset));
		glDisable(GL_BLEND);

		auto setColor = [&](float r, float g, float b) {
			color = glm::vec3(r, g, b);
			glUniform3fv(uniformColor, 1, glm::value_ptr(color));
		};
		auto drawMesh = [&](int meshIdx, const glm::mat4& m) {
			glUniformMatrix4fv(uniformModel, 1, GL_FALSE, glm::value_ptr(m));
			meshList[meshIdx]->RenderMesh();
		};
		// Caja con pivote en coordenadas de la caja unitaria (0.5,1,0.5 = centro de arriba)
		auto drawBox = [&](const glm::mat4& parent, glm::vec3 size, glm::vec3 pivot) {
			glm::mat4 m = glm::scale(parent, size);
			m = glm::translate(m, -pivot);
			drawMesh(MESH_CAJA, m);
		};

		// ============================================================
		//  ESCENA
		// ============================================================

		// Piso de piedra (UV: 1 repeticion cada STONE_TILE m)
		if (pisoLoaded) setColor(1.0f, 1.0f, 1.0f); else setColor(0.38f, 0.36f, 0.34f);
		pisoTexture.UseTexture();
		Material_opaco.UseMaterial(uniformSpecularIntensity, uniformShininess);
		drawMesh(MESH_PISO, glm::mat4(1.0f));

		plainTexture.UseTexture();

		// Suelo alrededor de la calle (no transitable): adoquin mas oscuro, se pierde en la niebla
		setColor(0.42f, 0.40f, 0.38f);
		pisoTexture.UseTexture();
		Material_opaco.UseMaterial(uniformSpecularIntensity, uniformShininess);
		drawMesh(MESH_SUELO, glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -0.03f, 0.0f)));

		// Modelos (sin culling: muchos .glb tienen caras en ambos sentidos)
		glDisable(GL_CULL_FACE);
		for (const Prop& pr : props) {
			if (!pr.model || !pr.model->IsLoaded()) continue;
			if (!Visible(frustum, pr.cullC, pr.cullR)) continue;
			if ((dbgSkip & 1) || ((dbgSkip & 64) && pr.id >= 16) || pr.id == dbgSkipId) continue;
			if (quality == 2 && ((pr.id >= 16 && fabsf(pr.pos.x) > 27.0f) || glm::length(glm::vec2(pr.pos.x - camPos.x, pr.pos.z - camPos.z)) > 95.0f)) continue;
			glUniformMatrix4fv(uniformModel, 1, GL_FALSE, glm::value_ptr(pr.matrix));
			pr.model->RenderModel(uniformColor);   // color del material del glb
		}
		glEnable(GL_CULL_FACE);
		plainTexture.UseTexture();

		// Carros: cuerpo (.glb) + 2 vigas longitudinales + 4 ruedas de madera, en circuito
		if (cartRig.model) {
			for (int ci = 0; ci < CART_COUNT; ci++) {
				float heading;
				glm::vec3 cp = CartPath(cartDist - ci * CART_GAP, heading);
				if (!Visible(frustum, cp + glm::vec3(0.0f, 2.5f, 0.0f), 6.0f) || (dbgSkip & 16)) continue;
				glm::mat4 base = glm::translate(glm::mat4(1.0f), cp);
				base = glm::rotate(base, glm::radians(heading), glm::vec3(0.0f, 1.0f, 0.0f));

				glDisable(GL_CULL_FACE);
				setColor(1.0f, 1.0f, 1.0f);
				glUniformMatrix4fv(uniformModel, 1, GL_FALSE, glm::value_ptr(base * cartRig.bodyMatrix));
				cartRig.model->RenderModel(uniformColor);
				glEnable(GL_CULL_FACE);

				woodTexture.UseTexture();
				if (woodLoaded) setColor(1.0f, 1.0f, 1.0f); else setColor(0.40f, 0.26f, 0.14f);
				Material_opaco.UseMaterial(uniformSpecularIntensity, uniformShininess);
				float beamLen = 2.0f * cartRig.wheelZ + 0.5f;
				for (int sx = -1; sx <= 1; sx += 2) {
					drawBox(glm::translate(base, glm::vec3(sx * cartRig.beamX, WHEEL_R, 0.0f)),
						glm::vec3(BEAM_T, BEAM_T, beamLen), glm::vec3(0.5f, 0.5f, 0.5f));
					for (int sz = -1; sz <= 1; sz += 2) {
						glm::mat4 w = glm::translate(base, glm::vec3(sx * cartRig.wheelX, WHEEL_R, sz * cartRig.wheelZ));
						w = glm::rotate(w, glm::radians(cartWheel), glm::vec3(1.0f, 0.0f, 0.0f));
						w = glm::scale(w, glm::vec3(WHEEL_R));
						drawMesh(MESH_RUEDA, w);
					}
				}
			}
			plainTexture.UseTexture();
		}

		// Musico de taiko: jerarquia cuerpo -> brazo -> antebrazo -> mano -> baqueta
		for (int ti = 0; ti < TAIKO_COUNT && taikoModel[0]; ti++) {
			if (!Visible(frustum, TAIKO_POS[ti] + glm::vec3(0.0f, 1.0f, 0.0f), 2.2f) || (dbgSkip & 8)) continue;
			glm::mat4 base = glm::translate(glm::mat4(1.0f), TAIKO_POS[ti]);
			base = glm::rotate(base, glm::radians(TAIKO_YAW), glm::vec3(0.0f, 1.0f, 0.0f));
			base = glm::scale(base, glm::vec3(TAIKO_SCALE));

			glDisable(GL_CULL_FACE);
			setColor(1.0f, 1.0f, 1.0f);
			Material_opaco.UseMaterial(uniformSpecularIntensity, uniformShininess);
			auto part = [&](int i, const glm::mat4& m) {
				glUniformMatrix4fv(uniformModel, 1, GL_FALSE, glm::value_ptr(m));
				taikoModel[i]->RenderModel(uniformColor);
			};
			const glm::vec3 Zaxis(0.0f, 0.0f, 1.0f);

			// tambor: la piel se hunde un poco al golpear
			part(1, glm::scale(base, glm::vec3(1.0f, 1.0f - TAIKO_DRUM_PRESS * taikoPath[7], 1.0f)));

			// torso: se inclina sobre la cadera y arrastra a los brazos
			glm::mat4 torso = glm::translate(base, TAIKO_HIP);
			torso = glm::rotate(torso, glm::radians(taikoPath[6]), Zaxis);
			torso = glm::translate(torso, -TAIKO_HIP);
			part(0, torso);

			for (int side = 0; side < 2; side++) {
				const TaikoArm& a = TAIKO_ARM[side];
				float sh = taikoPath[side * 3 + 0], el = taikoPath[side * 3 + 1], wr = taikoPath[side * 3 + 2];
				int b = 2 + 4 * side;
				glm::mat4 arm  = glm::rotate(glm::translate(torso, a.shoulder), glm::radians(sh), Zaxis);
				glm::mat4 fore = glm::rotate(glm::translate(arm, a.elbow - a.shoulder), glm::radians(el), Zaxis);
				glm::mat4 hand = glm::rotate(glm::translate(fore, a.wrist - a.elbow), glm::radians(wr), Zaxis);
				glm::mat4 stick = glm::translate(hand, a.grip - a.wrist);
				part(b + 0, arm);
				part(b + 1, fore);
				part(b + 2, hand);
				part(b + 3, stick);
			}
			glEnable(GL_CULL_FACE);
			plainTexture.UseTexture();
		}

		// Pileta del kingyo-sukui: tina de madera, fondo, peces y agua translucida
		{
			GLuint uSwayAmp = shaderList[0].GetUniformLocation("swayAmp");
			GLuint uSwayZone = shaderList[0].GetUniformLocation("swayZone");
			GLuint uSwayPhase = shaderList[0].GetUniformLocation("swayPhase");
			GLuint uWaveAmp = shaderList[0].GetUniformLocation("waveAmp");
			GLuint uWaveFreq = shaderList[0].GetUniformLocation("waveFreq");
			GLuint uWaveTime = shaderList[0].GetUniformLocation("waveTime");
			Material_opaco.UseMaterial(uniformSpecularIntensity, uniformShininess);

			// pared de madera
			woodTexture.UseTexture();
			if (woodLoaded) setColor(1.0f, 1.0f, 1.0f); else setColor(0.40f, 0.26f, 0.14f);
			drawMesh(MESH_TINA, glm::translate(glm::mat4(1.0f), POOL_C));
			// fondo
			glDisable(GL_CULL_FACE);
			plainTexture.UseTexture();
			setColor(0.22f, 0.38f, 0.48f);
			drawMesh(MESH_DISCO, glm::translate(glm::mat4(1.0f), POOL_C + glm::vec3(0.0f, 0.03f, 0.0f)));

			// peces (nadan, huyen de la red o saltan al tazon)
			if (fishModel) {
				glUniform2f(uSwayZone, fishTailZ0, fishTailZ1);
				for (size_t i = 0; i < fish.size(); i++) {
					const Fish& f = fish[i];
					if (f.state == 2) continue;                            // ya esta en el tazon
					float yaw = 0.0f;
					glm::vec3 pw;
					if (f.state == 0) pw = FishPos(f, &yaw);
					else {                                                  // salto en parabola hacia el tazon
						float u = glm::min(f.jt / JUMP_SECS, 1.0f);
						glm::vec3 dst = BOWL_C + glm::vec3(0.0f, BOWL_H * 0.6f, 0.0f);
						pw = glm::mix(f.jfrom, dst, u) + glm::vec3(0.0f, 4.0f * JUMP_H * u * (1.0f - u), 0.0f);
						yaw = atan2f(dst.x - f.jfrom.x, dst.z - f.jfrom.z);
					}
					glm::mat4 m = glm::translate(glm::mat4(1.0f), pw);
					m = glm::rotate(m, yaw, glm::vec3(0.0f, 1.0f, 0.0f));
					m = glm::scale(m, glm::vec3(fishScale * f.size));
					m = glm::translate(m, -fishCenter);
					setColor(f.tint.r, f.tint.g, f.tint.b);
					glUniform1f(uSwayAmp, fishTailAmp * (f.state == 1 ? 1.6f : 1.0f));
					glUniform1f(uSwayPhase, 2.0f * PI_F * FISH_TAIL_HZ * t + 1.7f * i);
					glUniformMatrix4fv(uniformModel, 1, GL_FALSE, glm::value_ptr(m));
					fishModel->RenderModel();
				}
				// peces atrapados: dan vueltas dentro del tazon
				for (int i = 0; i < glm::min(bowlCount, 10); i++) {
					float ang = 0.8f * t + i * 0.63f;
					glm::mat4 m = glm::translate(glm::mat4(1.0f), BOWL_C + glm::vec3(0.07f * cosf(ang), 0.07f, 0.07f * sinf(ang)));
					m = glm::rotate(m, -ang, glm::vec3(0.0f, 1.0f, 0.0f));
					m = glm::scale(m, glm::vec3(fishScale * 0.55f));
					m = glm::translate(m, -fishCenter);
					setColor(1.0f, 0.8f, 0.6f);
					glUniform1f(uSwayAmp, fishTailAmp * 0.5f);
					glUniform1f(uSwayPhase, 2.0f * PI_F * FISH_TAIL_HZ * t + i);
					glUniformMatrix4fv(uniformModel, 1, GL_FALSE, glm::value_ptr(m));
					fishModel->RenderModel();
				}
				glUniform1f(uSwayAmp, 0.0f);
			}

			// tazon (donde caen los peces atrapados)
			plainTexture.UseTexture();
			setColor(0.78f, 0.12f, 0.10f);
			drawMesh(MESH_TAZON, glm::translate(glm::mat4(1.0f), BOWL_C));
			setColor(0.60f, 0.09f, 0.08f);
			drawMesh(MESH_DISCO, glm::scale(glm::translate(glm::mat4(1.0f), BOWL_C + glm::vec3(0.0f, 0.015f, 0.0f)), glm::vec3(BOWL_R / POOL_R, 1.0f, BOWL_R / POOL_R)));

			// red (poi): aro y mango de madera clara
			bool netShown = (gameState == GS_PLAY || gameState == GS_RESULT);
			if (netShown) {
				setColor(0.88f, 0.76f, 0.52f);
				drawMesh(MESH_ARO, glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(netPos.x, netY, netPos.z)), glm::vec3(NET_R)));
				glm::mat4 hm = glm::translate(glm::mat4(1.0f), glm::vec3(netPos.x, netY, netPos.z + NET_R));
				hm = glm::rotate(hm, glm::radians(-28.0f), glm::vec3(1.0f, 0.0f, 0.0f));
				drawBox(hm, glm::vec3(0.022f, 0.022f, 0.55f), glm::vec3(0.5f, 0.5f, 0.0f));
			}

			// agua: translucida y con ondas
			plainTexture.UseTexture();   // el ultimo pez dejo enlazada su textura: el agua va lisa
			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			glDepthMask(GL_FALSE);
			glUniform1f(uniformAlpha, 0.45f);
			glUniform1f(uWaveAmp, 0.004f);
			glUniform1f(uWaveFreq, 16.0f);
			glUniform1f(uWaveTime, 2.0f * t);
			setColor(0.35f, 0.65f, 0.90f);
			drawMesh(MESH_DISCO, glm::translate(glm::mat4(1.0f), POOL_C + glm::vec3(0.0f, POOL_WATER_Y, 0.0f)));
			glUniform1f(uWaveAmp, 0.0f);
			if (netShown) {                                      // papel de la red: blanco translucido
				glUniform1f(uniformAlpha, 0.5f);
				setColor(1.0f, 1.0f, 1.0f);
				drawMesh(MESH_DISCO, glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(netPos.x, netY, netPos.z)), glm::vec3(NET_R / POOL_R, 1.0f, NET_R / POOL_R)));
			}
			glUniform1f(uniformAlpha, 1.0f);
			glDepthMask(GL_TRUE);
			glDisable(GL_BLEND);
			glEnable(GL_CULL_FACE);
		}

		// Duenos del puesto. Los dos de la pileta son jerarquicos (base -> torso -> cabeza / abanico): reverencia de bienvenida cada 9 s,
		// respiracion, cabeza que mira a un lado y a otro y el abanico moviendose. El resto de vendedores usa la figura entera con balanceo.
		for (size_t oi = 0; oi < owners.size(); oi++) {
			const Owner& o = owners[oi];
			if (!o.model) continue;
			if (!Visible(frustum, glm::vec3(o.x, 0.9f, o.z), 1.8f)) continue;
			if (dbgSkip & 2) continue;
			const NpcRig* rg = (oi < 2 && npcRigs[oi].base) ? &npcRigs[oi] : nullptr;
			float sway = (rg ? 0.0f : 5.0f) * sinf(0.7f * t + o.phase);          // grados
			float bob  = 0.008f * sinf(1.4f * t + o.phase);
			float br   = 1.0f + 0.006f * sinf(1.4f * t + o.phase);  // respiracion
			glm::mat4 m = glm::translate(glm::mat4(1.0f), glm::vec3(o.x, bob, o.z));
			m = glm::rotate(m, glm::radians(o.yaw + sway), glm::vec3(0.0f, 1.0f, 0.0f));
			m = glm::scale(m, glm::vec3(o.scale * br));
			glDisable(GL_CULL_FACE);
			setColor(1.0f, 1.0f, 1.0f);
			Material_opaco.UseMaterial(uniformSpecularIntensity, uniformShininess);
			if (!rg) {
				m = glm::translate(m, glm::vec3(-o.center.x, -o.minY, -o.center.z));
				glUniformMatrix4fv(uniformModel, 1, GL_FALSE, glm::value_ptr(m));
				o.model->RenderModel(uniformColor);
			} else {
				const glm::vec3 X(1.0f, 0.0f, 0.0f), Y(0.0f, 1.0f, 0.0f), Zx(0.0f, 0.0f, 1.0f);
				// reverencia: sube y baja suave durante 1.8 s de cada 9 s (sin^2)
				float u = fmodf(t / 9.0f + o.phase * 0.11f, 1.0f), bw = (u < 0.2f) ? sinf(u / 0.2f * PI_F) : 0.0f;
				bw *= bw;
				float breathe = 1.2f * sinf(1.4f * t + o.phase);
				glm::mat4 mt = glm::translate(m, rg->pTorso);
				mt = glm::rotate(mt, glm::radians(rg->bowDeg * bw + breathe), X);                    // inclinacion hacia delante
				mt = glm::rotate(mt, glm::radians(1.6f * sinf(0.7f * t + o.phase)), Zx);             // balanceo lateral del torso
				glm::mat4 mh = glm::translate(mt, rg->pHead - rg->pTorso);
				mh = glm::rotate(mh, glm::radians(rg->headYawDeg * sinf(0.5f * t + o.phase * 1.7f)), Y);          // mira a un lado y al otro
				mh = glm::rotate(mh, glm::radians(-0.6f * rg->bowDeg * bw + 3.0f * sinf(0.9f * t + o.phase)), X);   // asiente y compensa la reverencia
				glUniformMatrix4fv(uniformModel, 1, GL_FALSE, glm::value_ptr(m));
				rg->base->RenderModel(uniformColor);
				glUniformMatrix4fv(uniformModel, 1, GL_FALSE, glm::value_ptr(mt));
				rg->torso->RenderModel(uniformColor);
				glUniformMatrix4fv(uniformModel, 1, GL_FALSE, glm::value_ptr(mh));
				rg->head->RenderModel(uniformColor);
				if (rg->fan) {                                                                       // abanico: sube y baja con las manos y gira la muneca
					float w = 2.0f * PI_F * 1.4f * t + o.phase;
					glm::mat4 mf = glm::translate(mt, rg->pFan - rg->pTorso);
					mf = glm::rotate(mf, glm::radians(rg->fanDeg * (14.0f * sinf(w)) * (1.0f - 0.7f * bw)), X);
					mf = glm::rotate(mf, glm::radians(rg->fanDeg * (11.0f * sinf(w + 1.3f))), Zx);
					glUniformMatrix4fv(uniformModel, 1, GL_FALSE, glm::value_ptr(mf));
					rg->fan->RenderModel(uniformColor);
				}
			}
			glEnable(GL_CULL_FACE);
		}
		plainTexture.UseTexture();

		// Gente caminando: se dibuja el cuadro de la caminata que toca segun el tiempo
		{
			glDisable(GL_CULL_FACE);
			setColor(1.0f, 1.0f, 1.0f);
			Material_opaco.UseMaterial(uniformSpecularIntensity, uniformShininess);
			for (const Walker& w : walkers) {
				if (dbgSkip & 4) break;
				const WalkerType& wt = walkerTypes[w.type];
				if (!wt.model) continue;
				glm::vec2 tan;
				glm::vec3 p = LoopPos(w.s0 + w.speed * t, WALK_LANE, WALK_Z_FAR, WALK_Z_NEAR, tan);
				p += glm::vec3(tan.y, 0.0f, -tan.x) * w.lateral;                      // se corre un poco hacia el centro
				float yaw = atan2f(tan.x, tan.y);                                     // el modelo mira a +Z
				float cyc = fmodf(t * (w.speed / wt.strideSpeed) / wt.animSecs + w.phase, 1.0f);
				int frame = (int)(cyc * wt.frames) % wt.frames;
				glm::mat4 m = glm::translate(glm::mat4(1.0f), p);
				m = glm::rotate(m, yaw, glm::vec3(0.0f, 1.0f, 0.0f));
				m = glm::scale(m, glm::vec3(wt.scale));
				m = glm::translate(m, glm::vec3(-wt.center.x, -wt.minY, -wt.center.z));
				glUniformMatrix4fv(uniformModel, 1, GL_FALSE, glm::value_ptr(m));
				wt.model->RenderMeshRange(frame * wt.P, wt.P, uniformColor);
			}
			glEnable(GL_CULL_FACE);
			plainTexture.UseTexture();
		}

		// Muestrario de texturas TEMPORALES contra el borde izquierdo (T1..T5), visible con M
		if (showMarkers) {
			Material_opaco.UseMaterial(uniformSpecularIntensity, uniformShininess);
			for (int i = 0; i < 5; i++) {
				setColor(1.0f, 1.0f, 1.0f);
				swatchTex[i].UseTexture();
				glm::mat4 m = glm::translate(glm::mat4(1.0f), glm::vec3(-STREET_HALF_W + 0.5f, 0.4f, 12.0f - i * 1.5f));
				m = glm::rotate(m, glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
				m = glm::scale(m, glm::vec3(1.2f));
				m = glm::translate(m, glm::vec3(-0.5f, 0.0f, 0.0f));
				drawMesh(MESH_QUAD, m);
			}
			plainTexture.UseTexture();
		}

		// Cuerdas de los faroles
		setColor(0.08f, 0.06f, 0.05f);
		{
			const float step = (STREET_Z_MAX - STREET_Z_MIN - 2.0f) / (ROPE_COUNT - 1);
			for (int r = 0; r < ROPE_COUNT; r++) {
				glm::mat4 m = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, ROPE_HEIGHT, STREET_Z_MIN + 1.0f + r * step));
				drawBox(m, glm::vec3(2.0f * STREET_HALF_W, 0.02f, 0.02f), glm::vec3(0.5f, 0.5f, 0.5f));
			}
		}

		// Faroles: cuerda (pendulo) + tapas oscuras (iluminadas) y cuerpo emisivo (noLighting)
		for (const Lantern& l : lanterns) {
			if (dbgSkip & 32) break;
			glm::mat4 pivot = LanternPivotMatrix(l, t);
			float flick = LanternFlicker(l, t), sc = l.scale;
			const float cordL = LANTERN_CORD_L * sc, bodyH = LANTERN_BODY_H * sc, bodyR = LANTERN_BODY_R * sc;

			setColor(0.05f, 0.04f, 0.03f);
			drawBox(pivot, glm::vec3(0.015f * sc, cordL, 0.015f * sc), glm::vec3(0.5f, 1.0f, 0.5f));
			glm::mat4 body = glm::translate(pivot, glm::vec3(0.0f, -cordL - bodyH, 0.0f));
			drawBox(body, glm::vec3(bodyR * 1.6f, 0.04f * sc, bodyR * 1.6f), glm::vec3(0.5f, 0.0f, 0.5f));   // tapa inferior
			drawBox(glm::translate(body, glm::vec3(0.0f, bodyH, 0.0f)),
				glm::vec3(bodyR * 1.6f, 0.04f * sc, bodyR * 1.6f), glm::vec3(0.5f, 1.0f, 0.5f));           // tapa superior

			glUniform1i(uniformNoLighting, 1);
			setColor(1.0f * flick, 0.50f * flick, 0.18f * flick);
			drawMesh(MESH_CILINDRO, glm::scale(body, glm::vec3(bodyR, bodyH, bodyR)));
			glUniform1i(uniformNoLighting, 0);
		}

		// Carta de informacion flotante: cartel de frente a la camara, adelantado hacia ella para no quedar dentro del objeto
		if (infoOpen >= 0 && cardPageOK[infoPageBase[infoOpen] + infoPage]) {
			const InfoPoint& ip = infoPoints[infoOpen];
			Texture& ct = cardPages[infoPageBase[infoOpen] + infoPage];
			float aspect = (ct.GetWidth() > 0) ? (float)ct.GetHeight() / ct.GetWidth() : 0.56f;
			float cw = ip.cardW, ch = cw * aspect;
			glm::vec2 toCam(camPos.x - infoOpenPos.x, camPos.z - infoOpenPos.z);
			float dl = glm::length(toCam); if (dl > 0.001f) toCam /= dl;
			float yaw = atan2f(toCam.x, toCam.y);
			glm::vec3 cpos = infoOpenPos + glm::vec3(toCam.x * ip.cardFwd, ip.cardY + 0.04f * sinf(1.6f * t), toCam.y * ip.cardFwd);
			glm::mat4 m = glm::translate(glm::mat4(1.0f), cpos);
			m = glm::rotate(m, yaw, glm::vec3(0.0f, 1.0f, 0.0f));
			m = glm::scale(m, glm::vec3(cw, ch, 1.0f));
			m = glm::translate(m, glm::vec3(-0.5f, -0.5f, 0.0f));
			glUniform1i(uniformNoLighting, 1);
			glDisable(GL_CULL_FACE);
			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			glUniform1f(uniformAlpha, glm::min(1.0f, infoT / 0.2f));   // aparece con un fundido de 0.2 s
			ct.UseTexture();
			setColor(1.0f, 1.0f, 1.0f);
			drawMesh(MESH_QUAD, m);
			glUniform1f(uniformAlpha, 1.0f);
			glDisable(GL_BLEND);
			glEnable(GL_CULL_FACE);
			glUniform1i(uniformNoLighting, 0);
			plainTexture.UseTexture();
		}

		// Fuegos artificiales: sprites aditivos de cara a la camara, sin niebla ni escritura de profundidad
		if (fwSpriteOK && !sparks.empty()) {
			glm::mat4 vw = camera.calculateViewMatrix();
			glm::vec3 right(vw[0][0], vw[1][0], vw[2][0]), up(vw[0][1], vw[1][1], vw[2][1]), fwd(vw[0][2], vw[1][2], vw[2][2]);
			glUniform1f(shaderList[0].GetUniformLocation("fogDensity"), 0.0f);
			glUniform1i(uniformNoLighting, 1);
			glDisable(GL_CULL_FACE);
			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE);
			glDepthMask(GL_FALSE);
			fwSprite.UseTexture();
			for (const Spark& sp : sparks) {
				float u = sp.age / sp.life;
				float fade = powf(1.0f - u, 1.4f) * (0.75f + 0.25f * sinf(sp.age * 38.0f + sp.p.x));       // se apaga y titila
				glm::vec3 c = glm::mix(sp.col, glm::vec3(1.0f), glm::max(0.0f, 0.5f - u * 3.0f));         // al inicio, blanco incandescente
				c = glm::mix(c, glm::vec3(0.9f, 0.35f, 0.12f), glm::smoothstep(0.55f, 1.0f, u) * 0.7f);   // al final, brasa
				setColor(c.r, c.g, c.b);
				glUniform1f(uniformAlpha, fade);
				float sz = sp.size * (1.0f + 1.2f * (1.0f - u) * (sp.age < 0.12f ? 1.0f : 0.0f)) * 1.4f;
				glm::mat4 m(1.0f);
				m[0] = glm::vec4(right * sz, 0.0f);
				m[1] = glm::vec4(up * sz, 0.0f);
				m[2] = glm::vec4(fwd, 0.0f);
				m[3] = glm::vec4(sp.p - (right + up) * (0.5f * sz), 1.0f);
				drawMesh(MESH_QUAD, m);
			}
			glUniform1f(uniformAlpha, 1.0f);
			glDepthMask(GL_TRUE);
			glDisable(GL_BLEND);
			glEnable(GL_CULL_FACE);
			glUniform1i(uniformNoLighting, 0);
			glUniform1f(shaderList[0].GetUniformLocation("fogDensity"), fogDensityOverride >= 0.0f ? fogDensityOverride : FOG_DENSITY);
			plainTexture.UseTexture();
		}

		// Marcadores numerados (display de 7 segmentos, siempre de frente a la camara, sin iluminacion)
		if (showMarkers) {
			glUniform1i(uniformNoLighting, 1);
			static const int SEG[10] = { 63, 6, 91, 79, 102, 109, 125, 7, 127, 111 }; // bits: a b c d e f g
			const float DW = 0.34f, DH = 0.60f, DT = 0.07f, DZ = 0.05f, GAP = 0.12f;

			// Dibuja un numero (con prefijo 'T' opcional) centrado en 'at', escalado por 'k'
			auto drawLabel = [&](glm::vec3 at, int number, bool tPrefix, glm::vec3 col, float k) {
				float yawToCam = atan2f(camPos.x - at.x, camPos.z - at.z);
				glm::mat4 base = glm::translate(glm::mat4(1.0f), at);
				base = glm::rotate(base, yawToCam, glm::vec3(0.0f, 1.0f, 0.0f));
				base = glm::scale(base, glm::vec3(k));

				char txt[8]; int nd = snprintf(txt, sizeof(txt), "%d", number);
				int cells = nd + (tPrefix ? 1 : 0);
				float total = cells * DW + (cells - 1) * GAP;

				// placa oscura detras para contraste
				setColor(0.0f, 0.0f, 0.0f);
				drawBox(glm::translate(base, glm::vec3(0.0f, DH * 0.5f, -0.04f)),
					glm::vec3(total + 0.24f, DH + 0.24f, 0.03f), glm::vec3(0.5f, 0.5f, 0.5f));

				setColor(col.r, col.g, col.b);
				float x0 = -total * 0.5f;
				auto seg = [&](float cx, float cy, float w, float h) {
					drawBox(glm::translate(base, glm::vec3(cx, cy, 0.0f)), glm::vec3(w, h, DZ), glm::vec3(0.5f, 0.5f, 0.5f));
				};
				int cell = 0;
				if (tPrefix) { // letra T
					float cx = x0 + DW * 0.5f;
					seg(cx, DH - DT * 0.5f, DW, DT);
					seg(cx, DH * 0.5f, DT, DH);
					cell++;
				}
				for (int d = 0; d < nd; d++, cell++) {
					int mask = SEG[txt[d] - '0'];
					float left = x0 + cell * (DW + GAP);
					if (mask & 1)  seg(left + DW * 0.5f,      DH - DT * 0.5f, DW, DT);          // a
					if (mask & 2)  seg(left + DW - DT * 0.5f, DH * 0.75f,     DT, DH * 0.5f);   // b
					if (mask & 4)  seg(left + DW - DT * 0.5f, DH * 0.25f,     DT, DH * 0.5f);   // c
					if (mask & 8)  seg(left + DW * 0.5f,      DT * 0.5f,      DW, DT);          // d
					if (mask & 16) seg(left + DT * 0.5f,      DH * 0.25f,     DT, DH * 0.5f);   // e
					if (mask & 32) seg(left + DT * 0.5f,      DH * 0.75f,     DT, DH * 0.5f);   // f
					if (mask & 64) seg(left + DW * 0.5f,      DH * 0.5f,      DW, DT);          // g
				}
			};

			for (const Prop& pr : props) {
				if (!pr.model || !pr.model->IsLoaded()) continue;
				const glm::vec3& col = MarkerColor(pr.id);
				glm::vec3 top(pr.pos.x, pr.topY + 1.0f, pr.pos.z);
				float k = glm::clamp(glm::length(top - camPos) / 8.0f, 1.0f, 5.0f); // crece con la distancia
				setColor(col.r, col.g, col.b);   // poste que une el modelo con su numero
				drawBox(glm::translate(glm::mat4(1.0f), glm::vec3(pr.pos.x, pr.topY, pr.pos.z)),
					glm::vec3(0.06f * k, 1.0f, 0.06f * k), glm::vec3(0.5f, 0.0f, 0.5f));
				drawLabel(top, pr.id, false, col, k);
			}
			for (int i = 0; i < 5; i++) // T1..T5 sobre cada muestra de textura
				drawLabel(glm::vec3(-STREET_HALF_W + 0.5f, 1.85f, 12.0f - i * 1.5f), i + 1, true, glm::vec3(1.0f), 1.0f);

			glUniform1i(uniformNoLighting, 0);
		}

		// HUD 2D con las imagenes de Textures/UI: aviso E, tiempo, puntos, instrucciones y resultado
		{
			bool nearPool = glm::length(glm::vec2(camPos.x - POOL_C.x, camPos.z - POOL_C.z)) < GAME_NEAR_R;
			bool showE = uiOK && gameState == GS_IDLE && infoOpen < 0 && (nearPool || infoNear >= 0);
			bool showGame = uiOK && (gameState == GS_PLAY || gameState == GS_RESULT);
			bool showNav = uiNavOK && gameState == GS_IDLE && infoOpen >= 0;
			bool showCtrl = uiCtrlOK && controlsOn && gameState == GS_IDLE;
			if (showE || showGame || showNav || showCtrl) {
				float W = mainWindow.getBufferWidth(), H = mainWindow.getBufferHeight();
				glm::mat4 ortho = glm::ortho(0.0f, W, 0.0f, H, -1.0f, 1.0f);
				glm::mat4 ident(1.0f);
				glUniformMatrix4fv(uniformProjection, 1, GL_FALSE, glm::value_ptr(ortho));
				glUniformMatrix4fv(uniformView, 1, GL_FALSE, glm::value_ptr(ident));
				glUniform1f(shaderList[0].GetUniformLocation("fogDensity"), 0.0f);
				glUniform1i(uniformNoLighting, 1);
				glDisable(GL_DEPTH_TEST);
				glEnable(GL_BLEND);
				glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

				const float k = 0.78f * H / 720.0f, mg = 20.0f * k;      // escala de la interfaz (se adapta al tamano de la ventana)
				auto img = [&](Texture& tx, float x, float y, float w, float h, float a) {
					tx.UseTexture();
					setColor(1.0f, 1.0f, 1.0f);
					glUniform1f(uniformAlpha, a);
					drawMesh(MESH_QUAD, glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(x, y, 0.0f)), glm::vec3(w, h, 1.0f)));
				};
				// numero centrado en (cx, cy) con los glifos de hud_digitos.png (h = alto del digito)
				auto number = [&](int n, float cx, float cy, float h, glm::vec3 tint) {
					uiDigitos.UseTexture();
					setColor(tint.r, tint.g, tint.b);
					glUniform1f(uniformAlpha, 1.0f);
					char txt[12]; int nd = snprintf(txt, sizeof(txt), "%d", n);
					float total = 0.0f;
					for (int i = 0; i < nd; i++) total += h * digitAspect[txt[i] - '0'] * 0.94f;
					float x = cx - total * 0.5f;
					for (int i = 0; i < nd; i++) {
						int d = txt[i] - '0';
						float w = h * digitAspect[d];
						drawMesh(MESH_DIG[d], glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(x, cy - h * 0.5f, 0.0f)), glm::vec3(w, h, 1.0f)));
						x += w * 0.94f;
					}
				};

				if (showCtrl) img(uiCtrl, mg, mg, 460.0f * k * 0.74f, 364.0f * k * 0.74f, 0.92f);               // abajo a la izquierda
				if (showNav) img(uiNav, (W - 640.0f * k) * 0.5f, mg, 640.0f * k, 120.0f * k, 1.0f);          // abajo al centro (con la carta abierta)
				if (showE) img(uiAviso, (W - 420.0f * k) * 0.5f, mg, 420.0f * k, 140.0f * k, 1.0f);       // abajo al centro
				if (showGame) {
					float pw = 360.0f * k, ph = 140.0f * k;
					img(uiTiempo, mg, H - mg - ph, pw, ph, 1.0f);                                       // arriba a la izquierda
					img(uiPuntos, W - mg - pw, H - mg - ph, pw, ph, 1.0f);                              // arriba a la derecha
					int secs = (int)ceilf(gameTimeLeft);
					glm::vec3 tcol = (gameTimeLeft < 10.0f) ? glm::vec3(1.0f, 0.35f, 0.30f) : glm::vec3(1.0f);   // en rojo los ultimos 10 s
					// los numeros van en la mitad derecha de cada marco (la izquierda es el icono)
					number(secs,      mg + 0.75f * pw,            H - mg - 0.5f * ph, ph * (secs >= 100 ? 0.44f : 0.55f), tcol);
					number(gameScore, W - mg - pw + 0.75f * pw,   H - mg - 0.5f * ph, ph * (gameScore >= 100 ? 0.44f : (gameScore >= 10 ? 0.52f : 0.55f)), glm::vec3(1.0f));
					if (gameState == GS_PLAY && gameT < 8.0f) {                                         // instrucciones al empezar, con fundido
						float iw = 800.0f * k * 0.8f, ih = 220.0f * k * 0.8f;
						img(uiInstr, (W - iw) * 0.5f, H - mg - ih, iw, ih, glm::min(1.0f, (8.0f - gameT) / 1.5f));
					}
					if (gameState == GS_RESULT) {                                                       // resultado: centrado y pegado abajo
						float rk = k * 1.15f, rw = 700.0f * rk, rh = 400.0f * rk;
						img(uiResult, (W - rw) * 0.5f, 0.0f, rw, rh, 1.0f);
						number(gameScore, W * 0.5f, rh * 0.40f, rh * (gameScore >= 100 ? 0.26f : 0.32f), glm::vec3(1.0f, 0.82f, 0.30f));
					}
				}
				glUniform1f(uniformAlpha, 1.0f);
				glDisable(GL_BLEND);
				glEnable(GL_DEPTH_TEST);
				glUniform1i(uniformNoLighting, 0);
			}
		}

		if (shotsDir) { // modo revision: 1 captura por vista, luego cierra
			const CamPreset* views = beauty ? BEAUTY_VIEWS : AUTO_VIEWS;
			const int N = beauty ? (int)(sizeof(BEAUTY_VIEWS) / sizeof(BEAUTY_VIEWS[0])) : (int)(sizeof(AUTO_VIEWS) / sizeof(AUTO_VIEWS[0]));
			int view = shotFrame / 30, phase = shotFrame % 30;
			if (phase == 0 && view < N) {
				SetCameraPreset(views[view]); freeCamera = true;
				bool aerial = beauty && (view == 8 || view == 9);
				fogDensityOverride = aerial ? 0.004f : -1.0f;   // aereas: menos niebla
				moonAmbientOverride = aerial ? 0.60f : -1.0f;   // y mas luz ambiental
			}
			if (phase == 20 && view < N) {
				char path[512];
				snprintf(path, sizeof(path), "%s/view%d.tga", shotsDir, view);
				SaveScreenshotTGA(path);
			}
			shotFrame++;
			if (view >= N) mainWindow.setShouldClose(true);
		}

		if (fireShotDir) {
			if (fsFrame >= 40 && fsFrame % 25 == 0 && fsFrame <= 190) {
				char path[512]; snprintf(path, sizeof(path), "%s/fire%d.tga", fireShotDir, fsFrame);
				SaveScreenshotTGA(path);
			}
			static float fsT0 = 0.0f;
			if (fsFrame == 40) fsT0 = (float)glfwGetTime();
			if (fsFrame == 190) printf("FPS_PRUEBA %.1f\n", 150.0f / ((float)glfwGetTime() - fsT0));
			if (fsFrame > 190) mainWindow.setShouldClose(true);
		}
		if (cardShotDir) {
			if (csFrame == 60) printf("CARTA: infoOpen=%d infoNear=%d infoT=%.2f cardOK=%d pos=(%.1f,%.1f,%.1f)\n", infoOpen, infoNear, infoT, infoOpen >= 0 ? (int)cardPageOK[infoPageBase[infoOpen] + infoPage] : -1, infoOpenPos.x, infoOpenPos.y, infoOpenPos.z);
			if (csFrame == 20 || csFrame == 60) {
				char path[512];
				snprintf(path, sizeof(path), "%s/card%d.tga", cardShotDir, csFrame == 20 ? 0 : 1);
				SaveScreenshotTGA(path);
			}
			if (csFrame > 70) mainWindow.setShouldClose(true);
		}
		if (gameShotDir && gameState == GS_RESULT && gameT > 0.8f) {
			static bool shotResult = false;
			if (!shotResult) { shotResult = true; char path[512]; snprintf(path, sizeof(path), "%s/gameresult.tga", gameShotDir); SaveScreenshotTGA(path); }
		}
		if (gameShotDir) {
			static const int SHOTS[] = { 20, 110, 190, 260, 330, 400 };
			for (int i = 0; i < 6; i++)
				if (gsFrame == SHOTS[i]) {
					char path[512];
					snprintf(path, sizeof(path), "%s/game%d.tga", gameShotDir, i);
					SaveScreenshotTGA(path);
				}
			if (gsFrame > 430) {
				printf("FPS promedio de la prueba: %.1f\n", 390.0f / ((float)glfwGetTime() - gsT0));
				mainWindow.setShouldClose(true);
			}
		}

		glFlush();
		mainWindow.swapBuffers();
	}

	if (taikoHitLoaded) ma_sound_uninit(&taikoHit);
	if (bgmLoaded) ma_sound_uninit(&bgm);
	if (taikoTrackLoaded) ma_sound_uninit(&taikoTrack);
	if (audioReady) ma_engine_uninit(&audioEngine);

	return 0;
}
