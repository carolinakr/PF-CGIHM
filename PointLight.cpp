#include "PointLight.h"


// Invoca el constructor base Light() e inicializa la posición en el origen y atenua sin caída de intensidad
PointLight::PointLight() : Light()
{
	position = glm::vec3(0.0f, 0.0f, 0.0f);
	constant = 1.0f;
	linear = 0.0f;
	exponent = 0.0f;
}
// Pasa color e intensidades al constructor base Light, y asigna posición y factores de atenuación
PointLight::PointLight(GLfloat red, GLfloat green, GLfloat blue, 
						GLfloat aIntensity, GLfloat dIntensity, 
						GLfloat xPos, GLfloat yPos, GLfloat zPos, 
						GLfloat con, GLfloat lin, GLfloat exp) : Light(red, green, blue, aIntensity, dIntensity)
{
	position = glm::vec3(xPos, yPos, zPos);
	constant = con;
	linear = lin;
	exponent = exp;
}
// Transfiere las propiedades de la luz puntual y sus coeficientes de atenuación a las variables uniformes del Shader activo
void PointLight::UseLight(GLfloat ambientIntensityLocation, GLfloat ambientcolorLocation, 
							GLfloat diffuseIntensityLocation, GLfloat positionLocation, 
							GLfloat constantLocation, GLfloat linearLocation, GLfloat exponentLocation)
{
	// Envía el color RGB ambiental al shader
	glUniform3f(ambientcolorLocation, color.x, color.y, color.z);
	// Envía el factor de intensidad ambiental al shader
	glUniform1f(ambientIntensityLocation, ambientIntensity);
	// Envía el factor de intensidad difusa al shader
	glUniform1f(diffuseIntensityLocation, diffuseIntensity);

	// Envía la posición 3D de la luz puntual en el espacio del mundo al shader
	glUniform3f(positionLocation, position.x, position.y, position.z);
	// Envía el coeficiente de atenuación constante (previene división por cero)
	glUniform1f(constantLocation, constant);
	// Envía el coeficiente de atenuación lineal
	glUniform1f(linearLocation, linear);
	// Envía el coeficiente de atenuación cuadrática/exponente
	glUniform1f(exponentLocation, exponent);
}

PointLight::~PointLight()
{
}
