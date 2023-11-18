#version 330 core

layout (location = 0) in vec3 Position;
layout (location = 1) in vec3 Normal;

// Transformation matrix.
uniform mat4 worldMatrix;
uniform mat4 viewMatrix;
uniform mat4 normalMatrix;
uniform mat4 MVP;

// Material properties.
uniform vec3 Ka;
uniform vec3 Kd;
uniform vec3 Ks;
uniform float Ns;
// Light data.
uniform vec3 dirLightDir;
uniform vec3 dirLightRadiance;
uniform vec3 pointLightPos;
uniform vec3 pointLightIntensity;
uniform vec3 ambientLight;

// Data pass to fragment shader.
out vec3 iPosition;
out vec3 iNormal;
out vec3 iKa;
out vec3 iKd;
out vec3 iKs;
out float iNs;
out vec3 iDirLightDir;
out vec3 iDirLightRadiance;
out vec3 iPointLightPos;
out vec3 iPointLightIntensity;
out vec3 iAmbientLight;

void main()
{
    vec4 tmpPosition = viewMatrix * worldMatrix * vec4(Position, 1.0);
    iPosition = tmpPosition.xyz / tmpPosition.w;
    iNormal = normalize(vec3(normalMatrix * vec4(Normal, 0.0)));

    iKa = Ka;
    iKd = Kd;
    iKs = Ks;
    iNs = Ns;
    iDirLightDir = vec3(viewMatrix * vec4(dirLightDir, 0.0));
    iDirLightDir = normalize(iDirLightDir);
    iDirLightRadiance = dirLightRadiance;
    iPointLightPos = vec3(viewMatrix * vec4(pointLightPos, 1.0));
    vec3 dist = iPointLightPos - iPosition;
    float distSqr = dot(dist, dist);
    iPointLightIntensity = pointLightIntensity / distSqr;
    iAmbientLight = ambientLight;

    gl_Position = MVP * vec4(Position, 1.0);
}