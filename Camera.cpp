#include "Camera.h"

Camera::Camera() {}
// configura posición inicial, orientación y velocidades
Camera::Camera(glm::vec3 startPosition, glm::vec3 startUp, GLfloat startYaw, GLfloat startPitch, GLfloat startMoveSpeed, GLfloat startTurnSpeed)
{
	position = startPosition;
	worldUp = startUp;
	yaw = startYaw;
	pitch = startPitch;
	front = glm::vec3(0.0f, 0.0f, -1.0f);

	moveSpeed = startMoveSpeed;
	turnSpeed = startTurnSpeed;
	// Calcula los vectores iniciales (front, right, up)
	update();
}

void Camera::keyControl(bool* keys, GLfloat deltaTime)
{
	GLfloat velocity = moveSpeed * deltaTime;
	// Movimiento hacia adelante
	if (keys[GLFW_KEY_W])
	{
		position += front * velocity;
	}
	// Movimiento hacia atrás
	if (keys[GLFW_KEY_S])
	{
		position -= front * velocity;
	}
	// Desplazamiento lateral hacia la izquierda
	if (keys[GLFW_KEY_A])
	{
		position -= right * velocity;
	}
	// Desplazamiento lateral hacia la derecha
	if (keys[GLFW_KEY_D])
	{
		position += right * velocity;
	}
}
// Procesa el movimiento del ratón para modificar la orientación de la cámara
void Camera::mouseControl(GLfloat xChange, GLfloat yChange)
{
	xChange *= turnSpeed;
	yChange *= turnSpeed;

	yaw += xChange;
	pitch += yChange;

	if (pitch > 89.0f)
	{
		pitch = 89.0f;
	}

	if (pitch < -89.0f)
	{
		pitch = -89.0f;
	}

	update();
}
// Genera y retorna la matriz de vista (View Matrix) para la transformación de coordenadas
glm::mat4 Camera::calculateViewMatrix()
{
	return glm::lookAt(position, position + front, up);
}
// Retorna la posición actual de la cámara en el espacio del mundo
glm::vec3 Camera::getCameraPosition()
{
	return position;
}

// Retorna el vector de dirección frontal normalizado
glm::vec3 Camera::getCameraDirection()
{
	return glm::normalize(front);
}

void Camera::update()
{
	front.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
	front.y = sin(glm::radians(pitch));
	front.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
	front = glm::normalize(front);

	right = glm::normalize(glm::cross(front, worldUp));
	up = glm::normalize(glm::cross(right, front));
}


Camera::~Camera()
{
}
