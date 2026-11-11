#include "Material.h"


//asigna intensidad especular nula y brillo cero
Material::Material()
{
	specularIntensity = 0.0f;
	shininess = 0.0f;
}
//inicializa la intensidad de reflejo especular y el exponente de brillo 
Material::Material(GLfloat sIntensity, GLfloat shine)
{
	specularIntensity = sIntensity;
	shininess = shine;
}
// Transfiere las propiedades especulares del material a las variables uniformes del Shader activo en OpenGL
void Material::UseMaterial(GLuint specularIntensityLocation, GLuint shininessLocation)
{
	// Envía la intensidad del brillo especular al shader
	glUniform1f(specularIntensityLocation, specularIntensity);
	// Envía el factor de concentración del reflejo (exponente Phong/Blinn-Phong) al shader
	glUniform1f(shininessLocation, shininess);
}

Material::~Material()
{
}
