
/*
* Sphere.cpp
*Basado en el código creado por
*  Created on: 06/09/2016
*      Author: reynaldo Martell
*/
#include "Sphere.h"


// Constructor: recibe el radio de la esfera y la cantidad de divisiones
// horizontales y verticales que se utilizarán para construirla.
Sphere::Sphere(float ratio, int slices, int stacks) :
	ratio(ratio), slices(slices), stacks(stacks), VAO(0), VBO(0), EBO(0) {
}


// Destructor: libera los recursos de OpenGL utilizados por la esfera.
Sphere::~Sphere() {

	// Deshabilitamos los atributos de vértice utilizados.
	glDisableVertexAttribArray(0);
	glDisableVertexAttribArray(1);
	glDisableVertexAttribArray(2);

	// Desvinculamos y eliminamos el búfer que almacena los vértices.
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glDeleteBuffers(1, &VBO);

	// Eliminamos el búfer que almacena los índices de los triángulos.
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
	glDeleteBuffers(1, &EBO);

	// Desvinculamos y eliminamos el Vertex Array Object.
	glBindVertexArray(0);
	glDeleteVertexArrays(1, &VAO);

}



// Genera los vértices, colores e índices necesarios para construir la esfera.
void Sphere::init() {
		// Reservamos espacio para todos los vértices de la esfera.
		vertexC.resize(((slices + 1) * (stacks + 1)));

	// Reservamos espacio para los índices que definirán los triángulos.
	index.resize((slices * stacks + slices) * 6);

	// Recorremos las divisiones verticales de la esfera.
	for (int i = 0; i <= stacks; ++i) {
		// Convertimos la posición del recorrido en un valor entre 0 y 1.
		float V = i / (float)stacks;

		// Calculamos el ángulo vertical que va desde un polo hasta el otro.
		float phi = V * M_PI;

		// Recorremos las divisiones horizontales de cada nivel.
		for (int j = 0; j <= slices; ++j) {
			// Convertimos la posición horizontal en un valor entre 0 y 1.
			float U = j / (float)slices;

			// Calculamos el ángulo horizontal alrededor de la esfera.
			float theta = U * M_PI * 2.0;

			// Obtenemos las coordenadas X, Y y Z mediante coordenadas esféricas.
			float X = cos(theta) * sin(phi);
			float Y = cos(phi);
			float Z = sin(theta) * sin(phi);

				// Asignamos la posición del vértice y aplicamos el radio de la esfera.
				vertexC[i * (slices + 1) + j].position = ratio
					* glm::vec3(X, Y, Z);

				// Asignamos un color aleatorio a cada vértice.
				vertexC[i * (slices + 1) + j].color = glm::sphericalRand(1.0);
		}
	}

	// Construimos los índices que conectan los vértices para formar triángulos.
	for (int i = 0; i < slices * stacks + slices; ++i) {
		// Cada grupo de seis índices representa dos triángulos.
		index[i * 6] = i;
		index[i * 6 + 1] = i + slices + 1;
		index[i * 6 + 2] = i + slices;
		index[i * 6 + 3] = i + slices + 1;
		index[i * 6 + 4] = i;
		index[i * 6 + 5] = i + 1;
	}
}


// Prepara los búferes y configura cómo OpenGL interpretará los datos de la esfera.
void Sphere::load() {

	// Generamos el VAO, el VBO y el EBO.
	glGenVertexArrays(1, &VAO);
	glGenBuffers(1, &VBO);
	glGenBuffers(1, &EBO);

	// Activamos el VAO para configurar los atributos de la esfera.
	glBindVertexArray(VAO);

	// Vinculamos el VBO, que almacenará la información de los vértices.
	glBindBuffer(GL_ARRAY_BUFFER, VBO);

	// stride indica el tamaño total de cada elemento de vértice.
	size_t stride;

	// Definimos los desplazamientos de los atributos dentro de cada vértice.
	size_t offset1 = 0;
	size_t offset2 = 0;
	size_t offset3 = 0;

		// Copiamos los datos de los vértices al búfer de OpenGL.
		glBufferData(GL_ARRAY_BUFFER, vertexC.size() * sizeof(glm::vec3) * 2,
			vertexC.data(),
			GL_STATIC_DRAW);

		// Obtenemos el tamaño de cada elemento de vertexC.
		stride = sizeof(vertexC[0]);

		// El primer atributo comienza desde el inicio del elemento.
		offset1 = 0;

	// Vinculamos el EBO y copiamos los índices de los triángulos.
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, index.size() * sizeof(GLuint),
		index.data(),
		GL_STATIC_DRAW);

	// First attribute
	// Configuramos el atributo 0, correspondiente a la posición de los vértices.
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (GLvoid*)offset1);
	glEnableVertexAttribArray(0);

	// Second attribute
	// Configuramos el atributo 1, utilizado para otro dato de cada vértice.
		glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride,
		(GLvoid*)offset2);
	glEnableVertexAttribArray(1);

	// Thrid attribute
	// Configuramos el atributo 2 para que también esté disponible en el shader.
	glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, stride, (GLvoid*)offset3);
	glEnableVertexAttribArray(2);

	// Desvinculamos el VAO una vez terminada su configuración.
	glBindVertexArray(0); // Unbind VAO

}


// Dibuja la esfera utilizando los índices almacenados en el EBO.
void Sphere::render() {

	// Activamos el VAO que contiene la configuración de la esfera.
	glBindVertexArray(VAO);

	// Dibujamos los triángulos utilizando los índices definidos en init().
	glDrawElements( GL_TRIANGLES, index.size(), GL_UNSIGNED_INT,
			(GLvoid*) (sizeof(GLuint) * 0));

	// Desactivamos el VAO al terminar el dibujo.
	glBindVertexArray(0);

}
