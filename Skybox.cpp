
#include "Skybox.h"


// Constructor por defecto de Skybox.
Skybox::Skybox()
{
}


// Recibe las seis imágenes que se utilizarán para formar el cielo.
// También configura el shader, las texturas y la geometría del cubo.
Skybox::Skybox(std::vector<std::string> faceLocations)
{
	// Creamos el shader que se encargará de dibujar el Skybox.
	skyShader = new Shader();
	skyShader->CreateFromFiles("shaders/skybox.vert", "shaders/skybox.frag");

	// Guardamos las ubicaciones de las matrices de proyección y vista.
	uniformProjection = skyShader->GetProjectionLocation();
	uniformView = skyShader->GetViewLocation();

	// Obtenemos la variable uniform que permite cambiar el color del cielo.
	uniformTint = skyShader->GetUniformLocation("skyTint");

	// Iniciamos el color sin aplicar ningún cambio a las texturas.
	tint = glm::vec3(1.0f, 1.0f, 1.0f);

	// Generamos la textura y la configuramos como un mapa cúbico.
	glGenTextures(1, &textureId);
	glBindTexture(GL_TEXTURE_CUBE_MAP, textureId);

	int width, height, bitDepth;

	// Las caras se guardan sin voltear (Texture.cpp activa el flip global de stb)
	stbi_set_flip_vertically_on_load(false);

	// Recorremos las seis caras que forman el Skybox.
	for (size_t i = 0; i < 6; i++)
	{
		// Cargamos la imagen correspondiente a cada cara del cubo.
		// Si no hay una ruta disponible, se asigna nullptr.
		unsigned char *texData = (i < faceLocations.size())
			? stbi_load(faceLocations[i].c_str(), &width, &height, &bitDepth, 0) : nullptr;

		if (!texData)
		{
			// Sin textura: cara de color solido (orden GL: +X -X +Y -Y +Z -Z) para que el
			// skybox y su tint (ciclo dia/noche) sigan funcionando.
			// Si no se encuentra una imagen, mostramos un mensaje y utilizamos
			// un color de respaldo para que esa cara no quede sin textura.
			if (i < faceLocations.size())
				printf("No se encontro : %s\n", faceLocations[i].c_str());

			// Definimos los colores que se utilizarán como reemplazo.
			const unsigned char sky[3]    = { 150, 195, 240 };
			const unsigned char top[3]    = {  90, 150, 235 };
			const unsigned char ground[3] = {  90,  90,  90 };

			// Elegimos el color según la cara que se esté procesando.
			const unsigned char* c = (i == 2) ? top : (i == 3) ? ground : sky;

			// Ajustamos la alineación y asignamos el color a la cara del cubo.
			glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
			glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB, 1, 1, 0, GL_RGB, GL_UNSIGNED_BYTE, c);
			continue;
		}

		// Revisamos si la imagen tiene formato RGB o RGBA según sus canales.
		GLenum fmt  = (bitDepth == 4) ? GL_RGBA : GL_RGB;
		GLenum ifmt = (bitDepth == 4) ? GL_RGBA : GL_RGB;

		// Cargamos la imagen en la cara correspondiente del mapa cúbico.
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, ifmt, width, height, 0, fmt, GL_UNSIGNED_BYTE, texData);

		// Liberamos la memoria de la imagen una vez que OpenGL ya la cargó.
		stbi_image_free(texData);
	}

	// Evitamos que las texturas se repitan en los bordes del cubo.
	// Esto ayuda a que las uniones entre las caras se vean continuas.
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

	// Configuramos el filtrado lineal para ampliar o reducir las texturas.
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	// Definimos los índices que conectan los vértices para formar
	// los triángulos que componen las seis caras del cubo.
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

	// Definimos las posiciones de los ocho vértices del cubo.
	// Sus coordenadas van de -1 a 1 y forman la geometría del Skybox.
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

	// Creamos la malla y le pasamos los vértices e índices del cubo.
	skyMesh = new Mesh();
	skyMesh->CreateMesh(skyboxVertices, skyboxIndices, 64, 36);
}


// Dibuja el Skybox utilizando las matrices de vista y proyección de la cámara.
void Skybox::DrawSkybox(glm::mat4 viewMatrix, glm::mat4 projectionMatrix)
{
	// Quitamos la traslación de la cámara para que el cielo
	// permanezca centrado mientras el usuario se mueve por la escena.
	viewMatrix = glm::mat4(glm::mat3(viewMatrix));

	// Desactivamos temporalmente la escritura en el búfer de profundidad
	// para que el Skybox no interfiera con los demás objetos.
	glDepthMask(false);

	// Activamos el shader y enviamos las matrices necesarias para dibujar el cielo.
	skyShader->UseShader();
	glUniformMatrix4fv(uniformProjection, 1, GL_FALSE, glm::value_ptr(projectionMatrix));
	glUniformMatrix4fv(uniformView, 1, GL_FALSE, glm::value_ptr(viewMatrix));

	// Activamos la unidad de textura y vinculamos el mapa cúbico.
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_CUBE_MAP, textureId);

	// Enviamos el color actual para modificar la apariencia del cielo.
	glUniform3f(uniformTint, tint.r, tint.g, tint.b);

	// Dibujamos la malla del Skybox.
	skyMesh->RenderMesh();

	// Volvemos a activar la escritura en el búfer de profundidad.
	glDepthMask(true);
}


// Permite cambiar el color del cielo mediante sus componentes RGB.
void Skybox::SetTint(GLfloat r, GLfloat g, GLfloat b)
{
	tint = glm::vec3(r, g, b);
}


// Destructor de Skybox.
Skybox::~Skybox()
{
}
