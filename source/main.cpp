#include <iostream>
#include <unordered_map>

// GLFW headers
#include <glad/glad.h>
#include <GLFW/glfw3.h>

// GLM headers
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

// Engine headers
#include "include/shader.hpp"
#include "include/model.hpp"
#include "include/init.hpp"
#include "include/logger.hpp"
#include "include/lighting.hpp"
#include "include/rendering.hpp"
#include "include/varray.hpp"

#include "include/physics/physics.hpp"

#include "include/engine/camera.hpp"
#include "include/engine/monitors.hpp"
#include "include/engine/skybox.hpp"

#include "include/math/linalg.hpp"

#include "include/mesh/basic.hpp"
#include "include/mesh/sphere.hpp"
#include "include/mesh/cuboid.hpp"

#include "include/ui/text.hpp"
#include "include/ui/ui_layer.hpp"
#include "include/ui/line.hpp"

// Using declarations
using namespace mercury;

// Forward declarations
void mouse_callback(GLFWwindow* window, double xpos, double ypos);
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset);
void process_input(GLFWwindow *window, float);

// Camera
mercury::Camera camera(glm::vec3(5.0f, 0.0f, 10.0f));

// Daemons
lighting::Daemon	ldam;
rendering::Daemon	rdam;
physics::Daemon		pdam;

// Annotations
std::vector <Drawable *> annotations;

Shader sphere_shader;		// TODO: change to annotation shader

void add_annotation(SVA3 *sva, const glm::vec3 &color, Transform *transform = nullptr)
{
	static Transform default_transform;

	sva->color = color;
	size_t index = annotations.size();
	annotations.push_back(sva);
	rdam.add(
		annotations[index],
		&sphere_shader,
		(transform ? transform : &default_transform)
	);
}

void add_annotation(ui::Line *line, const glm::vec3 &color)
{
	line->color = color;
	size_t index = annotations.size();
	// lines.push_back(*line);
	annotations.push_back(line);
	rdam.add(annotations[index], winman.cres.line_shader);
}

// Render function for main window
Mesh hit_cube1;
Mesh hit_cube2;
Mesh hit_cube3;

// Rigidbody components
Transform rb_transform({0, 10, 0}, {30, 30, 30});
Transform t2({6, -2, 0}, {0, 0, 93});
Transform floor_transform({0, -1, 0}, {0, 0, -10});

physics::BoxCollider rb_collider({1, 1, 1}, &rb_transform);
physics::BoxCollider t2_collider({1, 2, 1}, &t2);
physics::BoxCollider floor_collider({10, 1, 10}, &floor_transform);

physics::RigidBody rb(1.0f, &rb_transform, &rb_collider);
physics::RigidBody t2_rb(1.0f, &t2, &t2_collider);
physics::RigidBody fl(1.0f, &floor_transform, &floor_collider);

// glm::mat4 *rb_model = new glm::mat4(1.0);
// glm::vec3 position = {0, 10, 0};

glm::vec3 velocity;
glm::vec3 gravity {0, -9.81, 0};

// Skybox
Skybox sb;

// Lights
const glm::vec3 lpos1 {2, 1.6, 1.6};
const glm::vec3 lpos2 {0.2, 1.6, 1.6};

lighting::DirLight dirlight {
	{-0.2f, -1.0f, -0.3f},
	{0.2f, 0.2f, 0.2f},
        {0.9f, 0.9f, 0.9f},
	{1.0f, 1.0f, 1.0f}
};

// TODO: color constants and hex strings

// Wireframe sphere: TODO: is there a better way than to manually set vertices?
const glm::vec3 center {1.0, 1.0, 1.0};
const float radius = 0.2f;

// Collision detection
using namespace physics;

// GJK Simplex
class Simplex {
	glm::vec3	_points[4];
	size_t		_size;
public:
	Simplex() : _size(0) {}

	// Assign with initializer list
	Simplex &operator=(std::initializer_list <glm::vec3> il) {
		_size = 0;
		for (const glm::vec3 &pt : il)
			_points[_size++] = pt;
		
		return *this;
	}

	// Size
	size_t size() const {
		return _size;
	}

	// Push vector
	void push(const glm::vec3 &v) {
		// Cycle the points
		_points[3] = _points[2];
		_points[2] = _points[1];
		_points[1] = _points[0];
		_points[0] = v;

		// Cap the index
		_size = std::min(_size + 1, 4UL);
	}

	// Indexing
	const glm::vec3 &operator[](size_t index) const {
		return _points[index];
	}

	// Vertices
	Collider::Vertices vertices() const {
		Collider::Vertices v;
		for (size_t i = 0; i < _size; i++)
			v.push_back(_points[i]);
		return v;
	}

	// As SVA3
	SVA3 sva() const {
		std::vector <glm::vec3> verts {
			_points[0], _points[1], _points[2], _points[0],
			_points[1], _points[3], _points[2], _points[0],
			_points[1], _points[2], _points[3], _points[0],
			_points[2], _points[3], _points[0], _points[0]
		};
		
		return SVA3(verts);
	}
};

// TODO: implement as a virtual function for colliders (resolved sphere collider issues)
glm::vec3 support(const glm::vec3 &dir, const Collider::Vertices &vertices)
{
	glm::vec3 vmax;

	float dmax = -std::numeric_limits <float> ::max();
	for (const glm::vec3 &v : vertices) {
		float d = glm::dot(dir, v);

		if (d > dmax) {
			dmax = d;
			vmax = v;
		}
	}

	return vmax;
}

glm::vec3 support(const glm::vec3 &dir, const Collider::Vertices &vs1, const Collider::Vertices &vs2)
{
	return support(dir, vs1) - support(-dir, vs2);
}

// Check if vectors are in the same direction (TODO: put in linalg)
bool same_direction(const glm::vec3 &v1, const glm::vec3 &v2)
{
	return glm::dot(v1, v2) > 0;
}

// Simplex stages (TODO: should be these be Simplex methods?)
bool line_simplex(Simplex &simplex, glm::vec3 &dir)
{
	// Logger::warn() << "SIMPLEX-Line stage.\n";
	glm::vec3 a = simplex[0];
	glm::vec3 b = simplex[1];

	glm::vec3 ab = b - a;
	glm::vec3 ao = -a;

	if (same_direction(ab, ao)) {
		dir = glm::cross(glm::cross(ab, ao), ab);
	} else {
		simplex = {a};
		dir = ab;
	}

	return false;
}

bool triangle_simplex(Simplex &simplex, glm::vec3 &dir)
{
	// Logger::warn() << "SIMPLEX-Triangle stage.\n";
	glm::vec3 a = simplex[0];
	glm::vec3 b = simplex[1];
	glm::vec3 c = simplex[2];

	glm::vec3 ab = b - a;
	glm::vec3 ac = c - a;
	glm::vec3 ao = -a;

	glm::vec3 abc = glm::cross(ab, ac);

	if (same_direction(glm::cross(abc, ac), ao)) {
		if (same_direction(ac, ao)) {
			simplex = {a, c};
			dir = glm::cross(glm::cross(ac, ao), ac);
		} else {
			simplex = {a, b};
			return line_simplex(simplex, dir);
		}
	} else {
		if (same_direction(glm::cross(ab, abc), ao)) {
			simplex = {a, b};
			return line_simplex(simplex, dir);
		} else {
			if (same_direction(abc, ao)) {
				dir = abc;
			} else {
				simplex = {a, b, c};
				dir = -abc;
			}
		}
	}

	return false;
}

bool tetrahedron_simplex(Simplex &simplex, glm::vec3 &dir)
{
	// Logger::warn() << "SIMPLEX-Tetrahedron stage.\n";
	glm::vec3 a = simplex[0];
	glm::vec3 b = simplex[1];
	glm::vec3 c = simplex[2];
	glm::vec3 d = simplex[3];

	glm::vec3 ab = b - a;
	glm::vec3 ac = c - a;
	glm::vec3 ad = d - a;
	glm::vec3 ao = -a;

	glm::vec3 abc = glm::cross(ab, ac);
	glm::vec3 acd = glm::cross(ac, ad);
	glm::vec3 adb = glm::cross(ad, ab);

	if (same_direction(abc, ao)) {
		simplex = {a, b, c};
		// Logger::warn() << "\tSIMPLEX-Tetrahedron SUB-stage: abc.\n";
		return triangle_simplex(simplex, dir);
	}
		
	if (same_direction(acd, ao)) {
		simplex = {a, c, d};
		// Logger::warn() << "\tSIMPLEX-Tetrahedron SUB-stage: acd.\n";
		return triangle_simplex(simplex, dir);
	}
 
	if (same_direction(adb, ao)) {
		simplex = {a, d, b};
		// Logger::warn() << "\tSIMPLEX-Tetrahedron SUB-stage: adb.\n";
		return triangle_simplex(simplex, dir);
	}

	// Logger::warn() << "\tSIMPLEX-Tetrahedron Stage COMPLETED\n";

	return true;
}

// Update the simplex (TODO: method)
bool next_simplex(Simplex &simplex, glm::vec3 &dir)
{
	// Cases for each simplex size
	switch (simplex.size()) {
	case 2:
		return line_simplex(simplex, dir);
	case 3:
		return triangle_simplex(simplex, dir);
	case 4:
		return tetrahedron_simplex(simplex, dir);
	}

	return false;
}

bool gjk(Simplex &simplex, const Collider *a, const Collider *b)
{
	// Logger::notify() << "Inside GJK function.\n";

	Collider::Vertices va = a->vertices();
	Collider::Vertices vb = b->vertices();

	// First direction and support
	glm::vec3 dir {1.0f, 0.0f, 0.0f};
	glm::vec3 s = support(dir, va, vb);
	simplex.push(s);

	// Next direction
	dir = -s;

	size_t i = 0;
	while (i++ < 100) {
		// Logger::notify() << "\tDirection = " << dir << "\n";
		
		// Support
		s = support(dir, va, vb);

		// Check for no intersection
		if (glm::dot(s, dir) <= 0.0f)
			return false;
		
		simplex.push(s);
		if (next_simplex(simplex, dir))
			return true;
	}

	// Should not get here
	Logger::fatal_error("GJK failed to converge.");
	return false;
}

// EPA algorithm
glm::vec3 polytope_center(const Collider::Vertices &vertices)
{
	glm::vec3 center {0.0f, 0.0f, 0.0f};
	for (const glm::vec3 &v : vertices)
		center += v;
	return center / (float) vertices.size();
}

// Get the normals for each face
struct NormalInfo {
	glm::vec3 normal;
	glm::uvec3 face;
	float distance;

	std::vector <glm::vec3> nfaces;
};

NormalInfo face_normals(const Collider::Vertices &vertices, const std::vector <glm::uvec3> &faces)
{
	// List of all normals
	std::vector <glm::vec3> normals;

	// Minimum info
	glm::uvec3 minf = {0, 0, 0};
	glm::vec3 minn = {0.0f, 0.0f, 0.0f};
	float mind = std::numeric_limits <float> ::max();

	for (const glm::uvec3 &face : faces) {
		glm::vec3 a = vertices[face[0]];
		glm::vec3 b = vertices[face[1]];
		glm::vec3 c = vertices[face[2]];

		glm::vec3 ab = b - a;
		glm::vec3 ac = c - a;
		glm::vec3 n = glm::cross(ab, ac);

		// TODO: we can check if the normal is reversed,
		// and then recerse it if needed to avoid having to check it later
		float d = glm::dot(n, a);	// TODO: Should be with the center, right?

		if (d < mind) {
			mind = d;
			minf = face;
			minn = n;
		}

		normals.push_back(n);
	}

	return {minn, minf, mind, normals};
}

// Check that the normals are facing the right direction (TODO: some linalg function)
bool check_normals(const Collider::Vertices &vertices, const std::vector <glm::uvec3> &faces, const std::vector <glm::vec3> &normals)
{
	glm::vec3 center = polytope_center(vertices);

	for (size_t i = 0; i < faces.size(); i++) {
		glm::vec3 sample = vertices[faces[i].x] - center;
		if (glm::dot(sample, normals[i]) < 0.0f)
			return false;
	}

	return true;
}

// Check if a face faces a vertex
bool faces_vertex(const glm::vec3 face[3], const glm::vec3 &normal, const glm::vec3 &vertex)
{
	// NOTE: the first vertex was always used to compute the normal
	return glm::dot(normal, vertex - face[0]) < 0.0f;
}

// Expand a polytope with the new vertex
void expand_polytope(Collider::Vertices &vertices,
		std::vector <glm::uvec3> &faces,
		const std::vector <glm::vec3> &normals,
		const glm::vec3 &svert)
{
	// Edge structure
	struct Edge {
		unsigned int a;
		unsigned int b;

		bool operator==(const Edge &other) const {
			return (a == other.a) && (b == other.b);
		}
	};

	// Edge list
	std::vector <Edge> edges;

	// Get array of edges in a face
	auto get_edges = [&] (const glm::uvec3 &face) {
		Edge e1 {face[0], face[1]};
		Edge e2 {face[1], face[2]};
		Edge e3 {face[2], face[0]};

		return std::vector <Edge> {e1, e2, e3};
	};

	// TODO: we are not going to remove vertices from
	// the polytope right now. Should this be considered?

	// Iterate over all faces
	for (size_t i = 0; i < faces.size(); i++) {
		// Face
		glm::vec3 vface[3] = {
			vertices[faces[i].x],
			vertices[faces[i].y],
			vertices[faces[i].z]
		};

		// Check if the face faces the new vertex
		if (!faces_vertex(vface, normals[i], svert))
			continue;

		// Get iterator at this posiiton and remove it
		auto it = faces.begin() + i;
		faces.erase(it);

		// Get edges
		glm::uvec3 f = faces[i];
		Edge iface[3] = {
			Edge {f.x, f.y},
			Edge {f.y, f.z},
			Edge {f.z, f.x}
		};
		
		// Check edges
		for (size_t i = 0; i < 3; i++) {
			Edge e = iface[i];

			auto itr = std::find(edges.begin(), edges.end(), e);
			if (itr == edges.end())
				edges.erase(itr);	
			else
				edges.push_back(e);
		}

		// Account for the shift in indices
		i--;
	}

	// Create the new triangles
	size_t svi = vertices.size();
	vertices.push_back(svert);
	for (const Edge &e : edges) {
		faces.push_back(
			{e.a, e.b, svi}
		);
	}
}

glm::vec3 mtv(Simplex &simplex, Collider *a, Collider *b)
{
	static const size_t maxi = 100;

	Collider::Vertices polytope = simplex.vertices();
	std::vector <glm::uvec3> faces {
		{0, 1, 2},
		{0, 3, 1},
		{0, 2, 3},
		{1, 3, 2}
	};

	// TODO: set a loop counter a backup notifier for infinite loops
	size_t i = 0;
	while (true) {
		// One interation of EPA
		NormalInfo ninfo = face_normals(polytope, faces);

		/* Logger::notify() << "EPA index: " << nfaces.second << "\n";
		for (const glm::vec3 &n : nfaces.first)
			Logger::notify() << "\tNormal: " << n << "\n"; */

		/* Print all points in the simplex
		Logger::notify() << "Simplex points: ";
		for (const glm::vec3 &v : simplex.vertices())
			Logger::notify() << "\tPoint: " << v << "\n"; */
		
		// Get the furthest point from the normal
		glm::vec3 svert = support(ninfo.normal, a->vertices(), b->vertices());
		// Logger::notify() << "Support point: " << svert << "\n";

		float sdist = glm::dot(svert, ninfo.normal);

		/* Logger::notify() << "Min distance: " << ninfo.distance << "\n";
		Logger::notify() << "Support distance: " << sdist << "\n"; */

		if (fabs(sdist - ninfo.distance) < 0.1f) {
			// Logger::notify() << "Terminate EPA, normal is = " << ninfo.normal << "\n";
			return ninfo.distance * glm::normalize(ninfo.normal);
		}

		expand_polytope(polytope, faces, ninfo.nfaces, svert);
		
		if (i++ > maxi)
			Logger::error("MTV Algorithm has exceed [maxi] iterations");
	}

	return {0, 0, 0};
}

void main_initializer()
{
	// Uncap FPS
	glfwSwapInterval(0);

	// TODO: put in init or smthing
	// stbi_set_flip_vertically_on_load(true);

	// Configure global opengl state
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_MULTISAMPLE);

	// Hide cursor
	glfwSetInputMode(winman.cwin,
		GLFW_CURSOR, GLFW_CURSOR_DISABLED);

	// TODO: do in init
	srand(clock());

	// Draw in wireframe -- TODO: should be an init option (or live option)
	// glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

	// Load resources
	winman.load_font(0);
	winman.load_skybox(0);
	winman.load_lines(0);

	// Meshes
	hit_cube1 = mesh::cuboid({0, 0, 0}, 1, 1, 1);
	hit_cube2 = mesh::cuboid({0, 0, 0}, 1, 2, 1);
	hit_cube3 = mesh::cuboid({0, 0, 0}, 10, 1, 10);	// TODO: size only function
	
	// Set the materials
	hit_cube1.set_material({.color = {0.5, 1.0, 0.5}});
	hit_cube2.set_material({.color = {1.0, 0.5, 0.5}});
	hit_cube3.set_material({.color = {0.9, 0.9, 0.9}});

	// Set line width
	glLineWidth(5.0f);

	// Create the sphere
	sphere_shader = Shader(
		_shader("basic3d.vert"),
		_shader("basic.frag")
	);

	sphere_shader.set_name("sphere_shader");

	// TODO: some way to check that the resources being used in render are in another context

	// Skybox stuff
	sb = Skybox({
		"resources/textures/skybox/uv_4.png",
		"resources/textures/skybox/uv_2.png",
		"resources/textures/skybox/uv_1.png",
		"resources/textures/skybox/uv_6.png",
		"resources/textures/skybox/uv_3.png",
		"resources/textures/skybox/uv_5.png"
	});

	// Add objects and lights to the ldam system
	ldam = lighting::Daemon(&rdam);

	ldam.add_light(dirlight);

	ldam.add_object(&hit_cube1, &rb_transform);
	//ldam.add_object(&hit_cube2, &t2);
	//ldam.add_object(&hit_cube3, &floor_transform);

	// ldam.add_object(&tree);

	// Add objects to the render daemon
	rdam.add(&sb, winman.cres.sb_shader);

	// physics::AABB box1 = rb_collider.aabb();
	// box1.annotate(rdam, &sphere_shader);
	
	physics::AABB box2 = floor_collider.aabb();
	box2.annotate(rdam, &sphere_shader);

	// Logger::notify() << "INTERSECTS: " << std::boolalpha << box1.intersects(box2) << "\n";

	// Physics objects
	// pdam.add_rb(&rb);
	// pdam.add_rb(&t2_rb);
	// pdam.add_cb(&fl);

	// Annotations
	// rb_collider.annotate(rdam, &sphere_shader);
	// floor_collider.annotate(rdam, &sphere_shader);

	// add_annotation(new SVA3(mesh::wireframe_cuboid({0, 0, 0}, {15, 0.1, 15})), {0.5, 1.0, 1.0});
	// add_annotation(new SVA3(mesh::wireframe_sphere({0, 0, 0}, 0.1)), {0.5, 1.0, 1.0});
	// Logger::notify() << "GJK RESULT (2) = " << std::boolalpha << gjk(&rb_collider, &floor_collider) << std::endl;
	// Logger::notify() << "GJK RESULT (3) = " << std::boolalpha << gjk(&t2_collider, &rb_collider) << std::endl;
	
	Simplex simplex;
	Logger::notify() << "GJK RESULT = " << std::boolalpha << gjk(simplex, &t2_collider, &floor_collider) << std::endl;
	add_annotation(new SVA3(simplex.sva()), {0.5, 1.0, 1.0});
	glm::vec3 t = mtv(simplex, &t2_collider, &floor_collider);

	Logger::warn() << "MTV = " << t << "\n";

	t2.move(-t);
	Logger::notify() << "GJK RESULT (AGAIN) = " << std::boolalpha << gjk(simplex, &t2_collider, &floor_collider) << std::endl;
}

// TODO: into linalg
glm::mat4 _mk_model(const glm::vec3 &translation = {0, 0, 0}, const glm::vec3 &scale = {1, 1, 1})
{
	glm::mat4 model = glm::mat4(1.0f);
	model = glm::translate(model, translation);
	model = glm::scale(model, scale);		// TODO: what is the basis of this computation?
	return model;
}

void main_renderer()
{
	// Total time
	static float time = 0;
	static float delta_t = 0;
	static float last_t = 0;

	// Get time stuff
	float current_frame = glfwGetTime();
	delta_t = current_frame - last_t;
	last_t = current_frame;

	/* Update the monitor
	tui::tui.update();
	tui::tui.update_fps(delta_t); */

	// Process input
	process_input(mercury::winman.cwin, delta_t);	// TODO: return movement of camera

	// render
	glClearColor(0.05f, 1.00f, 0.05f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	// TODO: rerender all this only if the camera has moved

	// View and projection matrices
	glm::mat4 view = camera.get_view();
	glm::mat4 projection = glm::perspective(
		glm::radians(camera.zoom),
		winman.width / winman.height,
		0.1f, 100.0f
	);

	// Set lighting daemon uniforms
	ldam.uniforms = {
		_mk_model(),
		view,
		projection,
		camera.position
	};

	// Lighut and render scene
	pdam.update(delta_t);
	ldam.light();
	rdam.render();

	// Draw sphere		TODO: seriously some way to check that uniforms have been set
	sphere_shader.use();	// TODO: make into a common shader
	sphere_shader.set_mat4("model", _mk_model());
	sphere_shader.set_mat4("view", view);
	sphere_shader.set_mat4("projection", projection);

	// Draw skybox
	view = glm::mat4(glm::mat3(camera.get_view()));

	// Set skybox shader properties
	Shader *sshader = winman.cres.sb_shader;
	sshader->use();
	sshader->set_mat4("projection", projection);
	sshader->set_mat4("view", view);

	// Draw bounding boxes
	physics::AABB ab;
	SVA3 box;
	
	ab = rb_collider.aabb();
	box = mesh::wireframe_cuboid(ab.center, ab.size);
	box.color = {1.0, 1.0, 0.5};
	box.draw(&sphere_shader);
	
	ab = t2_collider.aabb();
	box = mesh::wireframe_cuboid(ab.center, ab.size);
	box.color = {1.0, 1.0, 0.5};
	box.draw(&sphere_shader);

	/* if (gjk(&t2_collider, &floor_collider)) {
		hit_cube2.set_material({
			.color = {1.0, 0.0, 0.0}
		});
	} else {
		hit_cube2.set_material({
			.color = {0.0, 0.0, 1.0}
		});
	} */
}

// Program render loop condition
bool rcondition()
{
	return !glfwWindowShouldClose(winman[0]);
}

int main()
{
	// Initialize mercury
	init();
	// tui::tui.init();

	// Set winman bindings
	winman.set_condition(rcondition);

	winman.set_initializer(0, main_initializer);
	winman.set_renderer(0, main_renderer);

	// Render loop
	winman.run();

	// Terminate GLFW
	// tui::tui.deinit();
	glfwTerminate();

	return 0;
}

// process all input: query GLFW whether relevant keys are pressed/released this frame and react accordingly
// ---------------------------------------------------------------------------------------------------------
void process_input(GLFWwindow *window, float delta_t)
{
	if (glfwGetKey(window, GLFW_KEY_BACKSPACE) == GLFW_PRESS)
		glfwSetWindowShouldClose(window, true);

	if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
		// glfwSetWindowShouldClose(window, true);
		glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
	}

	float cameraSpeed = 5 * delta_t;

	// Forward motion
	if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
		camera.move(cameraSpeed * camera.front);
	if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
		camera.move(-cameraSpeed * camera.front);

	// Lateral motion
	if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
		camera.move(-cameraSpeed * camera.right);
	if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
		camera.move(cameraSpeed * camera.right);

	// Vertical motion
	if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS)
		camera.move(-cameraSpeed * camera.up);
	if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS)
		camera.move(cameraSpeed * camera.up);

	// Rotating a box
	if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS)
		t2.rotate(0.05f * glm::vec3(0, 0, 1));

	if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS)
		t2.rotate(-0.05f * glm::vec3(0, 0, 1));
}

// Variables for mouse movement
float lastX = 0.0; // SCR_WIDTH / 2.0f;
float lastY = 0.0; // SCR_HEIGHT / 2.0f;
bool firstMouse = true;

// Mouse callback
void mouse_callback(GLFWwindow* window, double xpos, double ypos)
{
	static const float sensitivity = 0.1f;

	if (firstMouse) {
		lastX = xpos;
		lastY = ypos;
		firstMouse = false;
	}

	float xoffset = xpos - lastX;
	float yoffset = lastY - ypos; // reversed since y-coordinates go from bottom to top

	lastX = xpos;
	lastY = ypos;

	// camera.ProcessMouseMovement(xoffset, yoffset);
	camera.add_yaw(xoffset * sensitivity);
	camera.add_pitch(yoffset * sensitivity);
}

// glfw: whenever the mouse scroll wheel scrolls, this callback is called
// ----------------------------------------------------------------------
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset)
{
	// TODO: method to change zoom
	camera.zoom -= (float)yoffset;
	if (camera.zoom < 1.0f)
		camera.zoom = 1.0f;
	if (camera.zoom > 45.0f)
		camera.zoom = 45.0f;
}
