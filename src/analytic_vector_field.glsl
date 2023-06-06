#ifndef HEADER_ANALYTIC_VECTOR_FIELD
#define HEADER_ANALYTIC_VECTOR_FIELD

#define PI 3.14159265359
#define A_PARAM sqrt(3)
#define B_PARAM sqrt(2)
#define C_PARAM 1.0

vec3 analytic_vector_field(vec4 pos)
{
    const float t = pos.w;
    const float c_pos = 0.05;
    const float c_t1 = 0.05;
    const float c_t2 = 0.01;
    const float a_coeff = A_PARAM + c_t1 * t * sin(PI * t * c_t2);

    pos.xyz -= vec3(100, 0, 100);

    return vec3(a_coeff * sin(pos.z * c_pos) + B_PARAM * cos(pos.y * c_pos),
                B_PARAM * sin(pos.x * c_pos) + C_PARAM * cos(pos.z * c_pos),
                C_PARAM * sin(pos.y * c_pos) + a_coeff * cos(pos.x * c_pos));
}

#endif