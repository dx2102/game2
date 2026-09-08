#include "TintProgram.hpp"

#include "gl_compile_program.hpp"
#include "gl_errors.hpp"

Scene::Drawable::Pipeline tint_program_pipeline;

Load< TintProgram > tint_program(LoadTagEarly, []() -> TintProgram const * {
	TintProgram *ret = new TintProgram();
	tint_program_pipeline.program = ret->program;
	tint_program_pipeline.CLIP_FROM_OBJECT_mat4 = ret->CLIP_FROM_OBJECT_mat4;
	tint_program_pipeline.LIGHT_FROM_OBJECT_mat4x3 = ret->LIGHT_FROM_OBJECT_mat4x3;
	tint_program_pipeline.LIGHT_FROM_NORMAL_mat3 = ret->LIGHT_FROM_NORMAL_mat3;
	return ret;
});

TintProgram::TintProgram() {
	program = gl_compile_program(
		"#version 330\n"
		"uniform mat4 CLIP_FROM_OBJECT;\n"
		"uniform mat4x3 LIGHT_FROM_OBJECT;\n"
		"uniform mat3 LIGHT_FROM_NORMAL;\n"
		"in vec4 Position;\n"
		"in vec3 Normal;\n"
		"in vec4 Color;\n"
		"out vec3 normal;\n"
		"out vec4 color;\n"
		"void main() {\n"
		"	gl_Position = CLIP_FROM_OBJECT * Position;\n"
		"	normal = LIGHT_FROM_NORMAL * Normal;\n"
		"	color = Color;\n"
		"}\n"
	,
		"#version 330\n"
		"uniform vec3 LIGHT_DIRECTION;\n"
		"uniform vec4 TINT;\n"
		"in vec3 normal;\n"
		"in vec4 color;\n"
		"out vec4 fragColor;\n"
		"void main() {\n"
		"	vec3 n = normalize(normal);\n"
		"	float e = 0.55 + 0.45 * (dot(n, -LIGHT_DIRECTION) * 0.5 + 0.5);\n"
		"	vec4 albedo = color * TINT;\n"
		"	fragColor = vec4(e * albedo.rgb, albedo.a);\n"
		"}\n"
	);

	Position_vec4 = glGetAttribLocation(program, "Position");
	Normal_vec3 = glGetAttribLocation(program, "Normal");
	Color_vec4 = glGetAttribLocation(program, "Color");

	CLIP_FROM_OBJECT_mat4 = glGetUniformLocation(program, "CLIP_FROM_OBJECT");
	LIGHT_FROM_OBJECT_mat4x3 = glGetUniformLocation(program, "LIGHT_FROM_OBJECT");
	LIGHT_FROM_NORMAL_mat3 = glGetUniformLocation(program, "LIGHT_FROM_NORMAL");
	LIGHT_DIRECTION_vec3 = glGetUniformLocation(program, "LIGHT_DIRECTION");
	TINT_vec4 = glGetUniformLocation(program, "TINT");

	glUseProgram(program);
	glUniform3f(LIGHT_DIRECTION_vec3, -0.32f, -0.22f, -0.92f);
	glUniform4f(TINT_vec4, 1.0f, 1.0f, 1.0f, 1.0f);
	glUseProgram(0);
}

TintProgram::~TintProgram() {
	glDeleteProgram(program);
	program = 0;
}
