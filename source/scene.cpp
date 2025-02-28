// Standard headers
#include <filesystem>

// Engine headers
#include "../include/scene.hpp"
#include "../include/profiler.hpp"

namespace kobra {

// Scene saving functions and loading functions
static constexpr char transform_format[]
= R"(position: %f %f %f
rotation: %f %f %f
scale: %f %f %f
)";

// TODO: source files as well...
static constexpr char material_format[]
= R"(diffuse: %f %f %f
specular: %f %f %f
emission: %f %f %f
ambient: %f %f %f
shininess: %f
roughness: %f
refraction: %f
albedo_texture: %s
normal_texture: %s
roughness_texture: %s
)";

static const std::array <std::string, 4> rasterizer_modes {
	"Phong",
	"Normal",
	"Albedo",
	"Wireframe"
};

static constexpr char light_format[]
= R"(color: %f %f %f
power: %f
type: %s
)";

static const std::array <std::string, 4> light_types {
	"Point",
	"Spot",
	"Directional",
	"Area"
};

// TODO: get rid of the transform portion
static constexpr char camera_format[]
= R"(fov: %f
aspect: %f
)";

void Scene::save(const std::string &file) {}

// Scene loading functions and helpers
inline std::string get_header(std::ifstream &fin)
{
	std::string line;

	do {
		std::getline(fin, line);
	} while (line.empty() && !fin.eof());

	return line;
}

template <class ... Args>
static void _read_fmt(const char *header, std::ifstream &fin, const char *fmt, Args ... args)
{
	// Count number of lines to read
	int count = 0;
	for (const char *c = fmt; *c != '\0'; c++) {
		if (*c == '\n')
			count++;
	}

	// Read lines
	std::string line;
	std::string whole;
	for (int i = 0; i < count; i++) {
		std::getline(fin, line);
		whole += line + "\n";
	}

	// Parse
	int read = sscanf(whole.c_str(), fmt, std::forward <Args> (args)...);
	if (read != sizeof...(Args)) {
		KOBRA_LOG_FUNC(Log::WARN) << "[" << header
			<< "] Failed to read field after #"
			<< read << " fields\n";
	}
}

#define read_fmt(fin, fmt, ...) _read_fmt(__FUNCTION__, fin, fmt, __VA_ARGS__)

// Component basis
void load_transform(Entity &e, std::ifstream &fin)
{
	// TODO: eventually just use the format
	e.add <Transform> ();

	Transform &transform = e.get <Transform> ();

	read_fmt(fin, transform_format,
		&transform.position.x, &transform.position.y, &transform.position.z,
		&transform.rotation.x, &transform.rotation.y, &transform.rotation.z,
		&transform.scale.x, &transform.scale.y, &transform.scale.z
	);
}

/* void load_material(Entity &e, std::ifstream &fin)
{
	static char buf_albedo[1024];
	static char buf_normal[1024];
	static char buf_roughness[1024];

	e.add <Material> ();

	Material &material = e.get <Material> ();

	read_fmt(fin, material_format,
		&material.diffuse.x, &material.diffuse.y, &material.diffuse.z,
		&material.specular.x, &material.specular.y, &material.specular.z,
		&material.emission.x, &material.emission.y, &material.emission.z,
		&material.ambient.x, &material.ambient.y, &material.ambient.z,
		&material.shininess,
		&material.roughness,
		&material.refraction,
		buf_albedo, buf_normal, buf_roughness
	);

	material.shininess = glm::clamp(material.shininess, 0.0f, 1.0f);
	material.roughness = glm::clamp(material.roughness, 0.0f, 1.0f);

	std::string line;
	std::getline(fin, line);

	std::string field = line.substr(0, 14);
	std::string value = line.substr(14);

	if (field != "shading_type: ") {
		KOBRA_LOG_FUNC(Log::WARN) << "Failed to read shading type: field = \""
			<< field  << "\": value = \"" << value << "\"" << std::endl;
		return;
	}

	if (std::string(buf_albedo) != "0")
		material.albedo_texture = buf_albedo;

	if (std::string(buf_normal) != "0")
		material.normal_texture = buf_normal;

	if (std::string(buf_roughness) != "0")
		material.roughness_texture = buf_roughness;

	material.type = *shading_from_str(value);

	// If mesh exists, override its material
	if (e.exists <Mesh> ()) {
		for (auto &submesh : e.get <Mesh> ().submeshes)
			submesh.material = material;
	}

	// TODO: materials in a separate system from meshes, entities, etc.
} */

void load_mesh(Entity &e, std::ifstream &fin)
{
	static char buf_source[1024];


	std::string line;
	std::getline(fin, line);

	sscanf(line.c_str(), "source: %s", buf_source);

	if (std::string(buf_source) != "0") {
		auto mptr = Mesh::load(buf_source);

		if (!mptr.has_value()) {
			KOBRA_LOG_FUNC(Log::WARN) << "Failed to load mesh: " << buf_source << std::endl;
			return;
		}

		e.add <Mesh> (std::get <0> (*mptr));
	} else {
		// Raw mesh
		std::vector <Submesh> submeshes;
		while (true) {
			// Iterate over submeshes
			std::getline(fin, line);
			if (line.empty())
				break;

			if (line != "submesh {") {
				KOBRA_LOG_FUNC(Log::WARN) << "Expected submesh, got: " << line << std::endl;
				return;
			}

			// Submesh in creation
			std::vector <Vertex> vertices;
			std::vector <uint32_t> indices;

			// Read submesh vertices
			while (true) {
				std::getline(fin, line);
				if (line.empty())
					break;

				glm::vec3 vertex;
				sscanf(line.c_str(), "\tv %f %f %f", &vertex.x, &vertex.y, &vertex.z);
				vertices.push_back(vertex);
			}

			// Read faces
			while (true) {
				std::getline(fin, line);

				// TODO: check amount read
				int v0, v1, v2;
				int read = sscanf(line.c_str(), "\tf %d %d %d", &v0, &v1, &v2);

				if (read == 0)
					break;

				indices.push_back(v0);
				indices.push_back(v1);
				indices.push_back(v2);
			}

			// Assert closure
			if (line != "}") {
				KOBRA_LOG_FUNC(Log::WARN) << "Expected closure, got: " << line << std::endl;
				return;
			}

			// Append submesh
			submeshes.push_back({vertices, indices});
		}

		// Create mesh
		e.add <Mesh> (submeshes);
	}

	/* If material exists, override its material
	if (e.exists <Material> ()) {
		Material &material = e.get <Material> ();
		for (auto &submesh : e.get <Mesh> ().submeshes)
			submesh.material = material;
	} */
}

void load_renderable(Entity &e, std::ifstream &fin, const Context &context)
{
	static char buf_mode[1024];

	// Mkae sure we have a mesh and material
	if (!e.exists <Mesh> ()) {
		KOBRA_LOG_FUNC(Log::WARN) << "No mesh for rasterizer" << std::endl;
		return;
	}

	e.add <Renderable> (context, &e.get <Mesh> ());
}

void load_camera(Entity &e, std::ifstream &fin)
{
	e.add <Camera> ();

	Camera &camera = e.get <Camera> ();
	read_fmt(fin, camera_format,
		&camera.fov, &camera.aspect
	);
}

void load_light(Entity &e, std::ifstream &fin)
{
	static char buf_type[1024];

	e.add <Light> ();

	Light &light = e.get <Light> ();
	read_fmt(fin, light_format,
		&light.color.x, &light.color.y, &light.color.z,
		&light.power, buf_type
	);

	// Get light type
	int index = 0;
	while (light_types[index] != buf_type)
		index++;

	if (index >= light_types.size()) {
		KOBRA_LOG_FUNC(Log::WARN) << "Unknown light type: " << buf_type << std::endl;
		return;
	}

	light.type = Light::Type(index);
}

std::string load_components(Entity &e, std::ifstream &fin, const Context &context)
{
	std::string header;

	// Go in order of the components
	while (true) {
		header = get_header(fin);
		if (header == "[TRANSFORM]") {
			load_transform(e, fin);
			continue;
		}

		/* if (header == "[MATERIAL]") {
			load_material(e, fin);
			continue;
		} */

		if (header == "[MESH]") {
			load_mesh(e, fin);
			continue;
		}

		if (header == "[RENDERABLE]") {
			load_renderable(e, fin, context);
			continue;
		}

		if (header == "[CAMERA]") {
			load_camera(e, fin);
			continue;
		}

		if (header == "[LIGHT]") {
			load_light(e, fin);
			continue;
		}

		KOBRA_LOG_FUNC(Log::WARN) << "Unknown component: " << header << std::endl;
		break;
	}

	return header;
}

void Scene::load(const Context &context, const std::string &path)
{
	KOBRA_PROFILE_TASK("Scene loading");

	static char buf[1024] = "";

	std::ifstream fin(path);
	if (!fin.is_open()) {
		KOBRA_LOG_FUNC(Log::ERROR) << "Failed to open file: " << path << std::endl;
		return;
	}

	// Load properties
	if (get_header(fin) != "[PROPERTIES]") {
		KOBRA_LOG_FUNC(Log::ERROR) << "Failed to load properties" << std::endl;
		return;
	}

	read_fmt(fin, "environment_map: %s\n", buf);
	p_environment_map = buf;

	// Initialize the System
	system = std::make_shared <System> (nullptr);

	// Load entities
	std::string header = get_header(fin);
	while (fin.good()) {
		if (header != "[ENTITY]") {
			KOBRA_LOG_FUNC(Log::ERROR) << "Invalid header: " << header << std::endl;
			return;
		}

		read_fmt(fin, "name: %1023[^\n]\n", buf);
		Entity &e = system->make_entity(buf);

		header = load_components(e, fin, context);
	}
}

}
