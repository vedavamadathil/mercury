#ifndef SHADER_H_
#define SHADER_H_

// Standard headers
#include <string>

// GLM
#include "glm/glm/glm.hpp"

namespace mercury {

class Shader {
public:
	Shader(const char *, const char *);

	void use();

	// Variables
	unsigned int id;

	// Setters
	void set_bool(const std::string &, bool) const;
	void set_int(const std::string &, int) const;
	void set_float(const std::string &, float) const;

	void set_vec2(const std::string &, const glm::vec2 &) const;
	void set_vec2(const std::string &, float, float) const;

	void set_vec3(const std::string &, const glm::vec3 &) const;
	void set_vec3(const std::string &, float, float, float) const;

	void set_vec4(const std::string &, const glm::vec4 &) const;
	void set_vec4(const std::string &, float, float, float, float) const;

	void set_mat2(const std::string &, const glm::mat2 &) const;
	void set_mat3(const std::string &, const glm::mat3 &) const;
	void set_mat4(const std::string &, const glm::mat4 &) const;
};

}

#endif
