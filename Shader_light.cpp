
#include "Shader_light.h"

// Constructor: inicializamos los identificadores del shader y las variables uniform.
Shader::Shader()
{
	shaderID = 0;
	uniformModel = 0;
	uniformProjection = 0;
	uniformColor = 0;
	uniformTextureOffset = 0;
	pointLightCount = 0;
	spotLightCount = 0;
}

// Recibe directamente el código de los shaders y lo manda a compilar.
void Shader::CreateFromString(const char* vertexCode, const char* fragmentCode)
{
	CompileShader(vertexCode, fragmentCode);
}

// Carga los archivos de los shaders y obtiene su contenido para después compilarlo.
void Shader::CreateFromFiles(const char* vertexLocation, const char* fragmentLocation)
{
	std::string vertexString = ReadFile(vertexLocation);
	std::string fragmentString = ReadFile(fragmentLocation);
	const char* vertexCode = vertexString.c_str();
	const char* fragmentCode = fragmentString.c_str();

	CompileShader(vertexCode, fragmentCode);
}

// Abre y lee un archivo de texto para obtener el código fuente del shader.
std::string Shader::ReadFile(const char* fileLocation)
{
	std::string content;
	std::ifstream fileStream(fileLocation, std::ios::in);

	// Revisamos que el archivo se haya abierto correctamente.
	if (!fileStream.is_open()) {
		printf("Failed to read %s! File doesn't exist.", fileLocation);
		return "";
	}

	std::string line = "";

	// Leemos el archivo línea por línea y guardamos su contenido en una cadena.
	while (!fileStream.eof())
	{
		std::getline(fileStream, line);
		content.append(line + "\n");
	}

	fileStream.close();
	return content;
}

// Crea el programa de shaders, compila sus componentes y los vincula.
// También obtiene las ubicaciones de las variables uniform que utilizaremos
// para controlar las transformaciones, los materiales y la iluminación.
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

	// Vinculamos ambos shaders para formar un programa que pueda ejecutarse.
	glLinkProgram(shaderID);
	glGetProgramiv(shaderID, GL_LINK_STATUS, &result);
	if (!result)
	{
		// Si ocurre un error, obtenemos el mensaje que proporciona OpenGL.
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

	// Obtenemos las ubicaciones de las matrices de proyección, modelo y vista.
	uniformProjection = glGetUniformLocation(shaderID, "projection");
	uniformModel = glGetUniformLocation(shaderID, "model");
	uniformView = glGetUniformLocation(shaderID, "view");

	// Obtenemos las variables relacionadas con la luz direccional:
	// color, intensidad ambiental, dirección e intensidad difusa.
	uniformDirectionalLight.uniformcolor = glGetUniformLocation(shaderID, "directionalLight.base.color");
	uniformDirectionalLight.uniformAmbientIntensity = glGetUniformLocation(shaderID, "directionalLight.base.ambientIntensity");
	uniformDirectionalLight.uniformDirection = glGetUniformLocation(shaderID, "directionalLight.direction");
	uniformDirectionalLight.uniformDiffuseIntensity = glGetUniformLocation(shaderID, "directionalLight.base.diffuseIntensity");

	// Obtenemos los parámetros del material que controlan los reflejos y el brillo.
	uniformSpecularIntensity = glGetUniformLocation(shaderID, "material.specularIntensity");
	uniformShininess = glGetUniformLocation(shaderID, "material.shininess");

	// Obtenemos la posición de la cámara y las variables para controlar
	// el color, el desplazamiento de textura, la transparencia y la iluminación.
	uniformEyePosition = glGetUniformLocation(shaderID, "eyePosition");
	uniformColor = glGetUniformLocation(shaderID, "color");
	uniformTextureOffset = glGetUniformLocation(shaderID, "toffset");
	uniformAlpha = glGetUniformLocation(shaderID, "alphaValue");
	uniformNoLighting = glGetUniformLocation(shaderID, "noLighting");

	// Obtenemos la ubicación que indica cuántas luces puntuales están activas.
	uniformPointLightCount = glGetUniformLocation(shaderID, "pointLightCount");

	// Recorremos el arreglo de luces puntuales para obtener las ubicaciones
	// de sus propiedades dentro del shader.
	for (size_t i = 0; i < MAX_POINT_LIGHTS; i++)
	{
		char locBuff[100] = { '\0' };

		// Color de la luz.
		snprintf(locBuff, sizeof(locBuff), "pointLights[%d].base.color", i);
		uniformPointLight[i].uniformcolor = glGetUniformLocation(shaderID, locBuff);

		// Intensidad de la iluminación ambiental.
		snprintf(locBuff, sizeof(locBuff), "pointLights[%d].base.ambientIntensity", i);
		uniformPointLight[i].uniformAmbientIntensity = glGetUniformLocation(shaderID, locBuff);

		// Intensidad de la iluminación difusa.
		snprintf(locBuff, sizeof(locBuff), "pointLights[%d].base.diffuseIntensity", i);
		uniformPointLight[i].uniformDiffuseIntensity = glGetUniformLocation(shaderID, locBuff);

		// Posición de la luz dentro de la escena.
		snprintf(locBuff, sizeof(locBuff), "pointLights[%d].position", i);
		uniformPointLight[i].uniformPosition = glGetUniformLocation(shaderID, locBuff);

		// Parámetros que controlan cómo disminuye la intensidad con la distancia.
		snprintf(locBuff, sizeof(locBuff), "pointLights[%d].constant", i);
		uniformPointLight[i].uniformConstant = glGetUniformLocation(shaderID, locBuff);

		snprintf(locBuff, sizeof(locBuff), "pointLights[%d].linear", i);
		uniformPointLight[i].uniformLinear = glGetUniformLocation(shaderID, locBuff);

		snprintf(locBuff, sizeof(locBuff), "pointLights[%d].exponent", i);
		uniformPointLight[i].uniformExponent = glGetUniformLocation(shaderID, locBuff);
	}

	// Obtenemos la variable que indica cuántos reflectores están activos.
	uniformSpotLightCount = glGetUniformLocation(shaderID, "spotLightCount");

	// Recorremos el arreglo de reflectores para obtener las ubicaciones
	// de sus propiedades dentro del shader.
	for (size_t i = 0; i < MAX_SPOT_LIGHTS; i++)
	{
		char locBuff[100] = { '\0' };

		// Color e intensidades de la luz.
		snprintf(locBuff, sizeof(locBuff), "spotLights[%d].base.base.color", i);
		uniformSpotLight[i].uniformColour = glGetUniformLocation(shaderID, locBuff);

		snprintf(locBuff, sizeof(locBuff), "spotLights[%d].base.base.ambientIntensity", i);
		uniformSpotLight[i].uniformAmbientIntensity = glGetUniformLocation(shaderID, locBuff);

		snprintf(locBuff, sizeof(locBuff), "spotLights[%d].base.base.diffuseIntensity", i);
		uniformSpotLight[i].uniformDiffuseIntensity = glGetUniformLocation(shaderID, locBuff);

		// Posición desde donde se emite la luz.
		snprintf(locBuff, sizeof(locBuff), "spotLights[%d].base.position", i);
		uniformSpotLight[i].uniformPosition = glGetUniformLocation(shaderID, locBuff);

		// Parámetros de atenuación para controlar la intensidad según la distancia.
		snprintf(locBuff, sizeof(locBuff), "spotLights[%d].base.constant", i);
		uniformSpotLight[i].uniformConstant = glGetUniformLocation(shaderID, locBuff);

		snprintf(locBuff, sizeof(locBuff), "spotLights[%d].base.linear", i);
		uniformSpotLight[i].uniformLinear = glGetUniformLocation(shaderID, locBuff);

		snprintf(locBuff, sizeof(locBuff), "spotLights[%d].base.exponent", i);
		uniformSpotLight[i].uniformExponent = glGetUniformLocation(shaderID, locBuff);

		// Dirección hacia donde apunta el reflector.
		snprintf(locBuff, sizeof(locBuff), "spotLights[%d].direction", i);
		uniformSpotLight[i].uniformDirection = glGetUniformLocation(shaderID, locBuff);

		// Ángulo de apertura del haz de luz.
		snprintf(locBuff, sizeof(locBuff), "spotLights[%d].edge", i);
		uniformSpotLight[i].uniformEdge = glGetUniformLocation(shaderID, locBuff);
	}

}

// Estos métodos permiten consultar las ubicaciones de las variables uniform
// desde otras partes del programa.

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

GLuint Shader::GetAmbientcolorLocation()
{
	return uniformDirectionalLight.uniformcolor;
}

GLuint Shader::GetAmbientIntensityLocation()
{
	return uniformDirectionalLight.uniformAmbientIntensity;
}

GLuint Shader::GetDiffuseIntensityLocation()
{
	return uniformDirectionalLight.uniformDiffuseIntensity;
}

GLuint Shader::GetDirectionLocation()
{
	return uniformDirectionalLight.uniformDirection;
}

GLuint Shader::GetSpecularIntensityLocation()
{
	return uniformSpecularIntensity;
}

GLuint Shader::GetShininessLocation()
{
	return uniformShininess;
}

GLuint Shader::GetEyePositionLocation()
{
	return uniformEyePosition;
}

// Devuelve la ubicación de la variable uniform que controla el color.
GLuint Shader::getColorLocation()
{
	return uniformColor;
}

// Devuelve la ubicación utilizada para modificar el desplazamiento de la textura.
GLuint Shader::getOffsetLocation()
{
	return uniformTextureOffset;
}

// Devuelve la ubicación de la variable que controla la transparencia.
GLuint Shader::getAlphaLocation()
{
	return uniformAlpha;
}

// Devuelve la ubicación de la variable que permite desactivar la iluminación.
GLuint Shader::getNoLightingLocation()
{
	return uniformNoLighting;
}

// Permite obtener la ubicación de una variable uniform usando su nombre.
GLuint Shader::GetUniformLocation(const char* name)
{
	return glGetUniformLocation(shaderID, name);
}

// Envía las propiedades de la luz direccional al shader.
// La clase DirectionalLight se encarga de proporcionar sus valores.
void Shader::SetDirectionalLight(DirectionalLight * dLight)
{
	dLight->UseLight(uniformDirectionalLight.uniformAmbientIntensity, uniformDirectionalLight.uniformcolor,
		uniformDirectionalLight.uniformDiffuseIntensity, uniformDirectionalLight.uniformDirection);
}

// Configura las luces puntuales y envía sus propiedades al shader.
void Shader::SetPointLights(PointLight * pLight, unsigned int lightCount)
{
	// Limitamos la cantidad de luces al máximo permitido.
	if (lightCount > MAX_POINT_LIGHTS) lightCount = MAX_POINT_LIGHTS;

	// Indicamos al shader cuántas luces puntuales se van a utilizar.
	glUniform1i(uniformPointLightCount, lightCount);

	// Recorremos las luces y enviamos sus propiedades para calcular la iluminación.
	for (size_t i = 0; i < lightCount; i++)
	{
		pLight[i].UseLight(uniformPointLight[i].uniformAmbientIntensity, uniformPointLight[i].uniformcolor,
			uniformPointLight[i].uniformDiffuseIntensity, uniformPointLight[i].uniformPosition,
			uniformPointLight[i].uniformConstant, uniformPointLight[i].uniformLinear, uniformPointLight[i].uniformExponent);
	}
}

// Configura las luces tipo reflector y envía sus propiedades al shader.
void Shader::SetSpotLights(SpotLight * sLight, unsigned int lightCount)
{
	// Limitamos la cantidad de reflectores al máximo permitido.
	if (lightCount > MAX_SPOT_LIGHTS) lightCount = MAX_SPOT_LIGHTS;

	// Indicamos al shader cuántos reflectores se van a utilizar.
	glUniform1i(uniformSpotLightCount, lightCount);

	// Recorremos los reflectores y enviamos sus propiedades de iluminación.
	for (size_t i = 0; i < lightCount; i++)
	{
		sLight[i].UseLight(uniformSpotLight[i].uniformAmbientIntensity, uniformSpotLight[i].uniformColour,
			uniformSpotLight[i].uniformDiffuseIntensity, uniformSpotLight[i].uniformPosition, uniformSpotLight[i].uniformDirection,
			uniformSpotLight[i].uniformConstant, uniformSpotLight[i].uniformLinear, uniformSpotLight[i].uniformExponent,
			uniformSpotLight[i].uniformEdge);
	}
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
	uniformTextureOffset = 0;
}


// Crea y compila un shader individual, ya sea de vértices o de fragmentos.
void Shader::AddShader(GLuint theProgram, const char* shaderCode, GLenum shaderType)
{
	// Creamos el shader según el tipo que se haya indicado.
	GLuint theShader = glCreateShader(shaderType);

	// Preparamos el código fuente y su longitud para enviarlos a OpenGL.
	const GLchar* theCode[1];
	theCode[0] = shaderCode;

	GLint codeLength[1];
	codeLength[0] = strlen(shaderCode);

	// Enviamos el código fuente e iniciamos la compilación.
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
