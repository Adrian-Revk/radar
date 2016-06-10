#version 400
#define M_PI     3.14159265358
#define M_INV_PI 0.31830988618

#define IOR_IRON 2.9304
#define IOR_POLYSTYREN 1.5916

#define IOR_GOLD 0.27049
#define IOR_SILVER 0.15016
#define SCOLOR_GOLD      vec3(1.000000, 0.765557, 0.336057)
#define SCOLOR_SILVER    vec3(0.971519, 0.959915, 0.915324)
#define SCOLOR_ALUMINIUM vec3(0.913183, 0.921494, 0.924524)
#define SCOLOR_COPPER    vec3(0.955008, 0.637427, 0.538163)

in vec4 v_color;
in vec3 v_position;
in vec3 v_normal;
in vec2 v_texcoord;

struct Light {
    vec3 position;
    vec4 ambient;
    vec4 diffuse;
    float radius;
};

uniform Light lights[10];
uniform sampler2D tex0;
uniform vec3 eyePosition;

out vec4 frag_color;

float fresnel_F0(float n1, float n2) {
    float f0 = (n1 - n2)/(n1 + n2);
    return f0 * f0;
}

vec3 F_schlick(vec3 f0, float f90, float u) {
    return f0 + (f90 - f0) * pow(1.0 - u, 5.0);
}

float G_schlick_GGX(float k, float dotVal) {
    return dotVal / (dotVal * (1 - k) + k);
}

vec3 GGX(float NdotL, float NdotV, float NdotH, float LdotH, float roughness, vec3 F0) {
    float alpha2 = roughness * roughness;

    // F 
    vec3 F = F_schlick(F0, 1.0, LdotH);

    // D (Trowbridge-Reitz). Divide by PI at the end
    float denom = NdotH * NdotH * (alpha2 - 1.0) + 1.0;
    float D = alpha2 / (M_PI * denom * denom);

    // G (Smith GGX - Height-correlated)
    float lambda_GGXV = NdotL * sqrt((-NdotV * alpha2 + NdotV) * NdotV + alpha2);
    float lambda_GGXL = NdotV * sqrt((-NdotL * alpha2 + NdotL) * NdotL + alpha2);
    // float G = G_schlick_GGX(k, NdotL) * G_schlick_GGX(k, NdotV);

    // optimized G(NdotL) * G(NdotV) / (4 NdotV NdotL)
    // float G = 1.0 / (4.0 * (NdotL * (1 - k) + k) * (NdotV * (1 - k) + k));
    float G = 0.5 / (lambda_GGXL + lambda_GGXV);

    return D * F * G;
}

// Renormalized version of Burley's Disney Diffuse factor, used by Frostbite
float diffuse_Burley(float NdotL, float NdotV, float LdotH, float roughness) {
    float energyBias = mix(0.0, 0.5, roughness);
    float energyFactor = mix(1.0, 1.0 / 1.51, roughness);
    float fd90 = energyBias + 2.0 * LdotH * LdotH * roughness;
    vec3 f0 = vec3(1.0);
    float lightScatter = F_schlick(f0, fd90, NdotL).r;
    float viewScatter = F_schlick(f0, fd90, NdotV).r;

    return lightScatter * viewScatter * energyFactor * M_INV_PI;
}

float diffuse_Lambert(float NdotL) {
    return NdotL * M_INV_PI;
}

void main() {
    vec3 light_vec = vec3(-3,5,-2) - v_position;
    float light_dist = length(light_vec);

    vec3 light_color = vec3(10,10,10) / (light_dist * light_dist);
    vec3 diffuse_color = vec3(2.0, 0.5, 0);
    float specular_power = 30.0;
    float roughness = 0.1;// / specular_power;
    // float F0 = fresnel_F0(1.0, IOR_GOLD);
    vec3 F0 = SCOLOR_COPPER;

    vec3 N = normalize(v_normal);
    vec3 V = normalize(eyePosition - v_position);
    vec3 L = light_vec / light_dist;
    vec3 H = normalize(V + L);

    float NdotL = max(dot(L, N), 0.0);

    vec3 light_contrib = vec3(0);
    if(NdotL > 0.0)
    {
        float NdotV = abs(dot(N, V)) + 1e-5;
        float NdotH = max(0, dot(N, H));
        float LdotH = max(0, dot(L, H));

        vec3 Fd = diffuse_color * diffuse_Burley(NdotL, NdotV, LdotH, roughness);
        vec3 Fr = GGX(NdotL, NdotV, NdotH, LdotH, roughness, F0);

        light_contrib = light_color * (Fd + Fr);
    }

    // texturing
    vec4 tex_color = texture2D(tex0, v_texcoord);

    vec4 ambient = vec4(0.2, 0.2, 0.2, 1) * vec4(diffuse_color,1) ;


    frag_color = (ambient + vec4(light_contrib, 1)) *      // lighting
                 1 * //(0.3 + 0.7 * v_color) *    // color
                 tex_color;                 // texture
}
