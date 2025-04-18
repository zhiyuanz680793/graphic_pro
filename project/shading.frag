#version 420

// required by GLSL spec Sect 4.5.3 (though nvidia does not, amd does)
precision highp float;

///////////////////////////////////////////////////////////////////////////////
// Material
///////////////////////////////////////////////////////////////////////////////
uniform vec3 material_color = vec3(1, 1, 1);
uniform float material_metalness = 0;
uniform float material_fresnel = 0;
uniform float material_shininess = 0;
uniform vec3 material_emission = vec3(0);

uniform int has_color_texture = 0;
layout(binding = 0) uniform sampler2D colorMap;
uniform int has_emission_texture = 0;
layout(binding = 5) uniform sampler2D emissiveMap;

///////////////////////////////////////////////////////////////////////////////
// Environment
///////////////////////////////////////////////////////////////////////////////
layout(binding = 6) uniform sampler2D environmentMap;
layout(binding = 7) uniform sampler2D irradianceMap;
layout(binding = 8) uniform sampler2D reflectionMap;
uniform float environment_multiplier;

///////////////////////////////////////////////////////////////////////////////
// Light source
///////////////////////////////////////////////////////////////////////////////
uniform vec3 point_light_color = vec3(1.0, 1.0, 1.0);
uniform float point_light_intensity_multiplier = 50.0;

///////////////////////////////////////////////////////////////////////////////
// Constants
///////////////////////////////////////////////////////////////////////////////
#define PI 3.14159265359

///////////////////////////////////////////////////////////////////////////////
// Input varyings from vertex shader
///////////////////////////////////////////////////////////////////////////////
in vec2 texCoord;
in vec3 viewSpaceNormal;
in vec3 viewSpacePosition;

///////////////////////////////////////////////////////////////////////////////
// Input uniform variables
///////////////////////////////////////////////////////////////////////////////
uniform mat4 viewInverse;
uniform vec3 viewSpaceLightPosition;

///////////////////////////////////////////////////////////////////////////////
// Output color
///////////////////////////////////////////////////////////////////////////////
layout(location = 0) out vec4 fragmentColor;


in vec4 shadowMapCoord;
layout(binding = 10) uniform sampler2DShadow shadowMapTex;

uniform vec3 viewSpaceLightDir;
uniform float spotOuterAngle;
uniform float spotInnerAngle;


//uniform bool useSpotlight;
//uniform bool useSoftFalloff;


vec3 calculateDirectIllumiunation(vec3 wo, vec3 n, vec3 base_color)
{
	vec3 direct_illum = base_color;

	// Calculate the radiance Li from the light, and the direction to the light. 
	// If the light is backfacing the triangle, return vec3(0);        
	float d = length(viewSpacePosition - viewSpaceLightPosition);
	vec3 li = point_light_intensity_multiplier * point_light_color * (1/pow(d, 2));
	vec3 wi = normalize(viewSpaceLightPosition - viewSpacePosition);
	vec3 wh = normalize(wi + wo);

	if(dot(n, wi) <= 0)
		return vec3(0);

	// fix the pink dots (div by zero)
	if(dot(n, wh) < 0)
		return vec3(0);

	// Calculate the diffuse term and return that as the result
	vec3 diffuse_term = base_color * (1.0/PI) * length(dot(n, wi)) * li;

	// Calculate the Torrance Sparrow BRDF and return the light reflected from that instead
	float F = material_fresnel + (1 - material_fresnel) * pow(1 - dot(wh, wi), 5);

	float D = (material_shininess+2)/(2*PI) * pow(dot(n, wh), material_shininess);

	float G = min(1, min(
		2*((dot(n,wh)*dot(n,wo))/dot(wo,wh)),
		2*((dot(n,wh)*dot(n,wi))/dot(wo,wh))
	));

	float brdf = (F*D*G)/(4*dot(n,wo)*dot(n,wi));

	// Make your shader respect the parameters of our material model.
	vec3 dielectric_term = brdf * dot(n,wi)*li + (1-F)*diffuse_term;

	vec3 metal_term = brdf * base_color * dot(n,wi)*li;

	return material_metalness * metal_term + (1-material_metalness) * dielectric_term;
}

vec3 calculateIndirectIllumination(vec3 wo, vec3 n, vec3 base_color)
{
	vec3 indirect_illum = vec3(0.f);

	// Lookup the irradiance from the irradiance map and calculate the diffuse reflection.    
	// Calculate the spherical coordinates of the direction
	vec3 diffuse_term;
	{
		vec3 dir = (transpose(inverse(viewInverse)) * vec4(n, 0.0)).xyz;

		float theta = acos(max(-1.0f, min(1.0f, dir.y)));
		float phi = atan(dir.z, dir.x);
		if(phi < 0.0f)
		{
			phi = phi + 2.0f * PI;
		}

		// Use these to lookup the color in the environment map
		vec2 lookup = vec2(phi / (2.0 * PI), 1 - theta / PI);
		vec3 irradiance = (texture(irradianceMap, lookup)).xyz;

		diffuse_term = base_color * (1.0 / PI) * irradiance;
	}

	// Look up in the reflection map from the perfect specular direction and calculate the dielectric and metal terms.     
	vec3 dir = (transpose(inverse(viewInverse)) * vec4(n, 0.0)).xyz;

	float theta = acos(max(-1.0f, min(1.0f, dir.y)));
	float phi = atan(dir.z, dir.x);
	if(phi < 0.0f)
	{
		phi = phi + 2.0f * PI;
	}

	// Use these to lookup the color in the environment map
	vec2 lookup = vec2(phi / (2.0 * PI), 1 - theta / PI);



	float roughness = sqrt(sqrt(2/(material_shininess+2)));
	vec3 li = environment_multiplier * textureLod(reflectionMap, lookup, roughness * 7.0).rgb;

	vec3 wi = normalize(viewSpaceLightPosition - viewSpacePosition);
	vec3 wh = normalize(wi + wo);
	float F = material_fresnel + (1 - material_fresnel) * pow(1 - dot(wh, wi), 5);

	vec3 dielectric_term = F*li + (1 - F) * diffuse_term;

	vec3 metal_term = F * base_color * li;

	return material_metalness * metal_term + (1-material_metalness) * dielectric_term;
}

void main()
{
	float visibility = textureProj(shadowMapTex, shadowMapCoord);
	float attenuation = 1.0;

	// Spotlight shadow
	vec3 posToLight = normalize(viewSpaceLightPosition - viewSpacePosition);
	float cosAngle = dot(posToLight, -viewSpaceLightDir);

	// Spotlight with smooth border:
	float spotAttenuation;
	spotAttenuation = smoothstep(spotOuterAngle, spotInnerAngle, cosAngle);
	visibility *= spotAttenuation;

	vec3 wo = -normalize(viewSpacePosition);
	vec3 n = normalize(viewSpaceNormal);

	vec3 base_color = material_color;
	if(has_color_texture == 1)
	{
		base_color = base_color * texture(colorMap, texCoord).rgb;
	}

	// Direct illumination
	vec3 direct_illumination_term = visibility * calculateDirectIllumiunation(wo, n, base_color);

	// Indirect illumination
	vec3 indirect_illumination_term = calculateIndirectIllumination(wo, n, base_color);

	///////////////////////////////////////////////////////////////////////////
	// Add emissive term. If emissive texture exists, sample this term.
	///////////////////////////////////////////////////////////////////////////
	vec3 emission_term = material_emission * material_color;
	if(has_emission_texture == 1)
	{
		emission_term = texture(emissiveMap, texCoord).rgb;
	}

	vec3 shading = direct_illumination_term + indirect_illumination_term + emission_term;

	fragmentColor = vec4(shading, 1.0);
	return;
}
