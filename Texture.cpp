
#include "Texture.h"
#include "CommonValues.h"


// Constructor por defecto: inicializa los datos de la textura en cero.
Texture::Texture()
{
	textureID = 0;
	width = 0;
	height = 0;
	bitDepth = 0;
	fileLocation = 0;
}


// Constructor que recibe la ruta del archivo de imagen que se cargará.
Texture::Texture(const char *FileLoc)
{
	textureID = 0;
	width = 0;
	height = 0;
	bitDepth = 0;
	fileLocation = FileLoc;
}


// Carga una imagen desde un archivo y la prepara como textura RGBA.
bool Texture::LoadTextureA()
{
	// Volteamos la imagen verticalmente para adaptar su orientación a OpenGL.
	stbi_set_flip_vertically_on_load(true);

	// Cargamos la imagen solicitando cuatro canales de color: RGBA.
	unsigned char *texData = stbi_load(fileLocation, &width, &height, &bitDepth, STBI_rgb_alpha);

	// Comprobamos que la imagen se haya cargado correctamente.
	if (!texData)
	{
		printf("No se encontro el archivo: %s\n", fileLocation);
		return false;
	}

	// Generamos una textura y la vinculamos como textura 2D.
	glGenTextures(1, &textureID);
	glBindTexture(GL_TEXTURE_2D, textureID);

	// Configuramos cómo se comportará la textura cuando se repita.
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

	// Definimos el filtrado utilizado al reducir o ampliar la textura.
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	// Enviamos los datos de la imagen a OpenGL en formato RGBA.
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, texData);

	// Generamos versiones de menor resolución para utilizar mipmaps.
	glGenerateMipmap(GL_TEXTURE_2D);

	// Desvinculamos la textura y liberamos la memoria de la imagen cargada.
	glBindTexture(GL_TEXTURE_2D, 0);
	stbi_image_free(texData);

	return true;
}


// Carga una textura directamente desde un bloque de datos almacenado en memoria.
// Esto permite utilizar imágenes sin necesidad de leerlas desde una ruta de archivo.
bool Texture::LoadTextureFromMemory(const unsigned char* data, int dataLen)
{
	// Variables temporales para almacenar las dimensiones y profundidad de color.
	int w, h, bd;

	// Mantenemos la misma orientación vertical utilizada en las otras texturas.
	stbi_set_flip_vertically_on_load(true);

	// Decodificamos la imagen desde memoria y solicitamos cuatro canales RGBA.
	unsigned char* texData = stbi_load_from_memory(data, dataLen, &w, &h, &bd, STBI_rgb_alpha);

	// Verificamos que los datos de la imagen se hayan podido procesar.
	if (!texData)
	{
		printf("No se pudo cargar textura embebida desde memoria\n");
		return false;
	}

	// Guardamos las dimensiones y la profundidad de color de la imagen.
	width = w; height = h; bitDepth = bd;

	// Creamos y vinculamos la textura 2D en OpenGL.
	glGenTextures(1, &textureID);
	glBindTexture(GL_TEXTURE_2D, textureID);

	// Configuramos la repetición de la textura en ambos ejes.
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

	// Establecemos el filtrado lineal para la visualización de la textura.
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	// Enviamos los datos RGBA de la imagen a la tarjeta gráfica.
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, texData);

	// Generamos los mipmaps para mejorar el muestreo a diferentes distancias.
	glGenerateMipmap(GL_TEXTURE_2D);

	// Desvinculamos la textura y liberamos los datos temporales de la imagen.
	glBindTexture(GL_TEXTURE_2D, 0);
	stbi_image_free(texData);

	return true;
}


// Carga una textura desde un archivo utilizando sus canales originales.
bool Texture::LoadTexture()
{
	// Ajustamos la orientación vertical de la imagen antes de cargarla.
	stbi_set_flip_vertically_on_load(true);

	// Leemos el archivo y obtenemos sus dimensiones y profundidad de color.
	unsigned char *texData = stbi_load(fileLocation, &width, &height, &bitDepth, 0);

	// Si no se encuentra la imagen, mostramos el error y detenemos la carga.
	if (!texData)
	{
		printf("No se encontro el archivo: %s\n", fileLocation);
		return false;
	}

	// Creamos la textura y la vinculamos a OpenGL.
	glGenTextures(1, &textureID);
	glBindTexture(GL_TEXTURE_2D, textureID);

	// Configuramos la repetición de la textura en los ejes horizontal y vertical.
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

	// Definimos el filtrado lineal para mejorar la apariencia de la imagen.
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	// Enviamos los datos de la imagen a OpenGL utilizando el formato RGB.
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, texData);

	// Generamos los mipmaps y liberamos los datos temporales.
	glGenerateMipmap(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, 0);
	stbi_image_free(texData);

	return true;
}


// Libera la textura y reinicia los valores asociados a ella.
void Texture::ClearTexture()
{
	// Eliminamos la textura de OpenGL si existe una textura cargada.
	if (textureID != 0)
		glDeleteTextures(1, &textureID);

	// Restablecemos los valores para indicar que no hay una textura activa.
	textureID = 0;
	width = 0;
	height = 0;
	bitDepth = 0;
	fileLocation = "";
}


// Textura blanca 1x1 compartida: se usa cuando el archivo no existe (o aun no hay
// texturas en el proyecto), asi el color del material/luz se sigue viendo.
// Crea una textura blanca de respaldo que puede utilizarse cuando no existe
// una textura válida para un objeto.
static GLuint GetFallbackTexture()
{
	// Mantenemos el identificador para reutilizar la misma textura.
	static GLuint fallbackID = 0;

	// Solo creamos la textura si todavía no existe.
	if (fallbackID == 0)
	{
		// Un único píxel blanco con sus cuatro canales RGBA.
		const unsigned char white[4] = { 255, 255, 255, 255 };

		// Generamos y vinculamos la textura de respaldo.
		glGenTextures(1, &fallbackID);
		glBindTexture(GL_TEXTURE_2D, fallbackID);

		// Configuramos la repetición y el filtrado de la textura.
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

		// Cargamos el único píxel blanco como una textura RGBA de 1x1.
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);

		// Desvinculamos la textura después de crearla.
		glBindTexture(GL_TEXTURE_2D, 0);
	}

	// Regresamos el identificador de la textura blanca.
	return fallbackID;
}


// Activa la unidad de textura y vincula la textura que utilizará el objeto.
void Texture::UseTexture()
{
	// Seleccionamos la primera unidad de textura de OpenGL.
	glActiveTexture(GL_TEXTURE0);

	// Si existe una textura cargada, utilizamos su identificador.
	// En caso contrario, usamos la textura blanca de respaldo.
	glBindTexture(GL_TEXTURE_2D, textureID != 0 ? textureID : GetFallbackTexture());
}


// Destructor: libera los recursos asociados a la textura.
Texture::~Texture()
{
	ClearTexture();
}
