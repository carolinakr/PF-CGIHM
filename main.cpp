/*
Proyecto Final - CGIHM
Base limpia derivada de ProyectoTeoria: toda la logica (camara, luces, ciclo dia/noche,
keyframes, animacion procedural, jerarquia de modelos, skybox, audio) sin modelos ni texturas.
La escena de abajo usa solo geometria procedural como ejemplo; reemplazala por la tuya.

Controles:
  W A S D + mouse : camara
  1 - 4           : camaras predefinidas (editar CAM_PRESETS)
  7               : inicia/reinicia el ciclo dia/noche
  F               : enciende/apaga la maquina giratoria (arranque lento + inercia al apagar)
  C               : camina / detiene la figura procedural
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

// Ciclo dia/noche
static const float CYCLE_DURATION = 60.0f; // segundos por dia completo
float cycleElapsed = 0.0f;                 // 0 = mediodia (cos(0) = 1)
bool  cycleRunning = false;

// Indices de meshes procedurales (se asignan en CreateObjects)
int MESH_CAJA     = -1; // caja unitaria [0,1]^3
int MESH_CILINDRO = -1; // cilindro radio 1, altura 1 (solo lateral)
int MESH_PISO     = -1; // quad horizontal con UVs repetidas
int MESH_PIRAMIDE = -1; // piramide con normales promediadas
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
		verts.insert(verts.end(), { x0, 0.0f, z0,  u0, 0.0f,  x0, 0.0f, z0 });
		verts.insert(verts.end(), { x1, 0.0f, z1,  u1, 0.0f,  x1, 0.0f, z1 });
		verts.insert(verts.end(), { x1, 1.0f, z1,  u1, 1.0f,  x1, 0.0f, z1 });
		verts.insert(verts.end(), { x0, 1.0f, z0,  u0, 1.0f,  x0, 0.0f, z0 });
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

	// Piso horizontal 40x40 centrado en el origen. UVs de 0 a 20 (la textura se repite).
	{
		unsigned int idx[] = { 0, 2, 1,  1, 2, 3 };
		GLfloat v[] = {
			// x      y     z       u      v      nx    ny    nz
			-20.0f, 0.0f, -20.0f,   0.0f,  0.0f,  0.0f, 1.0f, 0.0f,
			 20.0f, 0.0f, -20.0f,  20.0f,  0.0f,  0.0f, 1.0f, 0.0f,
			-20.0f, 0.0f,  20.0f,   0.0f, 20.0f,  0.0f, 1.0f, 0.0f,
			 20.0f, 0.0f,  20.0f,  20.0f, 20.0f,  0.0f, 1.0f, 0.0f,
		};
		Mesh* m = new Mesh();
		m->CreateMesh(v, idx, 32, 6);
		meshList.push_back(m);
		MESH_PISO = (int)meshList.size() - 1;
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
			0.0f, 0.0f, 0.0f,  0.0f, 0.0f,  0.0f, 0.0f, 1.0f,
			1.0f, 0.0f, 0.0f,  1.0f, 0.0f,  0.0f, 0.0f, 1.0f,
			1.0f, 1.0f, 0.0f,  1.0f, 1.0f,  0.0f, 0.0f, 1.0f,
			0.0f, 1.0f, 0.0f,  0.0f, 1.0f,  0.0f, 0.0f, 1.0f,
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

// Ejemplo: orbita de un "dron" - variables { x, y, z, yaw }
KeyframePath dronPath;
float dron_helixAngle = 0.0f;

void InitAnimations()
{
	dronPath = KeyframePath(
		{
			//   x      y     z     yaw
			{  0.0f,  6.0f,  12.0f,   0.0f },
			{  9.0f,  8.0f,   9.0f,  45.0f },
			{ 12.0f,  7.0f,   0.0f,  90.0f },
			{  9.0f,  9.0f,  -9.0f, 135.0f },
			{  0.0f,  7.0f, -12.0f, 180.0f },
			{ -9.0f,  8.0f,  -9.0f, 225.0f },
			{-12.0f,  7.0f,   0.0f, 270.0f },
			{ -9.0f,  9.0f,   9.0f, 315.0f },
			{  0.0f,  6.0f,  12.0f, 360.0f }, // = KF0, cierra el loop
		},
		{ 200, 200, 200, 200, 200, 200, 200, 200 });
}

// ============================================================
//  ANIMACION PROCEDURAL (caminar con sinf) y MAQUINA CON INERCIA
// ============================================================
bool  walkerWalking   = true;
float walker_cycle    = 0.0f;   // fase acumulada del paso
float walker_posZ     = 8.0f;
float walker_dir      = 1.0f;   // +1 avanza hacia -Z, -1 hacia +Z

// Maquina giratoria: vibra al encender, gira tras un retardo y se frena con inercia al apagar
bool  machineOn           = false;
bool  machineStopping     = false;
float machineStartTime    = 0.0f;
float machineStopTime     = 0.0f;
float machineAngle        = 0.0f;
float machinePulse        = 1.0f;  // escala pulsante (llama/vapor)
float machineVib          = 0.0f;  // vibracion en grados
const float MACHINE_SPIN_DELAY    = 5.0f;   // seg antes de que empiece a girar
const float MACHINE_SPIN_DEG_SECS = 90.0f;  // velocidad maxima de giro
const float MACHINE_DECEL_SECS    = 2.5f;   // tiempo que tarda en frenar

void UpdateMachine(bool togglePressed, float dt)
{
	float now = (float)glfwGetTime();
	if (togglePressed) {
		machineOn = !machineOn;
		if (machineOn) {
			machineStartTime = now;
			machineStopping  = false;
		}
		else if (now - machineStartTime > MACHINE_SPIN_DELAY) {
			machineStopping = true;     // ya estaba girando: frena con inercia
			machineStopTime = now;
		}
	}

	if (machineOn) {
		float t = now - machineStartTime;
		machinePulse = 1.0f + 0.2f * sinf(t * 9.0f);
		machineVib   = 8.0f * sinf(t * 13.0f);
		if (t > MACHINE_SPIN_DELAY)
			machineAngle += MACHINE_SPIN_DEG_SECS * dt;
	}

	if (machineStopping) {
		float factor = 1.0f - ((now - machineStopTime) / MACHINE_DECEL_SECS);
		if (factor > 0.0f)
			machineAngle += MACHINE_SPIN_DEG_SECS * dt * factor;
		else
			machineStopping = false;
	}
}

// ============================================================
//  CAMARAS PREDEFINIDAS  (teclas 1-4)   Camera(pos, up, yaw, pitch, moveSpeed, turnSpeed)
//  yaw: -90 mira a -Z, 90 a +Z, 0 a +X, 180 a -X
// ============================================================
struct CamPreset { glm::vec3 pos; float yaw, pitch; };
const CamPreset CAM_PRESETS[3] = {
	{ glm::vec3(0.0f, 1.7f, 14.0f),  -90.0f,  0.0f },   // 1: inicio
	{ glm::vec3(10.0f, 4.0f, 10.0f), -135.0f, -15.0f },  // 2: esquina
	{ glm::vec3(0.0f, 25.0f, 0.1f),  -90.0f, -89.0f },   // 3: vista cenital
};
const float CAM_MOVE_SPEED = 3.5f;
const float CAM_TURN_SPEED = 0.25f;

void SetCameraPreset(const CamPreset& p)
{
	camera = Camera(p.pos, glm::vec3(0.0f, 1.0f, 0.0f), p.yaw, p.pitch, CAM_MOVE_SPEED, CAM_TURN_SPEED);
}

// ============================================================
//  MAIN
// ============================================================
int main()
{
	mainWindow = Window(1366, 768);
	if (mainWindow.Initialise() != 0)
		return 1;

	// ─── Audio (opcional): si no existe el archivo, simplemente no suena ───
	ma_sound bgm;
	bool bgmLoaded = false;
	bool audioReady = (ma_engine_init(NULL, &audioEngine) == MA_SUCCESS);
	if (audioReady)
	{
		if (ma_sound_init_from_file(&audioEngine, "Audio/background_sound.mp3",
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
	InitAnimations();

	SetCameraPreset(CAM_PRESETS[0]);

	// ─── Texturas ─── (agrega las tuyas; sin archivo se ve blanco y respeta luz/color)
	plainTexture = Texture("Textures/plain.png");
	plainTexture.LoadTextureA();
	pisoTexture = Texture("Textures/suelo.png");
	pisoTexture.LoadTextureA();

	// ─── Modelos ─── ejemplo (comentado, no hay modelos todavia):
	//   Model Mi_M;  (global)
	//   Mi_M.LoadModel("Models/Carpeta/mi_modelo.glb");
	//   ... al renderizar:  Mi_M.RenderModel(uniformColor);   // uniformColor usa el color del material del glb
	//   Si el archivo falta, LoadModel avisa en consola y RenderModel no dibuja nada.

	// ─── Skybox ─── (orden: +X -X -Y +Y +Z -Z). Sin imagenes se genera un cielo de color solido.
	// El tint se modula con el ciclo dia/noche, asi que un solo skybox basta.
	std::vector<std::string> skyboxFaces;
	skyboxFaces.push_back("Textures/Skybox/rt.tga");
	skyboxFaces.push_back("Textures/Skybox/lf.tga");
	skyboxFaces.push_back("Textures/Skybox/dn.tga");
	skyboxFaces.push_back("Textures/Skybox/up.tga");
	skyboxFaces.push_back("Textures/Skybox/bk.tga");
	skyboxFaces.push_back("Textures/Skybox/ft.tga");
	skybox = Skybox(skyboxFaces);

	Material_brillante = Material(4.0f, 256);
	Material_opaco     = Material(0.3f, 4);

	// Luz direccional (solo 1 y siempre debe existir); se recalcula cada frame
	mainLight = DirectionalLight(1.0f, 1.0f, 1.0f, 0.3f, 0.3f, 0.0f, 0.0f, -1.0f);

	// Linterna pegada a la camara
	unsigned int spotLightCount = 0;
	spotLights[0] = SpotLight(1.0f, 1.0f, 1.0f,
		0.0f, 2.0f,
		0.0f, 0.0f, 0.0f,
		0.0f, -1.0f, 0.0f,
		1.0f, 0.0f, 0.0f,
		5.0f);
	spotLightCount++;

	// Lamparas puntuales (se actualizan cada frame segun dia/noche)
	const unsigned int POINT_LIGHT_COUNT = 3;
	const float LAMP_Y = 3.0f;
	const float lampPos[POINT_LIGHT_COUNT][2] = { { -6.0f, -6.0f }, { 6.0f, -6.0f }, { 0.0f, 4.0f } }; // x, z

	GLuint uniformProjection = 0, uniformModel = 0, uniformView = 0, uniformEyePosition = 0,
		uniformSpecularIntensity = 0, uniformShininess = 0, uniformTextureOffset = 0;
	GLuint uniformColor = 0, uniformAlpha = 0, uniformNoLighting = 0;

	glm::mat4 projection = glm::perspective(45.0f, (GLfloat)mainWindow.getBufferWidth() / mainWindow.getBufferHeight(), 0.1f, 1000.0f);

	glm::vec3 lowerLight(0.0f);
	glm::mat4 model(1.0f);
	glm::vec3 color(1.0f);
	glm::vec2 toffset(0.0f);

	// Spritesheet de ejemplo: cambia de "frame" cada 0.5 s (cols x rows)
	int   spriteFrame = 0;
	float spriteLastChange = 0.0f;
	const int SPRITE_COLS = 4, SPRITE_ROWS = 3, SPRITE_COUNT = 9;

	glEnable(GL_CULL_FACE); // las mallas estan en CCW; para modelos de doble cara usa glDisable/glEnable alrededor

	// ─── Loop principal ───────────────────────────────────────────────
	while (!mainWindow.getShouldClose())
	{
		GLfloat now = (GLfloat)glfwGetTime();
		deltaTime = now - lastTime;
		lastTime = now;

		glfwPollEvents();

		// --- Entrada ---
		if (KeyPressedOnce(GLFW_KEY_C)) walkerWalking = !walkerWalking;
		bool toggleMachine = KeyPressedOnce(GLFW_KEY_F);
		if (KeyPressedOnce(GLFW_KEY_7)) { cycleElapsed = 0.0f; cycleRunning = true; }

		if (KeyPressedOnce(GLFW_KEY_1)) SetCameraPreset(CAM_PRESETS[0]);
		if (KeyPressedOnce(GLFW_KEY_2)) SetCameraPreset(CAM_PRESETS[1]);
		if (KeyPressedOnce(GLFW_KEY_3)) SetCameraPreset(CAM_PRESETS[2]);
		if (KeyPressedOnce(GLFW_KEY_4)) // sigue al dron en tiempo real (posicion al momento de pulsar)
			SetCameraPreset({ glm::vec3(dronPath[0], dronPath[1] + 1.5f, dronPath[2] + 4.0f), -90.0f, -15.0f });

		camera.keyControl(mainWindow.getsKeys(), deltaTime);
		camera.mouseControl(mainWindow.getXChange(), mainWindow.getYChange());

		// --- Animaciones ---
		dronPath.Step();
		dron_helixAngle += deltaTime * 720.0f; // 2 vueltas/seg

		if (walkerWalking) {
			walker_cycle += deltaTime * 4.0f;
			walker_posZ  -= walker_dir * deltaTime * 1.5f;
			if (walker_posZ < -15.0f) { walker_posZ = -15.0f; walker_dir = -1.0f; }
			if (walker_posZ >  15.0f) { walker_posZ =  15.0f; walker_dir =  1.0f; }
		}

		UpdateMachine(toggleMachine, deltaTime);

		{
			float t = (float)glfwGetTime();
			if (t - spriteLastChange > 0.5f) {
				spriteFrame = (spriteFrame + 1) % SPRITE_COUNT;
				spriteLastChange = t;
			}
		}

		// --- Ciclo dia/noche ---
		if (cycleRunning) {
			cycleElapsed += deltaTime;
			if (cycleElapsed >= CYCLE_DURATION) cycleElapsed -= CYCLE_DURATION;
		}
		float cycleTime = cycleElapsed / CYCLE_DURATION;
		float sunAngle  = cycleTime * 2.0f * PI_F;
		float dayFactor = glm::max(0.0f, cosf(sunAngle)); // 1 = mediodia, 0 = noche
		float sunDirX   = 0.3f;
		float sunDirY   = -cosf(sunAngle);
		float sunDirZ   = sinf(sunAngle);

		glm::vec3 dayColor(1.00f, 0.95f, 0.80f);
		glm::vec3 dawnColor(1.00f, 0.45f, 0.10f);
		glm::vec3 nightColor(0.02f, 0.02f, 0.10f);
		glm::vec3 sunColor;
		if      (dayFactor > 0.3f) sunColor = glm::mix(dawnColor, dayColor,  (dayFactor - 0.3f) / 0.7f);
		else if (dayFactor > 0.0f) sunColor = glm::mix(nightColor, dawnColor, dayFactor / 0.3f);
		else                       sunColor = nightColor;

		mainLight = DirectionalLight(
			sunColor.r, sunColor.g, sunColor.b,
			0.05f + dayFactor * 0.18f,   // ambiental
			dayFactor * 0.45f,           // difusa
			sunDirX, sunDirY, sunDirZ);

		// Lamparas: siempre prendidas, mas intensas de noche. Atenuacion = exp*d^2 + lin*d + con
		{
			float nightFactor = 1.0f - dayFactor;
			float lampAmbient = 0.1f + 0.5f * nightFactor;
			float lampDiffuse = 1.0f + 3.0f * nightFactor;
			for (unsigned int i = 0; i < POINT_LIGHT_COUNT; i++)
				pointLights[i] = PointLight(1.0f, 1.0f, 0.92f, lampAmbient, lampDiffuse,
					lampPos[i][0], LAMP_Y, lampPos[i][1], 1.0f, 0.8f, 0.6f);
		}

		// --- Limpiar + skybox ---
		glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		glm::vec3 skyTint = glm::max(sunColor, glm::vec3(0.15f, 0.15f, 0.35f)); // oscurece de noche
		skybox.SetTint(skyTint.r, skyTint.g, skyTint.b);
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
		glUniformMatrix4fv(uniformProjection, 1, GL_FALSE, glm::value_ptr(projection));
		glUniformMatrix4fv(uniformView, 1, GL_FALSE, glm::value_ptr(camera.calculateViewMatrix()));
		glUniform3f(uniformEyePosition, camera.getCameraPosition().x, camera.getCameraPosition().y, camera.getCameraPosition().z);

		lowerLight = camera.getCameraPosition();
		lowerLight.y -= 0.3f;
		spotLights[0].SetFlash(lowerLight, camera.getCameraDirection());

		shaderList[0].SetDirectionalLight(&mainLight);
		shaderList[0].SetPointLights(pointLights, POINT_LIGHT_COUNT);
		shaderList[0].SetSpotLights(spotLights, spotLightCount);

		color = glm::vec3(1.0f);
		toffset = glm::vec2(0.0f);
		glUniform3fv(uniformColor, 1, glm::value_ptr(color));
		glUniform2fv(uniformTextureOffset, 1, glm::value_ptr(toffset));
		glDisable(GL_BLEND);

		// Helpers de dibujo: fijan color + modelo y dibujan un mesh
		auto setColor = [&](float r, float g, float b) {
			color = glm::vec3(r, g, b);
			glUniform3fv(uniformColor, 1, glm::value_ptr(color));
		};
		auto drawMesh = [&](int meshIdx, const glm::mat4& m) {
			glUniformMatrix4fv(uniformModel, 1, GL_FALSE, glm::value_ptr(m));
			meshList[meshIdx]->RenderMesh();
		};
		// Caja con pivote: 'pivot' esta en coordenadas de la caja unitaria (0.5,1,0.5 = centro de arriba).
		// Sirve para que las rotaciones de extremidades giren en la articulacion.
		auto drawBox = [&](const glm::mat4& parent, glm::vec3 size, glm::vec3 pivot) {
			glm::mat4 m = glm::scale(parent, size);
			m = glm::translate(m, -pivot);
			drawMesh(MESH_CAJA, m);
		};

		// ============================================================
		//  ESCENA DE EJEMPLO  (reemplazar por la escena del proyecto)
		// ============================================================

		// Piso
		setColor(1.0f, 1.0f, 1.0f);
		pisoTexture.UseTexture();
		Material_opaco.UseMaterial(uniformSpecularIntensity, uniformShininess);
		drawMesh(MESH_PISO, glm::mat4(1.0f));

		plainTexture.UseTexture();

		// Pilares (cilindros) y caja base de cada lampara
		setColor(0.58f, 0.56f, 0.52f);
		for (unsigned int i = 0; i < POINT_LIGHT_COUNT; i++) {
			model = glm::translate(glm::mat4(1.0f), glm::vec3(lampPos[i][0], 0.0f, lampPos[i][1]));
			model = glm::scale(model, glm::vec3(0.2f, LAMP_Y, 0.2f));
			drawMesh(MESH_CILINDRO, model);
		}

		// Piramide con normales promediadas, material brillante
		Material_brillante.UseMaterial(uniformSpecularIntensity, uniformShininess);
		setColor(0.85f, 0.25f, 0.20f);
		model = glm::translate(glm::mat4(1.0f), glm::vec3(-4.0f, 1.0f, 2.0f));
		drawMesh(MESH_PIRAMIDE, model);
		Material_opaco.UseMaterial(uniformSpecularIntensity, uniformShininess);

		// Quad con hoja de sprites: toffset se aplica solo a este objeto y se reinicia
		toffset = SpriteSheetOffset(spriteFrame, SPRITE_COLS, SPRITE_ROWS);
		glUniform2fv(uniformTextureOffset, 1, glm::value_ptr(toffset));
		setColor(0.9f, 0.9f, 0.3f);
		model = glm::translate(glm::mat4(1.0f), glm::vec3(2.0f, 0.5f, -3.0f));
		drawMesh(MESH_QUAD, model);
		toffset = glm::vec2(0.0f);
		glUniform2fv(uniformTextureOffset, 1, glm::value_ptr(toffset));

		// Transparencia: blending + uniform alphaValue (volver a 1.0 y glDisable al terminar)
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glUniform1f(uniformAlpha, 0.4f);
		setColor(0.3f, 0.6f, 1.0f);
		glDisable(GL_CULL_FACE);
		model = glm::translate(glm::mat4(1.0f), glm::vec3(4.0f, 0.0f, -3.0f));
		model = glm::scale(model, glm::vec3(2.0f, 1.5f, 1.0f));
		drawMesh(MESH_QUAD, model);
		glEnable(GL_CULL_FACE);
		glUniform1f(uniformAlpha, 1.0f);
		glDisable(GL_BLEND);

		// Maquina giratoria con inercia (tecla F): base + eje que vibra, pulsa y gira
		{
			setColor(0.45f, 0.45f, 0.5f);
			model = glm::translate(glm::mat4(1.0f), glm::vec3(-6.0f, 0.0f, -2.0f));
			drawBox(model, glm::vec3(1.0f, 0.6f, 1.0f), glm::vec3(0.5f, 0.0f, 0.5f));

			glm::mat4 rot = glm::translate(glm::mat4(1.0f), glm::vec3(-6.0f, 1.1f, -2.0f));
			rot = glm::rotate(rot, glm::radians(machineVib), glm::vec3(0.0f, 0.0f, 1.0f));    // vibracion
			rot = glm::rotate(rot, glm::radians(machineAngle), glm::vec3(0.0f, 1.0f, 0.0f));   // giro
			rot = glm::scale(rot, glm::vec3(machinePulse));                                     // pulso
			setColor(machineOn ? 1.0f : 0.6f, machineOn ? 0.5f : 0.6f, 0.2f);
			drawBox(rot, glm::vec3(1.2f, 0.25f, 0.25f), glm::vec3(0.5f, 0.5f, 0.5f));
		}

		// Figura procedural con jerarquia (tecla C): cada parte hereda la matriz de su padre
		{
			float wc       = walker_cycle;
			float walkAmt  = walkerWalking ? 1.0f : 0.0f;
			float legSwing = sinf(wc) * 25.0f * walkAmt;
			float kneeFlx  = fabsf(sinf(wc)) * 18.0f * walkAmt;
			float armSwing = sinf(wc) * 30.0f * walkAmt;
			float bodyBob  = fabsf(sinf(wc)) * 0.04f * walkAmt;
			float faceYaw  = (walker_dir > 0) ? 0.0f : 180.0f;

			glm::mat4 base = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, bodyBob, walker_posZ));
			base = glm::rotate(base, glm::radians(faceYaw), glm::vec3(0.0f, 1.0f, 0.0f));

			// Torso (origen en la cadera) y cabeza
			glm::mat4 hip = glm::translate(base, glm::vec3(0.0f, 0.9f, 0.0f));
			setColor(0.2f, 0.45f, 0.85f);
			drawBox(hip, glm::vec3(0.4f, 0.6f, 0.2f), glm::vec3(0.5f, 0.0f, 0.5f));
			setColor(0.95f, 0.8f, 0.65f);
			drawBox(glm::translate(hip, glm::vec3(0.0f, 0.65f, 0.0f)), glm::vec3(0.22f, 0.22f, 0.22f), glm::vec3(0.5f, 0.0f, 0.5f));

			for (int side = -1; side <= 1; side += 2) {
				float s = (float)side;
				// Brazo: pivote en el hombro
				glm::mat4 shoulder = glm::translate(hip, glm::vec3(0.27f * s, 0.6f, 0.0f));
				shoulder = glm::rotate(shoulder, glm::radians(-armSwing * s), glm::vec3(1.0f, 0.0f, 0.0f));
				setColor(0.2f, 0.45f, 0.85f);
				drawBox(shoulder, glm::vec3(0.12f, 0.5f, 0.12f), glm::vec3(0.5f, 1.0f, 0.5f));

				// Muslo (hereda cadera) -> pierna (hereda muslo)
				glm::mat4 thigh = glm::translate(hip, glm::vec3(0.1f * s, 0.0f, 0.0f));
				thigh = glm::rotate(thigh, glm::radians(legSwing * s), glm::vec3(1.0f, 0.0f, 0.0f));
				setColor(0.15f, 0.15f, 0.2f);
				drawBox(thigh, glm::vec3(0.16f, 0.4f, 0.16f), glm::vec3(0.5f, 1.0f, 0.5f));

				glm::mat4 shin = glm::translate(thigh, glm::vec3(0.0f, -0.4f, 0.0f));
				shin = glm::rotate(shin, glm::radians(kneeFlx), glm::vec3(1.0f, 0.0f, 0.0f));
				drawBox(shin, glm::vec3(0.14f, 0.45f, 0.14f), glm::vec3(0.5f, 1.0f, 0.5f));
			}
		}

		// Dron: trayectoria por keyframes (x,y,z,yaw) + 4 helices que heredan su matriz
		{
			Material_brillante.UseMaterial(uniformSpecularIntensity, uniformShininess);
			glm::mat4 dronBase = glm::translate(glm::mat4(1.0f), glm::vec3(dronPath[0], dronPath[1], dronPath[2]));
			dronBase = glm::rotate(dronBase, glm::radians(dronPath[3]), glm::vec3(0.0f, 1.0f, 0.0f));

			setColor(0.15f, 0.15f, 0.15f);
			drawBox(dronBase, glm::vec3(0.8f, 0.2f, 0.8f), glm::vec3(0.5f, 0.5f, 0.5f));

			const glm::vec3 heliceOffsets[4] = {
				glm::vec3(-0.6f, 0.15f,  0.6f), glm::vec3(0.6f, 0.15f,  0.6f),
				glm::vec3(-0.6f, 0.15f, -0.6f), glm::vec3(0.6f, 0.15f, -0.6f),
			};
			const float heliceSigns[4] = { 1.0f, -1.0f, -1.0f, 1.0f }; // giro alterno CW/CCW
			setColor(0.9f, 0.9f, 0.9f);
			for (int hi = 0; hi < 4; hi++) {
				glm::mat4 mH = glm::translate(dronBase, heliceOffsets[hi]);
				mH = glm::rotate(mH, glm::radians(dron_helixAngle * heliceSigns[hi]), glm::vec3(0.0f, 1.0f, 0.0f));
				drawBox(mH, glm::vec3(0.7f, 0.03f, 0.08f), glm::vec3(0.5f, 0.5f, 0.5f));
			}
			Material_opaco.UseMaterial(uniformSpecularIntensity, uniformShininess);
		}

		glFlush();
		mainWindow.swapBuffers();
	}

	if (bgmLoaded) ma_sound_uninit(&bgm);
	if (audioReady) ma_engine_uninit(&audioEngine);

	return 0;
}
