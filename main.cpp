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
#include <algorithm>

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
int MESH_QUAD     = -1; // quad vertical unitario [0,1] (flyers/billboards)

// ============================================================
//  UTILIDADES
// ============================================================

// Deteccion de flanco de subida: true una sola vez por pulsacion
bool KeyPressedOnce(int key)
{
	static bool prev[1024] = { false };
	bool* keys = mainWindow.getsKeys();
	bool pressed = keys[key] && !prev[key];
	prev[key] = keys[key];
	return pressed;
}

// Promedio de normales para sombreado suave (Phong).
// vLength = floats por vertice (8: pos3 + uv2 + normal3), normalOffset = 5.
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
glm::vec2 SpriteSheetOffset(int index, int cols, int rows)
{
	return glm::vec2((index % cols) / (float)cols, -(index / cols) / (float)rows);
}

// ============================================================
//  GEOMETRIA PROCEDURAL  (vertice = x y z  u v  nx ny nz)
// ============================================================

// Cilindro: solo cuerpo lateral, radio 1, altura 1 (escalar con glm::scale)
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

void CreateObjects()
{
	MESH_CAJA     = MakeBox();
	MESH_CILINDRO = MakeCylinder(18);

	// Piso de la calle: 10 m (X) x 60 m (Z); UV = metros / STONE_TILE (1 repeticion cada 2 m)
	{
		const float W = 5.0f, Z0 = -30.0f, Z1 = 30.0f, STONE_TILE = 2.0f;
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
const float STREET_HALF_W = 5.0f;     // ancho 10 m
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
bool freeCamera = false; // tecla V: camara libre (sin limites de calle ni altura fija)

void SetCameraPreset(const CamPreset& p)
{
	camera = Camera(p.pos, glm::vec3(0.0f, 1.0f, 0.0f), p.yaw, p.pitch, CAM_MOVE_SPEED, CAM_TURN_SPEED);
}

// Mantiene al usuario dentro de la calle y a la altura de los ojos
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
};
std::vector<Lantern> lanterns;

const int   ROPE_COUNT        = 12;
const int   LANTERNS_PER_ROPE = 5;
const float ROPE_HEIGHT       = 4.5f;
const float LANTERN_CORD_L    = 0.30f;  // L: cuerda hasta el farol
const float LANTERN_BODY_H    = 0.45f;
const float LANTERN_BODY_R    = 0.22f;
const float LANTERN_SWING_DEG = 4.0f;
const float LANTERN_SWING_W   = 1.6f;
const glm::vec3 LANTERN_COLOR(1.0f, 0.62f, 0.30f);

// Ruido de valor 1D suave en [-1, 1]
static float Hash1D(int n)
{
	unsigned int x = (unsigned int)n;
	x = (x << 13) ^ x;
	x = x * (x * x * 15731u + 789221u) + 1376312589u;
	return 1.0f - (float)(x & 0x7fffffffu) / 1073741824.0f;
}
float Noise1D(float x)
{
	int i = (int)floorf(x);
	float f = x - (float)i;
	f = f * f * (3.0f - 2.0f * f);
	return Hash1D(i) * (1.0f - f) + Hash1D(i + 1) * f;
}

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

float LanternSwing(const Lantern& l, float t) // grados
{
	return LANTERN_SWING_DEG * sinf(LANTERN_SWING_W * t + l.phase);
}
float LanternFlicker(const Lantern& l, float t)
{
	return 0.92f + 0.08f * Noise1D(t * 6.0f + l.phase * 10.0f);
}
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
const char* M_APART  = "Models/japanese_apartment.glb";
const char* M_TORII1 = "Models/japanese_torii_gate_game_asset.glb";
const char* M_TORII2 = "Models/japanese_torii_gate.glb";
const char* M_LANTRN = "Models/stone_japanese_lantern.glb";

std::vector<Prop> props = {
	// --- entrada: torii y faroles de piedra a los lados ---
	{  8, M_TORII1,  0,  27.0f, 5.0f,   0.0f },
	{ 10, M_LANTRN,  0,  24.5f, 1.8f,  90.0f, -4.0f, 1e9f, 64.0f },
	{ 10, M_LANTRN,  0,  24.5f, 1.8f,  90.0f,  4.0f, 1e9f, 64.0f },

	// --- lado izquierdo ---
	{  1, M_PSX,    -1,  20.5f, 6.25f,  0.0f },
	{  2, M_STORE,  -1,  11.5f, 5.0f,  90.0f },
	{  2, M_STORE,  -1,   7.9f, 5.0f,  90.0f },
	{  2, M_STORE,  -1,   4.3f, 5.0f,  90.0f },
	{  3, M_HOUSE,  -1,  -0.5f, 6.5f,  90.0f },
	{  6, M_ALTAR,  -1,  -9.0f, 2.1f,  90.0f },
	{  2, M_STORE,  -1, -14.0f, 5.0f,  90.0f },
	{  2, M_STORE,  -1, -17.6f, 5.0f,  90.0f },
	{  2, M_STORE,  -1, -21.2f, 5.0f,  90.0f },
	{  3, M_HOUSE,  -1, -26.5f, 6.5f,  90.0f },

	// --- lado derecho ---
	{  2, M_STORE,  +1,  24.0f, 5.0f, -90.0f },
	{  2, M_STORE,  +1,  20.4f, 5.0f, -90.0f },
	{  2, M_STORE,  +1,  16.8f, 5.0f, -90.0f },
	{  4, M_FLOWER, +1,   9.5f, 4.4f, -45.0f, 0.0f, -90.0f, 0.09f },
	{  3, M_HOUSE,  +1,   0.0f, 6.5f, -90.0f },
	{  5, M_RAMEN,  +1,  -8.0f, 3.75f,-135.0f, 0.0f, -90.0f },
	{  2, M_STORE,  +1, -14.0f, 5.0f, -90.0f },
	{  2, M_STORE,  +1, -17.6f, 5.0f, -90.0f },
	{  2, M_STORE,  +1, -21.2f, 5.0f, -90.0f },
	{  2, M_STORE,  +1, -24.8f, 5.0f, -90.0f },

	// --- centro / fondo ---
	{  9, M_TORII2,  0, -24.0f, 5.0f,  90.0f },
	{  7, M_APART,   0, -42.0f, 9.0f,   0.0f, 0.0f, 1e9f, -1e30f, -0.15f },
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
//  MAIN
// ============================================================
// Guarda el framebuffer actual como .tga (24 bits). Sirve para revisar la escena sin depender de capturas de pantalla.
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
};

int main(int argc, char** argv)
{
	const char* shotsDir = nullptr;
	for (int i = 1; i + 1 < argc; i++)
		if (strcmp(argv[i], "--shots") == 0) shotsDir = argv[i + 1];
	int shotFrame = 0;
	mainWindow = Window(1366, 768);
	if (mainWindow.Initialise() != 0)
		return 1;

	// ─── Audio (opcional): si no existe el archivo, simplemente no suena ───
	ma_sound bgm;
	bool bgmLoaded = false;
	bool audioReady = (ma_engine_init(NULL, &audioEngine) == MA_SUCCESS);
	if (audioReady)
	{
		if (ma_sound_init_from_file(&audioEngine, "Audio/ohayashi_loop.mp3",
			MA_SOUND_FLAG_STREAM, NULL, NULL, &bgm) == MA_SUCCESS)
		{
			ma_sound_set_looping(&bgm, MA_TRUE);
			ma_sound_set_volume(&bgm, 0.3f);
			ma_sound_start(&bgm);
			bgmLoaded = true;
		}
	}

	CreateObjects();
	CreateShaders();
	InitLanterns();
	LoadProps();

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
	// Orden GL: +X -X -Y +Y +Z -Z = rt lf dn up bk ft
	std::vector<std::string> skyboxFaces;
	skyboxFaces.push_back("Textures/Skybox/rt.png");
	skyboxFaces.push_back("Textures/Skybox/lf.png");
	skyboxFaces.push_back("Textures/Skybox/dn.png");
	skyboxFaces.push_back("Textures/Skybox/up.png");
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
		if (KeyPressedOnce(GLFW_KEY_1)) SetCameraPreset(CAM_PRESETS[0]);
		if (KeyPressedOnce(GLFW_KEY_2)) SetCameraPreset(CAM_PRESETS[1]);
		if (KeyPressedOnce(GLFW_KEY_3)) SetCameraPreset(CAM_PRESETS[2]);

		camera.keyControl(mainWindow.getsKeys(), deltaTime);
		camera.mouseControl(mainWindow.getXChange(), mainWindow.getYChange());
		ConstrainCamera();

		// --- Luz de luna (fija) ---
		mainLight = DirectionalLight(MOON_COLOR.r, MOON_COLOR.g, MOON_COLOR.b,
			MOON_AMBIENT, MOON_DIFFUSE, MOON_DIR.x, MOON_DIR.y, MOON_DIR.z);

		// --- Luces de los 8 faroles mas cercanos a la camara ---
		glm::vec3 camPos = camera.getCameraPosition();
		std::vector<int> order(lanterns.size());
		for (size_t i = 0; i < order.size(); i++) order[i] = (int)i;
		auto dist2 = [&](int i) { glm::vec3 d = lanterns[i].anchor - camPos; return glm::dot(d, d); };
		unsigned int pointLightCount = (unsigned int)std::min((size_t)MAX_POINT_LIGHTS, order.size());
		std::partial_sort(order.begin(), order.begin() + pointLightCount, order.end(),
			[&](int a, int b) { return dist2(a) < dist2(b); });
		for (unsigned int i = 0; i < pointLightCount; i++) {
			const Lantern& l = lanterns[order[i]];
			float I = 1.9f * LanternFlicker(l, t); // I0 * (0.92 + 0.08 ruido)
			glm::vec3 p = glm::vec3(LanternPivotMatrix(l, t) *
				glm::vec4(0.0f, -(LANTERN_CORD_L + LANTERN_BODY_H * 0.5f), 0.0f, 1.0f));
			pointLights[i] = PointLight(LANTERN_COLOR.r, LANTERN_COLOR.g, LANTERN_COLOR.b,
				0.02f, I, p.x, p.y, p.z, 1.0f, 0.35f, 0.12f);
		}

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
		glUniform1f(shaderList[0].GetUniformLocation("fogDensity"), FOG_DENSITY);
		glUniform3f(shaderList[0].GetUniformLocation("fogColor"), FOG_COLOR.r, FOG_COLOR.g, FOG_COLOR.b);
		glUniformMatrix4fv(uniformProjection, 1, GL_FALSE, glm::value_ptr(projection));
		glUniformMatrix4fv(uniformView, 1, GL_FALSE, glm::value_ptr(camera.calculateViewMatrix()));
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
			glUniformMatrix4fv(uniformModel, 1, GL_FALSE, glm::value_ptr(pr.matrix));
			pr.model->RenderModel(uniformColor);   // color del material del glb
		}
		glEnable(GL_CULL_FACE);
		plainTexture.UseTexture();

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
			glm::mat4 pivot = LanternPivotMatrix(l, t);
			float flick = LanternFlicker(l, t);

			setColor(0.05f, 0.04f, 0.03f);
			drawBox(pivot, glm::vec3(0.015f, LANTERN_CORD_L, 0.015f), glm::vec3(0.5f, 1.0f, 0.5f));
			glm::mat4 body = glm::translate(pivot, glm::vec3(0.0f, -LANTERN_CORD_L - LANTERN_BODY_H, 0.0f));
			drawBox(body, glm::vec3(LANTERN_BODY_R * 1.6f, 0.04f, LANTERN_BODY_R * 1.6f), glm::vec3(0.5f, 0.0f, 0.5f));   // tapa inferior
			drawBox(glm::translate(body, glm::vec3(0.0f, LANTERN_BODY_H, 0.0f)),
				glm::vec3(LANTERN_BODY_R * 1.6f, 0.04f, LANTERN_BODY_R * 1.6f), glm::vec3(0.5f, 1.0f, 0.5f));           // tapa superior

			glUniform1i(uniformNoLighting, 1);
			setColor(1.0f * flick, 0.50f * flick, 0.18f * flick);
			drawMesh(MESH_CILINDRO, glm::scale(body, glm::vec3(LANTERN_BODY_R, LANTERN_BODY_H, LANTERN_BODY_R)));
			glUniform1i(uniformNoLighting, 0);
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

		if (shotsDir) { // modo revision: 1 captura por vista, luego cierra
			const int N = (int)(sizeof(AUTO_VIEWS) / sizeof(AUTO_VIEWS[0]));
			int view = shotFrame / 30, phase = shotFrame % 30;
			if (phase == 0 && view < N) { SetCameraPreset(AUTO_VIEWS[view]); freeCamera = true; }
			if (phase == 20 && view < N) {
				char path[512];
				snprintf(path, sizeof(path), "%s/view%d.tga", shotsDir, view);
				SaveScreenshotTGA(path);
			}
			shotFrame++;
			if (view >= N) mainWindow.setShouldClose(true);
		}

		glFlush();
		mainWindow.swapBuffers();
	}

	if (bgmLoaded) ma_sound_uninit(&bgm);
	if (audioReady) ma_engine_uninit(&audioEngine);

	return 0;
}
