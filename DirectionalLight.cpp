#include "DirectionalLight.h"

DirectionalLight::DirectionalLight() : Light()
{
	direction = glm::vec3(0.0f, -1.0f, 0.0f);
}

DirectionalLight::DirectionalLight(GLfloat red, GLfloat green, GLfloat blue,
									GLfloat aIntensity, GLfloat dIntensity,
									GLfloat xDir, GLfloat yDir, GLfloat zDir) : Light(red, green, blue, aIntensity, dIntensity)
{
	direction = glm::vec3(xDir, yDir, zDir);
}
// Transfiere las propiedades de la luz direccional a las variables uniformes del Shader activo
void DirectionalLight::UseLight(GLfloat ambientIntensityLocation, GLfloat ambientcolorLocation,
	GLfloat diffuseIntensityLocation, GLfloat directionLocation)
{
	// Envía el color RGB ambiental al shader
	glUniform3f(ambientcolorLocation, color.x, color.y, color.z);
	// Envía el factor de intensidad ambiental al shader
	glUniform1f(ambientIntensityLocation, ambientIntensity);

	// Envía el vector de dirección de la luz al shader
	glUniform3f(directionLocation, direction.x, direction.y, direction.z);
	// Envía el factor de intensidad difusa al shader
	glUniform1f(diffuseIntensityLocation, diffuseIntensity);
}

DirectionalLight::~DirectionalLight()
{
}
