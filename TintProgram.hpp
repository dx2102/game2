#pragma once

#include "GL.hpp"
#include "Load.hpp"
#include "Scene.hpp"

struct TintProgram {
	TintProgram();
	~TintProgram();

	GLuint program = 0;

	GLuint Position_vec4 = -1U;
	GLuint Normal_vec3 = -1U;
	GLuint Color_vec4 = -1U;

	GLuint CLIP_FROM_OBJECT_mat4 = -1U;
	GLuint LIGHT_FROM_OBJECT_mat4x3 = -1U;
	GLuint LIGHT_FROM_NORMAL_mat3 = -1U;
	GLuint LIGHT_DIRECTION_vec3 = -1U;
	GLuint TINT_vec4 = -1U;
};

extern Load< TintProgram > tint_program;
extern Scene::Drawable::Pipeline tint_program_pipeline;
