
#include "Window.h"

// Constructor por defecto: inicializa las propiedades de la ventana y los controles.
Window::Window()
{
	mainWindow = nullptr;
	mouseFirstMoved = true;
	xChange = 0.0f;
	yChange = 0.0f;
	lastX = 0.0f;
	lastY = 0.0f;
	width = 800;
	height = 600;

	// Inicializamos el arreglo de teclas para que ninguna aparezca presionada.
	for (size_t i = 0; i < 1024; i++)
	{
		keys[i] = 0;
	}
}


// Constructor que permite establecer el ancho y alto de la ventana.
Window::Window(GLint windowWidth, GLint windowHeight)
{
	width = windowWidth;
	mainWindow = nullptr;
	mouseFirstMoved = true;
	xChange = 0.0f;
	yChange = 0.0f;
	lastX = 0.0f;
	lastY = 0.0f;
	height = windowHeight;

	// Valores iniciales de posición utilizados para controlar elementos de la escena.
	muevex = 2.0f;
	muevez = -3.0f;
	muevehz = 6.0f;
	muevepez = 0.0f;

	// Dejamos todas las teclas en estado de reposo.
	for (size_t i = 0; i < 1024; i++)
	{
		keys[i] = 0;
	}
}


// Inicializa GLFW, crea la ventana y configura el contexto de OpenGL.
int Window::Initialise()
{
	//Inicializaci�n de GLFW
	if (!glfwInit())
	{
		printf("Fall� inicializar GLFW");
		glfwTerminate();
		return 1;
	}

	//Asignando variables de GLFW y propiedades de ventana
	// Solicitamos una versión de OpenGL 4.3.
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);

	//para solo usar el core profile de OpenGL y no tener retrocompatibilidad
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);

	// Activamos antialiasing de 4 muestras para suavizar los bordes.
	glfwWindowHint(GLFW_SAMPLES, 4); // antialiasing 4x (MSAA)

	//CREAR VENTANA
	// Creamos la ventana con el título del proyecto.
	mainWindow = glfwCreateWindow(width, height, "Gion Matsuri - Noches de Yoiyama", NULL, NULL);

	// Comprobamos que la ventana se haya creado correctamente.
	if (!mainWindow)
	{
		printf("Fallo en crearse la ventana con GLFW");
		glfwTerminate();
		return 1;
	}

	//Obtener tama�o de Buffer
	// Consultamos las dimensiones reales del framebuffer.
	glfwGetFramebufferSize(mainWindow, &bufferWidth, &bufferHeight);

	//asignar el contexto
	// Establecemos la ventana como contexto actual de OpenGL.
	glfwMakeContextCurrent(mainWindow);

	//MANEJAR TECLADO y MOUSE
	// Registramos las funciones que recibirán los eventos de entrada.
	createCallbacks();


	//permitir nuevas extensiones
	// Habilitamos el uso de las extensiones de GLEW.
	glewExperimental = GL_TRUE;

	// Inicializamos GLEW para poder utilizar las funciones modernas de OpenGL.
	if (glewInit() != GLEW_OK)
	{
		printf("Fall� inicializaci�n de GLEW");
		glfwDestroyWindow(mainWindow);
		glfwTerminate();
		return 1;
	}

	// Activamos el antialiasing y la prueba de profundidad.
	glEnable(GL_MULTISAMPLE);
	glEnable(GL_DEPTH_TEST); //HABILITAR BUFFER DE PROFUNDIDAD
							 // Asignar valores de la ventana y coordenadas
							 
							 //Asignar Viewport
	// Definimos el área de dibujo de OpenGL según el tamaño del framebuffer.
	glViewport(0, 0, bufferWidth, bufferHeight);

	//Callback para detectar que se est� usando la ventana
	// Asociamos este objeto Window con la ventana de GLFW para recuperarlo en los callbacks.
	glfwSetWindowUserPointer(mainWindow, this);

	//bloquear el cursor dentro de la ventana
	// Ocultamos y restringimos el cursor para permitir controlar la cámara con el mouse.
	glfwSetInputMode(mainWindow, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
	return 0;
}


// Registra las funciones que atenderán los eventos del teclado y del mouse.
void Window::createCallbacks()
{
	glfwSetKeyCallback(mainWindow, ManejaTeclado);
	glfwSetCursorPosCallback(mainWindow, ManejaMouse);
}


// Devuelve el desplazamiento horizontal del mouse y reinicia su valor.
GLfloat Window::getXChange()
{
	GLfloat theChange = xChange;
	xChange = 0.0f;
	return theChange;
}


// Devuelve el desplazamiento vertical del mouse y reinicia su valor.
GLfloat Window::getYChange()
{
	GLfloat theChange = yChange;
	yChange = 0.0f;
	return theChange;
}




// Callback que procesa las teclas presionadas y liberadas.
void Window::ManejaTeclado(GLFWwindow* window, int key, int code, int action, int mode)
{
	// Recuperamos el objeto Window asociado con la ventana que generó el evento.
	Window* theWindow = static_cast<Window*>(glfwGetWindowUserPointer(window));

	// Si se presiona ESC y está habilitada la opción de cierre, se solicita cerrar la ventana.
	if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS && theWindow->escapeCloses)
	{
		glfwSetWindowShouldClose(window, GL_TRUE);
	}

	// Tecla Y: incrementa la posición horizontal controlada por muevex.
	if (key == GLFW_KEY_Y)
	{
		theWindow->muevex += 1.0;
	}

	// Tecla U: disminuye la posición horizontal controlada por muevex.
	if (key == GLFW_KEY_U)
	{
		theWindow->muevex -= 1.0;
	}

	// Tecla 1: incrementa el valor de posición almacenado en muevez.
	if (key == GLFW_KEY_1)
	{
		theWindow->muevez += 1.0;
	}

	// Tecla 2: disminuye el valor de posición almacenado en muevez.
	if (key == GLFW_KEY_2)
	{
		theWindow->muevez -= 1.0;
	}

	// Movimiento del helicoptero: I = +X, K = -X (adelante)
	// Las teclas I y K modifican la posición asociada al movimiento del helicóptero.
	if (key == GLFW_KEY_I)
	{
		theWindow->muevehz += 1.0;
	}

	// Tecla K: modifica la posición del helicóptero en sentido contrario.
	if (key == GLFW_KEY_K)
	{
		theWindow->muevehz -= 1.0;
	}

	// Movimiento diagonal del pez abisal: O = sube diagonal, P = baja diagonal
	// Las teclas O y P modifican el valor utilizado para desplazar al pez.
	if (key == GLFW_KEY_O)
	{
		theWindow->muevepez += 0.5;
	}

	// Tecla P: modifica el desplazamiento del pez en sentido contrario.
	if (key == GLFW_KEY_P)
	{
		theWindow->muevepez -= 0.5;
	}



	// Comprobamos que el código de la tecla se encuentre dentro del arreglo.
	if (key >= 0 && key < 1024)
	{
		// Registramos cuando una tecla es presionada.
		if (action == GLFW_PRESS)
		{
			theWindow->keys[key] = true;
			//printf("se presiono la tecla %d'\n", key);
		}
		// Registramos cuando una tecla es liberada.
		else if (action == GLFW_RELEASE)
		{
			theWindow->keys[key] = false;
			//printf("se solto la tecla %d'\n", key);
		}
	}
}


// Callback que recibe la posición del cursor cada vez que se mueve el mouse.
void Window::ManejaMouse(GLFWwindow* window, double xPos, double yPos)
{
	// Recuperamos el objeto Window asociado con la ventana.
	Window* theWindow = static_cast<Window*>(glfwGetWindowUserPointer(window));

	// En el primer movimiento guardamos la posición inicial del cursor.
	// Esto evita que se genere un desplazamiento brusco al comenzar.
	if (theWindow->mouseFirstMoved)
	{
		theWindow->lastX = xPos;
		theWindow->lastY = yPos;
		theWindow->mouseFirstMoved = false;
	}

	// Calculamos cuánto se desplazó el mouse en cada eje.
	theWindow->xChange = xPos - theWindow->lastX;
	theWindow->yChange = theWindow->lastY - yPos;

	// Actualizamos la última posición registrada para el siguiente movimiento.
	theWindow->lastX = xPos;
	theWindow->lastY = yPos;
}


// Destructor: destruye la ventana y finaliza GLFW.
Window::~Window()
{
	glfwDestroyWindow(mainWindow);
	glfwTerminate();

}
