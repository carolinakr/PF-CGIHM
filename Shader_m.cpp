
#include "Shader_m.h"

// Constructor: inicializamos el identificador del shader y sus variables uniform.
Shader::Shader()
{
	shaderID = 0;
	uniformModel = 0;
	uniformProjection = 0;
	uniformColor = 0;
}

// Recibe el código de los shaders directamente y lo manda a compilar.
void Shader::CreateFromString(const char* vertexCode, const char* fragmentCode)
{
	CompileShader(vertexCode, fragmentCode);
}

// Abre los archivos de los shaders y guarda su contenido para poder compilarlo.
void Shader::CreateFromFiles(const char* vertexLocation, const char* fragmentLocation)
{
	std::string vertexString = ReadFile(vertexLocation);
	std::string fragmentString = ReadFile(fragmentLocation);
	const char* vertexCode = vertexString.c_str();
	const char* fragmentCode = fragmentString.c_str();

	CompileShader(vertexCode, fragmentCode);
}

// Lee el contenido de un archivo de texto, en este caso,
// los archivos que contienen el código de los shaders.
std::string Shader::ReadFile(const char* fileLocation)
{
	std::string content;
	std::ifstream fileStream(fileLocation, std::ios::in);

	// Comprobamos que el archivo se haya abierto correctamente.
	if (!fileStream.is_open()) {
		printf("Failed to read %s! File doesn't exist.", fileLocation);
		return "";
	}

	std::string line = "";

	// Leemos el archivo línea por línea y vamos guardando su contenido.
	while (!fileStream.eof())
	{
		std::getline(fileStream, line);
		content.append(line + "\n");
	}

	fileStream.close();
	return content;
}

// Crea el programa de shaders, compila sus componentes y los vincula.
// También obtiene las ubicaciones de las variables uniform que se utilizarán
// para controlar las transformaciones y la apariencia de los objetos.
void Shader::CompileShader(const char* vertexCode, const char* fragmentCode)
{
	// Creamos el programa de shaders en OpenGL.
	shaderID = glCreateProgram();

	// Comprobamos que el programa se haya creado correctamente.
	if (!shaderID)
	{
		printf("Error creating shader program!\n");
		return;
	}

	// Agregamos los shaders de vértices y fragmentos al programa.
	AddShader(shaderID, vertexCode, GL_VERTEX_SHADER);
	AddShader(shaderID, fragmentCode, GL_FRAGMENT_SHADER);

	GLint result = 0;
	GLchar eLog[1024] = { 0 };

	// Vinculamos los shaders para formar un programa ejecutable.
	glLinkProgram(shaderID);
	glGetProgramiv(shaderID, GL_LINK_STATUS, &result);
	if (!result)
	{
		// Si ocurre algún error, obtenemos el mensaje que proporciona OpenGL.
		glGetProgramInfoLog(shaderID, sizeof(eLog), NULL, eLog);
		printf("Error linking program: '%s'\n", eLog);
		return;
	}

	// Verificamos que el programa de shaders sea válido.
	glValidateProgram(shaderID);
	glGetProgramiv(shaderID, GL_VALIDATE_STATUS, &result);
	if (!result)
	{
		glGetProgramInfoLog(shaderID, sizeof(eLog), NULL, eLog);
		printf("Error validating program: '%s'\n", eLog);
		return;
	}

	// Obtenemos las ubicaciones de las matrices que controlan
	// la proyección, posición y vista de los objetos.
	uniformProjection = glGetUniformLocation(shaderID, "projection");
	uniformModel = glGetUniformLocation(shaderID, "model");
	uniformView = glGetUniformLocation(shaderID, "view");

	// Obtenemos la posición de la cámara y la variable que controla el color.
	uniformEyePosition = glGetUniformLocation(shaderID, "eyePosition");
	uniformColor = glGetUniformLocation(shaderID, "color");
	
	
}

// Estos métodos permiten consultar las ubicaciones de las variables uniform
// para utilizarlas desde otras partes del programa.
GLuint Shader::GetProjectionLocation()
{
	return uniformProjection;
}

GLuint Shader::GetModelLocation()
{
	return uniformModel;
}

GLuint Shader::GetViewLocation()
{
	return uniformView;
}


// Devuelve la ubicación de la variable uniform que controla el color.
GLuint Shader::getColorLocation()
{
	return uniformColor;
}


// Activa el programa de shaders para que OpenGL lo utilice al dibujar los objetos.
void Shader::UseShader()
{
	glUseProgram(shaderID);
}

// Libera el programa de shaders y reinicia las ubicaciones de las variables uniform.
void Shader::ClearShader()
{
	if (shaderID != 0)
	{
		glDeleteProgram(shaderID);
		shaderID = 0;
	}

	uniformModel = 0;
	uniformProjection = 0;
	uniformColor = 0;
	uniformView = 0;

}


// Crea y compila un shader individual, ya sea de vértices o de fragmentos.
void Shader::AddShader(GLuint theProgram, const char* shaderCode, GLenum shaderType)
{
	// Creamos el shader según el tipo que se haya indicado.
	GLuint theShader = glCreateShader(shaderType);

	// Preparamos el código fuente y obtenemos su longitud.
	const GLchar* theCode[1];
	theCode[0] = shaderCode;

	GLint codeLength[1];
	codeLength[0] = strlen(shaderCode);

	// Enviamos el código fuente a OpenGL e iniciamos la compilación.
	glShaderSource(theShader, 1, theCode, codeLength);
	glCompileShader(theShader);

	GLint result = 0;
	GLchar eLog[1024] = { 0 };

	// Comprobamos si la compilación se realizó correctamente.
	glGetShaderiv(theShader, GL_COMPILE_STATUS, &result);
	if (!result)
	{
		// Si falla, obtenemos y mostramos el mensaje de error.
		glGetShaderInfoLog(theShader, sizeof(eLog), NULL, eLog);
		printf("Error compiling the %d shader: '%s'\n", shaderType, eLog);
		return;
	}

	// Si la compilación fue correcta, agregamos el shader al programa.
	glAttachShader(theProgram, theShader);
}

// Destructor: libera los recursos del programa de shaders al destruir el objeto.
Shader::~Shader()
{
	ClearShader();
}
