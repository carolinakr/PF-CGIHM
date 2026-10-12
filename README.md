# 🏮 Gion Matsuri: noches de Yoiyama
 
Recorrido virtual interactivo en 3D por una calle de Kioto durante el **Yoiyama**, las noches previas al desfile de carros ceremoniales del **Gion Matsuri**. Desarrollado en **C++ y OpenGL** como proyecto final de *Computación Gráfica e Interacción Humano-Computadora*.
 
> **Estado:** en desarrollo · Entrega final: 10 de noviembre de 2026
 
---
 
## ¿Cómo correr el repositorio de GitHub?
 
Este manual sirve para configurar el proyecto base y poder utilizar **OpenGL 3.3+, GLFW, stb_image y Assimp** en Visual Studio. Después se copia el contenido de este repositorio dentro de ese proyecto.
 
### Parte 1. Crear el proyecto en Visual Studio Community
 
1. Instalar el IDE **Visual Studio Community** ([descarga](https://visualstudio.microsoft.com/es/downloads/)) y seleccionar la carga de trabajo **Desarrollo para escritorio con C++**.
2. Crear un **Proyecto vacío** de C++.
3. Cambiar la ubicación por defecto a la carpeta donde se quieran guardar los proyectos.
4. En la barra superior del IDE, cambiar las opciones a **Debug** y **x64**.
5. Abrir la ubicación de la carpeta del proyecto (clic derecho sobre el proyecto → *Abrir carpeta en el Explorador de archivos*).
6. Extraer ahí los archivos del ZIP del proyecto base: la carpeta de **OpenGL 3.3** y los archivos **main**.
7. En el *Explorador de soluciones*, agregar los archivos de encabezado (**Header Files**) y de origen (**Source Files**) con clic derecho → *Agregar → Elemento existente*.
8. Abrir **Propiedades de configuración** del proyecto, elegir **Todas las configuraciones** y plataforma **x64**. En **C/C++ → General → Directorios de inclusión adicionales** agregar:
```
   include;glm;
```
9. En **Vinculador (Linker) → General → Directorios de bibliotecas adicionales** agregar:
```
   lib;
```
10. En **Vinculador (Linker) → Entrada (Input) → Dependencias adicionales** agregar:
```
    glew32.lib;glfw3.lib;opengl32.lib;assimp-vc143-mt.lib;
```
    ⚠️ **No eliminar** las opciones que ya existen.
 
### Parte 2. Agregar el código del repositorio
 
11. Clonar o descargar este repositorio:
```bash
    git clone https://github.com/USUARIO/gion-matsuri-yoiyama.git
```
    Sin Git: botón verde **Code → Download ZIP**.
12. **Copiar todo el contenido del repositorio dentro de la carpeta del proyecto local** (la misma del paso 5) y reemplazar los archivos si lo pide.
13. Agregar al proyecto los archivos nuevos de `src/` como en el paso 7.
14. Compilar con `Ctrl + Shift + B` y ejecutar con `F5`.
> Si al ejecutar aparece un error de que falta una `.dll` (por ejemplo `glew32.dll` o la de Assimp), cópiala de la carpeta del proyecto base a la carpeta donde está el `.exe` (`x64/Debug/`).
 
---
 
## Sobre el proyecto
 
El Gion Matsuri se celebra cada julio en Kioto y su ceremonia de carros Yamahoko forma parte del Patrimonio Cultural Inmaterial de la Humanidad (UNESCO). Este recorrido acerca la celebración a personas que no pueden asistir: el usuario camina por una calle peatonal iluminada por faroles, se acerca a los puntos de interés para leer su significado y participa en actividades del festival.
 
**En el recorrido puedes:**
- Caminar libremente en primera persona por la calle del festival.
- Leer cards informativas sobre cada elemento cultural.
- Activar el desfile del carro Yamahoko.
- Jugar kingyo-sukui (atrapar peces dorados).
- Lanzar fuegos artificiales.
---
 
## Controles
 
| Tecla / acción | Función |
|---|---|
| `W` `A` `S` `D` | Moverse |
| Mouse | Mirar alrededor |
| `E` | Abrir / cerrar la card informativa o interactuar con un punto |
| `F` | Lanzar fuegos artificiales |
| Clic izquierdo | Mover la red en el juego kingyo-sukui |
| `C` | Ver créditos y licencias |
| `H` | Mostrar ayuda |
| `Esc` | Salir |
 
---
 
## Puntos del recorrido
 
1. **Entrada (torii)**
2. **Puestos de comida (yatai)**
3. **Juego kingyo-sukui**
4. **Carro ceremonial Yamahoko**
5. **Casa tradicional (machiya)**
6. **Músicos ohayashi**
7. **Fuegos artificiales**
---
 
## Animaciones
 
| # | Animación | Técnica |
|---|---|---|
| 1 | Músico tocando el taiko | **KeyFrames** |
| 2 | Desfile del carro Yamahoko | Jerárquica |
| 3 | Balanceo y parpadeo de faroles | Procedural |
| 4 | Fuegos artificiales | Sistema de partículas |
| 5 | Peces del kingyo-sukui | Paramétrica |
 
---
 
## Tecnologías
 
| Área | Herramienta |
|---|---|
| Lenguaje y API gráfica | C++17 · OpenGL 3.3 Core |
| IDE | Visual Studio 2022 |
| Ventana y entrada | GLFW |
| Carga de OpenGL | GLEW |
| Matemáticas | GLM |
| Texturas | stb_image |
| Modelos 3D | Assimp |
| Audio | irrKlang |
| Interfaz | Dear ImGui |
| Modelado | Blender |
| Edición de imagen | GIMP |
| Diseño de cards | Canva |
| Gestión | GitHub Projects (Kanban) |
 
---
 
## Estructura del repositorio
 
```
gion-matsuri-yoiyama/
├── src/          Código fuente (.cpp / .h)
├── shaders/      Shaders GLSL (.vs / .fs)
├── models/       Modelos 3D (.obj / .fbx / .glb)
├── textures/     Texturas y skybox
├── audio/        Música y efectos de sonido
├── cards/        Cards informativas (PNG)
├── libs/         Bibliotecas externas
├── docs/         Manuales, referencias y evidencias
└── README.md
```
 
---
 
## Forma de trabajo
 
- Las actividades se gestionan como **issues** en el tablero Kanban del proyecto (*Por hacer → En curso → En revisión → Hecho*).
- Cada cambio se trabaja en una rama `feature/nombre-de-la-tarea` y se integra a `develop` mediante **pull request**.
- Los commits referencian su issue, por ejemplo: `Cámara en primera persona #5`.
- `main` contiene solo versiones estables.
---
 
## Equipo 14
 
| No. de cuenta |
|---|
| 316255314 |
| 317313143 |
| 319274233 |
 
**Universidad Nacional Autónoma de México · Facultad de Ingeniería**
Ingeniería en Computación · Semestre 2027-1
 
---
 
## Créditos y licencias
 
Los modelos, texturas, sonidos y bibliotecas de terceros se listan con su autor y licencia en [`docs/creditos.md`](docs/creditos.md) y dentro de la aplicación (tecla `C`).
 
## Referencias principales
 
- UNESCO. *Yama, Hoko, Yatai, float festivals in Japan.* https://ich.unesco.org/en/RL/yama-hoko-yatai-float-festivals-in-japan-02292
- Yasaka Jinja. *Gion Festival.* https://www.yasaka-jinja.or.jp/en/gion_fes/
- Art Research Center, Ritsumeikan University. *The Gion Festival Digital Museum 2021.* https://www.arc.ritsumei.ac.jp/lib/vm/gionfestivalDMe/A/
---
 
<sub>Proyecto académico sin fines de lucro.</sub>
 
