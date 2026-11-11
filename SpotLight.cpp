
#include "SpotLight.h"


// Constructor por defecto. Hereda las propiedades de PointLight
// y establece una dirección inicial hacia abajo.
SpotLight::SpotLight() : PointLight()
{
	direction = glm::vec3(0.0f, -1.0f, 0.0f);
	edge = 0.0f;

	// Convertimos el ángulo a radianes y calculamos su coseno,
	// que se utilizará para delimitar el área iluminada.
	procEdge = cosf(glm::radians(edge));
}


// Constructor que permite configurar el color, intensidad, posición,
// dirección, atenuación y ángulo de apertura del reflector.
SpotLight::SpotLight(GLfloat red, GLfloat green, GLfloat blue, 
	GLfloat aIntensity, GLfloat dIntensity, 
	GLfloat xPos, GLfloat yPos, GLfloat zPos, 
	GLfloat xDir, GLfloat yDir, GLfloat zDir, 
	GLfloat con, GLfloat lin, GLfloat exp, 
	GLfloat edg) : PointLight(red, green, blue, aIntensity, dIntensity, xPos, yPos, zPos, con, lin, exp)
{
	// Normalizamos la dirección para trabajar con un vector unitario.
	direction = glm::normalize(glm::vec3(xDir, yDir, zDir));

	// Guardamos el ángulo de apertura y calculamos su coseno
	// para enviarlo posteriormente al shader.
	edge = edg;
	procEdge = cosf(glm::radians(edge));
}


// Envía al shader los valores necesarios para calcular
// la iluminación producida por este reflector.
void SpotLight::UseLight(GLuint ambientIntensityLocation, GLuint ambientColourLocation, 
	GLuint diffuseIntensityLocation, GLuint positionLocation, GLuint directionLocation, 
	GLuint constantLocation, GLuint linearLocation, GLuint exponentLocation, 
	GLuint edgeLocation)
{
	// Enviamos el color y las intensidades ambiental y difusa.
	glUniform3f(ambientColourLocation, color.x, color.y, color.z);
	glUniform1f(ambientIntensityLocation, ambientIntensity);
	glUniform1f(diffuseIntensityLocation, diffuseIntensity);

	// Indicamos dónde se encuentra la luz y sus factores de atenuación.
	// Estos factores controlan cómo disminuye la iluminación con la distancia.
	glUniform3f(positionLocation, position.x, position.y, position.z);
	glUniform1f(constantLocation, constant);
	glUniform1f(linearLocation, linear);
	glUniform1f(exponentLocation, exponent);

	// Enviamos la dirección del reflector y el límite de su cono de luz.
	glUniform3f(directionLocation, direction.x, direction.y, direction.z);
	glUniform1f(edgeLocation, procEdge);
}


// Actualiza al mismo tiempo la posición y dirección de la luz.
// Puede utilizarse cuando el reflector debe seguir un objeto o una cámara.
void SpotLight::SetFlash(glm::vec3 pos, glm::vec3 dir)
{
	position = pos;
	direction = dir;
}


// Permite cambiar únicamente la posición de la luz,
// manteniendo los demás valores de su configuración.
void SpotLight::SetPos(glm::vec3 pos)
{
	position = pos;
}


// Destructor de la clase SpotLight.
SpotLight::~SpotLight()
{
}
