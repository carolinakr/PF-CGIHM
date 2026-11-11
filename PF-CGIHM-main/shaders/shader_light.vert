#version 330

layout (location = 0) in vec3 pos;
layout (location = 1) in vec2 tex;
layout (location = 2) in vec3 norm;

out vec4 vCol;
out vec2 TexCoord;
out vec3 Normal;
out vec3 FragPos;
out vec4 vColor;

uniform mat4 model;
uniform mat4 projection;
uniform mat4 view;
uniform vec3 color;
uniform vec2 toffset;
uniform float alphaValue;
// cola del pez: desplaza X en la zona de la cola, en espacio del modelo
uniform float swayAmp;
uniform vec2 swayZone;
uniform float swayPhase;
// ondas del agua: desplaza Y
uniform float waveAmp;
uniform float waveFreq;
uniform float waveTime;


void main()
{
	vec3 p = pos;
	if (swayAmp != 0.0)
	{
		float t = clamp((p.z - swayZone.x) / (swayZone.y - swayZone.x), 0.0, 1.0);
		p.x += swayAmp * sin(swayPhase) * t * t;
	}
	if (waveAmp != 0.0)
	{
		p.y += waveAmp * (sin(waveFreq * (p.x + p.z) + waveTime) + 0.7 * sin(waveFreq * 0.8 * (p.x - p.z) - 1.3 * waveTime));
	}
	gl_Position = projection * view * model * vec4(p, 1.0);
	vCol = vec4(0.0, 1.0, 0.0, 1.0f);
	vColor = vec4(color, alphaValue);
	TexCoord = tex + toffset;
	//para tomar las Transformaciones geometricas y la transpuesta es para que las escalas se inviertan en caso de escalas no uniformes en las normales
	Normal = mat3(transpose(inverse(model))) * norm;

	FragPos = (model * vec4(p, 1.0)).xyz; //para obtener un vec3 solo posicion con respecto al origen en orden xyz: Swizzling
}
