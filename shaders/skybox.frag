#version 330
in vec3 TexCoords;
out vec4 color;
uniform samplerCube skybox;
uniform vec3 skyTint;
void main()
{
    color = texture(skybox, TexCoords) * vec4(skyTint, 1.0);
}
