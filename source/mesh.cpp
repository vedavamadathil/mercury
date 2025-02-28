// Standard headers
#include <thread>

// GLM headers
#include <glm/gtx/rotate_vector.hpp>

// Assimp headers
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

// TinyObjLoader headers
#define TINYOBJLOADER_IMPLEMENTATION

#include <tinyobjloader/tiny_obj_loader.h>

// Engine headers
#include "../include/common.hpp"
#include "../include/core/thread_pool.hpp"
#include "../include/mesh.hpp"
#include "../include/profiler.hpp"

// Global operators
namespace tinyobj {

inline bool operator<(const tinyobj::index_t &a, const tinyobj::index_t &b)
{
	return std::tie(a.vertex_index, a.normal_index, a.texcoord_index)
		< std::tie(b.vertex_index, b.normal_index, b.texcoord_index);
}

inline bool operator==(const tinyobj::index_t &a, const tinyobj::index_t &b)
{
	return std::tie(a.vertex_index, a.normal_index, a.texcoord_index)
		== std::tie(b.vertex_index, b.normal_index, b.texcoord_index);
}

}

namespace std {

template <>
struct hash <tinyobj::index_t>
{
	size_t operator()(const tinyobj::index_t &k) const
	{
		return ((hash<int>()(k.vertex_index)
			^ (hash<int>()(k.normal_index) << 1)) >> 1)
			^ (hash<int>()(k.texcoord_index) << 1);
	}
};

// TODO: please make a common vector type
template <>
struct hash <glm::vec3> {
	size_t operator()(const glm::vec3 &v) const
	{
		return ((hash <float>()(v.x)
			^ (hash <float>()(v.y) << 1)) >> 1)
			^ (hash <float>()(v.z) << 1);
	}
};

template <>
struct hash <glm::vec2> {
	size_t operator()(const glm::vec2 &v) const
	{
		return ((hash <float>()(v.x)
			^ (hash <float>()(v.y) << 1)) >> 1);
	}
};

template <>
struct hash <kobra::Vertex> {
	size_t operator()(kobra::Vertex const &vertex) const
	{
		return ((hash <glm::vec3>()(vertex.position)
			^ (hash <glm::vec3>()(vertex.normal) << 1)) >> 1)
			^ (hash <glm::vec2>()(vertex.tex_coords) << 1);
	}
};

}

namespace kobra {

inline bool operator==(const kobra::Vertex &a, const kobra::Vertex &b)
{
	return a.position == b.position
		&& a.normal == b.normal
		&& a.tex_coords == b.tex_coords;
}

// Generate bounding box for submesh
BoundingBox Submesh::bbox() const
{
	// Create a bounding box
	BoundingBox box {
		.min = glm::vec3(std::numeric_limits <float>::max()),
		.max = glm::vec3(std::numeric_limits <float>::lowest())
	};

	// Add all vertices to the bounding box
	for (const auto &v : vertices) {
		box.min = glm::min(box.min, v.position);
		box.max = glm::max(box.max, v.position);
	}

	return box;
}

// Process submesh vertex data, generate tangents and bitangents
void Submesh::_process_vertex_data()
{
	// TODO: run a compute shader for this...
	auto task = [&](const int &i) {
		// Get the vertex
		Vertex &v = vertices[i];

		// Calculate tangent and bitangent
		v.tangent = glm::vec3(0.0f);
		v.bitangent = glm::vec3(0.0f);

		// Iterate over all faces
		for (int j = 0; j < indices.size(); j += 3) {
			// Get the face
			int face0 = indices[j];
			int face1 = indices[j + 1];
			int face2 = indices[j + 2];

			// Check if the vertex is part of the face
			if (face0 == i || face1 == i || face2 == i) {
				// Get the face vertices
				const auto &v1 = vertices[face0];
				const auto &v2 = vertices[face1];
				const auto &v3 = vertices[face2];

				glm::vec3 e1 = v2.position - v1.position;
				glm::vec3 e2 = v3.position - v1.position;

				glm::vec2 uv1 = v2.tex_coords - v1.tex_coords;
				glm::vec2 uv2 = v3.tex_coords- v1.tex_coords;

				float r = 1.0f / (uv1.x * uv2.y - uv1.y * uv2.x);
				glm::vec3 tangent = (e1 * uv2.y - e2 * uv1.y) * r;
				glm::vec3 bitangent = (e2 * uv1.x - e1 * uv2.x) * r;

				// Add the tangent and bitangent to the vertex
				v.tangent += tangent;
				v.bitangent += bitangent;
			}
		}

		// Normalize the tangent and bitangent
		v.tangent = glm::normalize(v.tangent);
		v.bitangent = glm::normalize(v.bitangent);
	};

	struct _generator {
		int counter = 0;
		int max = 0;

		_generator(int max) : max(max) {}

		std::optional <int> operator()() {
			if (counter < max)
				return counter++;

			return std::nullopt;
		}
	};

	core::run_tasks <int> (task, _generator(vertices.size()));
}

// Submesh modifiers
void Submesh::transform(Submesh &submesh, const Transform &transform)
{
	// Transform vertices
	for (auto &vertex : submesh.vertices) {
		vertex.position = transform.apply(vertex.position);
		vertex.normal = transform.apply_vector(vertex.normal);
		vertex.tangent = transform.apply_vector(vertex.tangent);
		vertex.bitangent = transform.apply_vector(vertex.bitangent);
	}
}

// Submesh factories
Submesh Submesh::sphere(int slices, int stacks)
{
	static const glm::vec3 center {0.0f};
	static const float radius = 1.0f;

	// Vertices and indices
	std::vector <Vertex> vertices;
	std::vector <uint32_t> indices;

	// add top vertex
	glm::vec3 top_vertex {center.x, center.y + radius, center.z};
	vertices.push_back(Vertex {
		top_vertex,
		{0.0f, 1.0f, 0.0f},
		{0.5f, 0.5f}
	});

	// generate vertices in the middle stacks
	for (int i = 0; i < stacks - 1; i++) {
		float phi = glm::pi <float> () * double(i + 1) / stacks;

		// generate vertices in the slice
		for (int j = 0; j < slices; j++) {
			float theta = 2.0f * glm::pi <float> () * double(j) / slices;

			// add vertex
			// todo: utlility function to generate polar and
			// spherical coordinates
			glm::vec3 vertex {
				center.x + radius * glm::sin(phi) * glm::cos(theta),
				center.y + radius * glm::cos(phi),
				center.z + radius * glm::sin(phi) * glm::sin(theta)
			};

			glm::vec3 normal {
				glm::sin(phi) * glm::cos(theta),
				glm::cos(phi),
				glm::sin(phi) * glm::sin(theta)
			};

			glm::vec2 uv {
				double(j) / slices,
				double(i) / stacks
			};

			// add vertex
			vertices.push_back(Vertex {
				vertex,
				normal,
				uv
			});
		}
	}

	// add bottom vertex
	glm::vec3 bottom_vertex {center.x, center.y - radius, center.z};
	vertices.push_back(Vertex {
		bottom_vertex,
		{0.0f, -1.0f, 0.0f},
		{0.5f, 0.5f}
	});

	// top and bottom triangles
	for (int i = 0; i < slices; i++) {
		// corresponding top
		int i0 = i + 1;
		int i1 = (i + 1) % slices + 1;

		indices.push_back(0);
		indices.push_back(i1);
		indices.push_back(i0);

		// corresponding bottom
		i0 = i + slices * (stacks - 2) + 1;
		i1 = (i + 1) % slices + slices * (stacks - 2) + 1;

		indices.push_back(vertices.size() - 1);
		indices.push_back(i0);
		indices.push_back(i1);
	}

	// middle triangles
	for (int i = 0; i < stacks - 2; i++) {
		for (int j = 0; j < slices; j++) {
			int i0 = i * slices + j + 1;
			int i1 = i * slices + (j + 1) % slices + 1;
			int i2 = (i + 1) * slices + (j + 1) % slices + 1;
			int i3 = (i + 1) * slices + j + 1;

			indices.push_back(i0);
			indices.push_back(i1);
			indices.push_back(i2);

			indices.push_back(i0);
			indices.push_back(i2);
			indices.push_back(i3);
		}
	}

	// construct and return the mesh
	return Submesh {vertices, indices};
}

// Mesh
Mesh Mesh::box(const glm::vec3 &center, const glm::vec3 &dim)
{
	float x = dim.x;
	float y = dim.y;
	float z = dim.z;

	// All 24 vertices, with correct normals
	VertexList vertices {
		// Front
		Vertex {{ center.x - x, center.y - y, center.z + z }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f }},
		Vertex {{ center.x + x, center.y - y, center.z + z }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f }},
		Vertex {{ center.x + x, center.y + y, center.z + z }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 1.0f }},
		Vertex {{ center.x - x, center.y + y, center.z + z }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 1.0f }},

		// Back
		Vertex {{ center.x - x, center.y - y, center.z - z }, { 0.0f, 0.0f, -1.0f }, { 0.0f, 0.0f }},
		Vertex {{ center.x + x, center.y - y, center.z - z }, { 0.0f, 0.0f, -1.0f }, { 1.0f, 0.0f }},
		Vertex {{ center.x + x, center.y + y, center.z - z }, { 0.0f, 0.0f, -1.0f }, { 1.0f, 1.0f }},
		Vertex {{ center.x - x, center.y + y, center.z - z }, { 0.0f, 0.0f, -1.0f }, { 0.0f, 1.0f }},

		// Left
		Vertex {{ center.x - x, center.y - y, center.z + z }, { -1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f }},
		Vertex {{ center.x - x, center.y - y, center.z - z }, { -1.0f, 0.0f, 0.0f }, { 1.0f, 0.0f }},
		Vertex {{ center.x - x, center.y + y, center.z - z }, { -1.0f, 0.0f, 0.0f }, { 1.0f, 1.0f }},
		Vertex {{ center.x - x, center.y + y, center.z + z }, { -1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f }},

		// Right
		Vertex {{ center.x + x, center.y - y, center.z + z }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f }},
		Vertex {{ center.x + x, center.y - y, center.z - z }, { 1.0f, 0.0f, 0.0f }, { 1.0f, 0.0f }},
		Vertex {{ center.x + x, center.y + y, center.z - z }, { 1.0f, 0.0f, 0.0f }, { 1.0f, 1.0f }},
		Vertex {{ center.x + x, center.y + y, center.z + z }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f }},

		// Top
		Vertex {{ center.x - x, center.y + y, center.z + z }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f }},
		Vertex {{ center.x + x, center.y + y, center.z + z }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f }},
		Vertex {{ center.x + x, center.y + y, center.z - z }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 1.0f }},
		Vertex {{ center.x - x, center.y + y, center.z - z }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 1.0f }},

		// Bottom
		Vertex {{ center.x - x, center.y - y, center.z + z }, { 0.0f, -1.0f, 0.0f }, { 0.0f, 0.0f }},
		Vertex {{ center.x + x, center.y - y, center.z + z }, { 0.0f, -1.0f, 0.0f }, { 1.0f, 0.0f }},
		Vertex {{ center.x + x, center.y - y, center.z - z }, { 0.0f, -1.0f, 0.0f }, { 1.0f, 1.0f }},
		Vertex {{ center.x - x, center.y - y, center.z - z }, { 0.0f, -1.0f, 0.0f }, { 0.0f, 1.0f }}
	};

	// All 36 indices
	IndexList indices {
		0, 1, 2,	2, 3, 0,	// Front
		4, 6, 5,	6, 4, 7,	// Back
		8, 10, 9,	10, 8, 11,	// Left
		12, 13, 14,	14, 15, 12,	// Right
		16, 17, 18,	18, 19, 16,	// Top
		20, 22, 21,	22, 20, 23	// Bottom
	};

	// TODO: should set source of the mesh to box, then dimensions
	auto out = Submesh {vertices, indices};
	Mesh m = std::vector <Submesh> {out};
	return m;
}

// Plane
Mesh Mesh::plane(const glm::vec3 &center, float width, float height)
{
	glm::vec3 normal = { 0.0f, 0.0f, 1.0f };
	VertexList vertices {
		Vertex {{ center.x - width / 2.0f, center.y, center.z - height / 2.0f }, normal, { 0.0f, 0.0f }},
		Vertex {{ center.x + width / 2.0f, center.y, center.z - height / 2.0f }, normal, { 1.0f, 0.0f }},
		Vertex {{ center.x + width / 2.0f, center.y, center.z + height / 2.0f }, normal, { 1.0f, 1.0f }},
		Vertex {{ center.x - width / 2.0f, center.y, center.z + height / 2.0f }, normal, { 0.0f, 1.0f }}
	};

	IndexList indices {
		0, 1, 2,
		2, 3, 0
	};

	auto out = Submesh {vertices, indices};
	Mesh m = std::vector <Submesh> {out};
	return m;
}

/*
 * TODO: only parameters should be slices and stacks (transform will do the
 * rest)
kmesh kmesh::make_sphere(const glm::vec3 &center, float radius, int slices, int stacks)
{
	// vertices and indices
	vertexlist vertices;
	indexlist indices;

	// add top vertex
	glm::vec3 top_vertex {center.x, center.y + radius, center.z};
	vertices.push_back(vertex {
		top_vertex,
		{0.0f, 1.0f, 0.0f},
		{0.5f, 0.5f}
	});

	// generate vertices in the middle stacks
	for (int i = 0; i < stacks - 1; i++) {
		float phi = glm::pi <float> () * double(i + 1) / stacks;

		// generate vertices in the slice
		for (int j = 0; j < slices; j++) {
			float theta = 2.0f * glm::pi <float> () * double(j) / slices;

			// add vertex
			// todo: utlility function to generate polar and
			// spherical coordinates
			glm::vec3 vertex {
				center.x + radius * glm::sin(phi) * glm::cos(theta),
				center.y + radius * glm::cos(phi),
				center.z + radius * glm::sin(phi) * glm::sin(theta)
			};

			glm::vec3 normal {
				glm::sin(phi) * glm::cos(theta),
				glm::cos(phi),
				glm::sin(phi) * glm::sin(theta)
			};

			glm::vec2 uv {
				double(j) / slices,
				double(i) / stacks
			};

			// add vertex
			vertices.push_back(vertex {
				vertex,
				normal,
				uv
			});
		}
	}

	// add bottom vertex
	glm::vec3 bottom_vertex {center.x, center.y - radius, center.z};
	vertices.push_back(vertex {
		bottom_vertex,
		{0.0f, -1.0f, 0.0f},
		{0.5f, 0.5f}
	});

	// top and bottom triangles
	for (int i = 0; i < slices; i++) {
		// corresponding top
		int i0 = i + 1;
		int i1 = (i + 1) % slices + 1;

		indices.push_back(0);
		indices.push_back(i1);
		indices.push_back(i0);

		// corresponding bottom
		i0 = i + slices * (stacks - 2) + 1;
		i1 = (i + 1) % slices + slices * (stacks - 2) + 1;

		indices.push_back(vertices.size() - 1);
		indices.push_back(i0);
		indices.push_back(i1);
	}

	// middle triangles
	for (int i = 0; i < stacks - 2; i++) {
		for (int j = 0; j < slices; j++) {
			int i0 = i * slices + j + 1;
			int i1 = i * slices + (j + 1) % slices + 1;
			int i2 = (i + 1) * slices + (j + 1) % slices + 1;
			int i3 = (i + 1) * slices + j + 1;

			indices.push_back(i0);
			indices.push_back(i1);
			indices.push_back(i2);

			indices.push_back(i0);
			indices.push_back(i2);
			indices.push_back(i3);
		}
	}

	// construct and return the mesh
	return kmesh {vertices, indices};
} */

/* create a ring
kmesh kmesh::make_ring(const glm::vec3 &center, float radius, float width, float height, int slices)
{
	// vertices and indices
	vertexlist vertices;
	indexlist indices;

	// add vertices
	for (int i = 0; i < slices; i++) {
		float theta = 2.0f * glm::pi <float> () * double(i) / slices;
		float iradius = radius - width / 2.0f;
		float oradius = radius + width / 2.0f;

		// upper-inner ring
		vertex u_inner {
			{
				center.x + iradius * glm::sin(theta),
				center.y + height / 2.0f,
				center.z + iradius * glm::cos(theta)
			},
			{0.0f, 1.0f, 0.0f},
			{double(i) / slices, 0.0f}
		};

		// upper-outer ring
		vertex u_outer {
			{
				center.x + oradius * glm::sin(theta),
				center.y + height / 2.0f,
				center.z + oradius * glm::cos(theta)
			},
			{0.0f, 1.0f, 0.0f},
			{double(i) / slices, 1.0f}
		};

		// lower-inner ring
		vertex l_inner {
			{
				center.x + iradius * glm::sin(theta),
				center.y - height / 2.0f,
				center.z + iradius * glm::cos(theta)
			},
			{0.0f, -1.0f, 0.0f},
			{double(i) / slices, 0.0f}
		};

		// lower-outer ring
		vertex l_outer {
			{
				center.x + oradius * glm::sin(theta),
				center.y - height / 2.0f,
				center.z + oradius * glm::cos(theta)
			},
			{0.0f, -1.0f, 0.0f},
			{double(i) / slices, 1.0f}
		};

		// add vertices
		vertices.push_back(u_inner);
		vertices.push_back(u_outer);
		vertices.push_back(l_inner);
		vertices.push_back(l_outer);
	}

	// add indices
	for (int i = 0; i < slices; i++) {
		// next index
		int n = (i + 1) % slices;

		// relevant indices
		uint i0 = 4 * i;
		uint i1 = i0 + 1;
		uint i2 = i0 + 2;
		uint i3 = i0 + 3;

		uint i4 = 4 * n;
		uint i5 = i4 + 1;
		uint i6 = i4 + 2;
		uint i7 = i4 + 3;

		// group indices
		std::vector <uint> upper_quad {
			i0, i4, i5,
			i0, i5, i1
		};

		std::vector <uint> lower_quad {
			i2, i6, i7,
			i2, i7, i3
		};

		std::vector <uint> outer_mid {
			i1, i5, i3,
			i3, i7, i5
		};

		std::vector <uint> inner_mid {
			i0, i4, i2,
			i2, i6, i4
		};

		// add indices
		indices.insert(indices.end(), upper_quad.begin(), upper_quad.end());
		indices.insert(indices.end(), lower_quad.begin(), lower_quad.end());
		indices.insert(indices.end(), outer_mid.begin(), outer_mid.end());
		indices.insert(indices.end(), inner_mid.begin(), inner_mid.end());
	}

	// construct and return the mesh
	return kmesh {vertices, indices};
} */

// TODO: in math.hpp
glm::vec3 project(const glm::vec3 &point, const glm::vec3 &normal)
{
	return point - glm::dot(point, normal) * normal;
}

// Unit cylinder
Submesh Submesh::cylinder(int resolution)
{
	// Height is 1, radius is 0.5
	glm::vec3 c1 = glm::vec3 {0, 0.5, 0};
	glm::vec3 c2 = glm::vec3 {0, -0.5, 0};
	float r = 0.5f;

	// Vertices and indices
	std::vector <Vertex> vertices;
	std::vector <uint32_t> indices;

	// Top face
	glm::vec3 ntop = glm::vec3 {0, 1, 0};
	for (int i = 0; i < resolution; i++) {
		float theta = 2.0f * glm::pi <float> () * double(i) / resolution;
		glm::vec3 p = c1 + r * glm::vec3 {glm::sin(theta), 0, glm::cos(theta)};
		glm::vec2 t = glm::vec2 {double(i) / resolution, 0.0f};
		vertices.push_back({p, ntop, t});
	}

	for (int i = 0; i < resolution; i++) {
		indices.push_back(0);
		indices.push_back(i);
		indices.push_back((i + 1) % resolution);
	}

	// Bottom face
	int offset = vertices.size();
	glm::vec3 nbottom = glm::vec3 {0, -1, 0};
	for (int i = 0; i < resolution; i++) {
		float theta = 2.0f * glm::pi <float> () * double(i) / resolution;
		glm::vec3 p = c2 + r * glm::vec3 {glm::sin(theta), 0, glm::cos(theta)};
		glm::vec2 t = glm::vec2 {double(i) / resolution, 1.0f};
		vertices.push_back({p, nbottom, t});
	}

	for (int i = 0; i < resolution; i++) {
		indices.push_back(offset);
		indices.push_back(offset + (i + 1) % resolution);
		indices.push_back(offset + i);
	}

	// Lateral faces
	offset = vertices.size();
	for (int i = 0; i < resolution; i++) {
		// Each face (two triangles) is made of four vertices
		int i0 = offset + 4 * i;
		int i1 = i0 + 1;
		int i2 = i0 + 2;
		int i3 = i0 + 3;

		// Vertex positions
		float theta1 = 2.0f * glm::pi <float> () * double(i) / resolution;
		float theta2 = 2.0f * glm::pi <float> () * double(i + 1) / resolution;

		glm::vec3 p0 = c1 + r * glm::vec3 {glm::sin(theta1), 0, glm::cos(theta1)};
		glm::vec3 p1 = c1 + r * glm::vec3 {glm::sin(theta2), 0, glm::cos(theta2)};
		glm::vec3 p2 = c2 + r * glm::vec3 {glm::sin(theta1), 0, glm::cos(theta1)};
		glm::vec3 p3 = c2 + r * glm::vec3 {glm::sin(theta2), 0, glm::cos(theta2)};

		// Vertex normals
		glm::vec3 n0 = glm::normalize(p0 - c1);
		glm::vec3 n1 = glm::normalize(p1 - c1);
		glm::vec3 n2 = glm::normalize(p2 - c2);
		glm::vec3 n3 = glm::normalize(p3 - c2);

		// Vertex texture coordinates
		glm::vec2 t0 = glm::vec2 {double(i) / resolution, 0.5f};
		glm::vec2 t1 = glm::vec2 {double(i + 1) / resolution, 0.5f};
		glm::vec2 t2 = glm::vec2 {double(i) / resolution, 0.0f};
		glm::vec2 t3 = glm::vec2 {double(i + 1) / resolution, 0.0f};

		// Add vertices
		vertices.push_back({p0, n0, t0});
		vertices.push_back({p1, n1, t1});
		vertices.push_back({p2, n2, t2});
		vertices.push_back({p3, n3, t3});

		// Add indices
		indices.push_back(i0);
		indices.push_back(i2);
		indices.push_back(i1);

		indices.push_back(i1);
		indices.push_back(i2);
		indices.push_back(i3);
	}

	// Construct and return the mesh
	return Submesh {vertices, indices};
}

// Unit cone
Submesh Submesh::cone(int resolution)
{
	// Height is 1, radius is 0.5
	glm::vec3 center = glm::vec3 {0, -0.5, 0};
	glm::vec3 top = glm::vec3 {0, 0.5, 0};

	float radius = 0.5f;

	// Vertices and indices
	std::vector <Vertex> vertices;
	std::vector <uint32_t> indices;

	// First normal, rotate for each vertex
	glm::vec3 n0 = glm::normalize(glm::vec3 {0, 0.4, 0.8});

	// Generate lateral face
	for (int i = 0; i < resolution; i++) {
		// Each triangle individually
		int i0 = 3 * i;
		int i1 = i0 + 1;
		int i2 = i0 + 2;

		// Angles
		float theta1 = 2.0f * glm::pi <float> () * double(i) / resolution;
		float theta2 = 2.0f * glm::pi <float> () * double(i + 1) / resolution;

		// Positions
		glm::vec3 p1 = center + radius * glm::vec3 {glm::sin(theta1), 0, glm::cos(theta1)};
		glm::vec3 p2 = center + radius * glm::vec3 {glm::sin(theta2), 0, glm::cos(theta2)};

		// Normals: rotate n0
		glm::vec3 n1 = glm::rotate(n0, theta1, glm::vec3 {0, 1, 0});
		glm::vec3 n2 = glm::rotate(n0, theta2, glm::vec3 {0, 1, 0});
		glm::vec3 ntop = (n1 + n2)/2.0f;

		// Texture coordinates
		glm::vec2 t1 = glm::vec2 {double(i) / resolution, 0};
		glm::vec2 t2 = glm::vec2 {double(i + 1) / resolution, 0};
		glm::vec2 ttop = glm::vec2 {0.5f, 1.0f};

		// Add vertices
		vertices.push_back(Vertex {p1, n1, t1});
		vertices.push_back(Vertex {p2, n2, t2});
		vertices.push_back(Vertex {top, ntop, ttop});

		// Add indices
		indices.push_back(i0);
		indices.push_back(i1);
		indices.push_back(i2);
	}

	// Generate bottom face
	glm::vec3 normal = glm::vec3 {0, -1, 0};

	int offset = vertices.size();

	vertices.push_back(Vertex {center, normal, glm::vec2 {0, 0}});
	for (int i = 0; i < resolution; i++) {
		float theta = 2.0f * glm::pi <float> () * double(i) / resolution;
		glm::vec3 position = glm::vec3 {
			center.x + radius * glm::sin(theta),
			center.y,
			center.z + radius * glm::cos(theta)
		};

		vertices.push_back(Vertex {position, normal, glm::vec2 {0, 0}});
	}

	for (int i = 0; i < resolution; i++) {
		int oi = offset + i + 1;
		int on = offset + 1 + (i + 1) % resolution;

		indices.push_back(offset);
		indices.push_back(on);
		indices.push_back(oi);
	}

	// Construct and return the mesh
	return Submesh {vertices, indices};
}

namespace assimp {

static std::tuple <Submesh, Material> process_mesh(aiMesh *mesh, const aiScene *scene, const std::string &dir)
{
	KOBRA_PROFILE_TASK("Assimp process mesh");

	// Mesh data
	VertexList vertices;
        std::vector <uint32_t> indices;

	// Process all the mesh's vertices
	for (size_t i = 0; i < mesh->mNumVertices; i++) {
		// Create a new vertex
		Vertex v;

		// Vertex position
		v.position = {
			mesh->mVertices[i].x,
			mesh->mVertices[i].y,
			mesh->mVertices[i].z
		};

		// Vertex normal
		if (mesh->HasNormals()) {
			v.normal = {
				mesh->mNormals[i].x,
				mesh->mNormals[i].y,
				mesh->mNormals[i].z
			};
		}

		// Vertex texture coordinates
		if (mesh->HasTextureCoords(0)) {
			v.tex_coords = {
				mesh->mTextureCoords[0][i].x,
				mesh->mTextureCoords[0][i].y
			};
		}


		// TODO: material?

		vertices.push_back(v);
	}

	// Process all the mesh's indices
	for (size_t i = 0; i < mesh->mNumFaces; i++) {
		// Get the face
		aiFace face = mesh->mFaces[i];

		// Process all the face's indices
		for (size_t j = 0; j < face.mNumIndices; j++)
			indices.push_back(face.mIndices[j]);
	}

	// Material
	Material mat;
	aiMaterial *material = scene->mMaterials[mesh->mMaterialIndex];

        mat.name = material->GetName().C_Str();

	// Get diffuse
	aiString path;
	if (material->GetTexture(aiTextureType_DIFFUSE, 0, &path) == AI_SUCCESS) {
		mat.diffuse_texture = path.C_Str();
		mat.diffuse_texture = common::resolve_path(path.C_Str(), {dir});
		// assert(!mat.diffuse_texture.empty());
	} else {
		aiColor3D diffuse;
		material->Get(AI_MATKEY_COLOR_DIFFUSE, diffuse);
		mat.diffuse = {diffuse.r, diffuse.g, diffuse.b};
	}

	// Get normal texture
	if (material->GetTexture(aiTextureType_NORMALS, 0, &path) == AI_SUCCESS) {
		mat.normal_texture = path.C_Str();
		mat.normal_texture = common::resolve_path(path.C_Str(), {dir});
		// assert(!mat.normal_texture.empty());
	}

	// Get specular
	aiColor3D specular;
	material->Get(AI_MATKEY_COLOR_SPECULAR, specular);
	mat.specular = {specular.r, specular.g, specular.b};

	// Get shininess
	float shininess;
	material->Get(AI_MATKEY_SHININESS, shininess);
	// mat.shininess = shininess;
	mat.roughness = 1 - shininess/1000.0f;

	// Push the material to the material list
	// uint32_t mat_index = Material::all.size();
	// Material::all.push_back(mat);

	return { Submesh { vertices, indices, -1 }, mat };
}

static std::tuple <Mesh, std::vector <Material>> process_node(aiNode *node, const aiScene *scene, const std::string &dir)
{
	// Process all the node's meshes (if any)
	std::vector <Submesh> submeshes;
        std::vector <Material> materials;

	for (size_t i = 0; i < node->mNumMeshes; i++) {
		aiMesh *mesh = scene->mMeshes[node->mMeshes[i]];

                auto [submesh, material] = process_mesh(mesh, scene, dir);
		submeshes.push_back(submesh);
                materials.push_back(material);
	}

	// Recusively process all the node's children
	// TODO: multithreaded task system (using a threadpool, etc) in core/
	// std::vector <Mesh> children;

	for (size_t i = 0; i < node->mNumChildren; i++) {
		auto [m, mats] = process_node(node->mChildren[i], scene, dir);

		for (size_t j = 0; j < m.submeshes.size(); j++)
			submeshes.push_back(m.submeshes[j]);

                materials.insert(materials.end(), mats.begin(), mats.end());
	}

	return { Mesh { submeshes }, materials };
}

std::optional <std::tuple <Mesh, std::vector <Material>>> load_mesh(const std::string &path)
{
	KOBRA_PROFILE_TASK("Assimp load mesh");

	// Create the Assimp importer
	Assimp::Importer importer;

	// Read scene
	const aiScene *scene = importer.ReadFile(
		path, aiProcess_Triangulate
			| aiProcess_GenNormals
			| aiProcess_FlipUVs
	);

	// Check if the scene was loaded
	if (!scene | scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE
			|| !scene->mRootNode) {
		KOBRA_LOG_FUNC(Log::ERROR) << "Assimp error: "
			<< importer.GetErrorString() << std::endl;
		return std::nullopt;
	}

	// Process the scene (root node)
	return process_node(scene->mRootNode, scene, common::get_directory(path));
}

}

namespace tinyobjloader {

// TODO: alias for std::optional <std::tuple <Mesh, std::vector <Material>>>
std::optional <std::tuple <Mesh, std::vector <Material>>> load_mesh(const std::string &path)
{
	KOBRA_PROFILE_TASK("Loading mesh");

	// Loader configuration
	tinyobj::ObjReaderConfig reader_config;
	reader_config.mtl_search_path = common::get_directory(path);

	// Loader
	tinyobj::ObjReader reader;

	{
		KOBRA_PROFILE_TASK("Loading mesh: reading file");

		// Load the mesh
		if (!reader.ParseFromFile(path, reader_config)) {
			KOBRA_LOG_FUNC(Log::ERROR) << "TinyObjLoader error: "
				<< reader.Error() << std::endl;
			return {};
		}

		// Warnings
		// TODO: cusotm logger headers (like optix)
		if (!reader.Warning().empty())
			KOBRA_LOG_FUNC(Log::WARN) << reader.Warning() << std::endl;

	}

	// Get the mesh properties
	auto &attrib = reader.GetAttrib();
	auto &shapes = reader.GetShapes();
	auto &materials = reader.GetMaterials();

	// Load submeshes
	std::vector <Submesh> submeshes;
        std::vector <Material> mats;

	{
		KOBRA_PROFILE_TASK("Loading mesh: Loading submeshes");

		std::mutex submeshes_mutex;

		core::TaskQueue tasks;
		for (int i = 0; i < shapes.size(); i++) {
			core::Task task = [&, i]() {
				// Get the mesh
				auto &mesh = shapes[i].mesh;

				std::vector <Vertex> vertices;
				std::vector <uint32_t> indices;

				std::unordered_map <Vertex, uint32_t> unique_vertices;
				std::unordered_map <tinyobj::index_t, uint32_t> index_map;

				int offset = 0;
				for (int f = 0; f < mesh.num_face_vertices.size(); f++) {
					// Get the number of vertices in the face
					int fv = mesh.num_face_vertices[f];

					// Loop over vertices in the face
					for (int v = 0; v < fv; v++) {
						// Get the vertex index
						tinyobj::index_t index = mesh.indices[offset + v];

						if (index_map.count(index) > 0) {
							indices.push_back(index_map[index]);
							continue;
						}

						Vertex vertex;

						vertex.position = {
							attrib.vertices[3 * index.vertex_index + 0],
							attrib.vertices[3 * index.vertex_index + 1],
							attrib.vertices[3 * index.vertex_index + 2]
						};

						if (index.normal_index >= 0) {
							vertex.normal = {
								attrib.normals[3 * index.normal_index + 0],
								attrib.normals[3 * index.normal_index + 1],
								attrib.normals[3 * index.normal_index + 2]
							};
						} else {
							// Compute geometric normal with
							// respect to this face

							// TODO: method
							int pindex = (v - 1 + fv) % fv;
							int nindex = (v + 1) % fv;

							tinyobj::index_t p = mesh.indices[offset + pindex];
							tinyobj::index_t n = mesh.indices[offset + nindex];

							glm::vec3 vn = {
								attrib.vertices[3 * p.vertex_index + 0],
								attrib.vertices[3 * p.vertex_index + 1],
								attrib.vertices[3 * p.vertex_index + 2]
							};

							glm::vec3 vp = {
								attrib.vertices[3 * n.vertex_index + 0],
								attrib.vertices[3 * n.vertex_index + 1],
								attrib.vertices[3 * n.vertex_index + 2]
							};

							glm::vec3 e1 = vp - vertex.position;
							glm::vec3 e2 = vn - vertex.position;

							vertex.normal = glm::normalize(glm::cross(e1, e2));
						}

						if (index.texcoord_index >= 0) {
							vertex.tex_coords = {
								attrib.texcoords[2 * index.texcoord_index + 0],
								1 - attrib.texcoords[2 * index.texcoord_index + 1]
							};
						} else {
							vertex.tex_coords = {0.0f, 0.0f};
						}

						// Add the vertex to the list
						// vertices.push_back(vertex);
						// indices.push_back(vertices.size() - 1);

						// Add the vertex
						uint32_t id;
						if (unique_vertices.count(vertex) > 0) {
							id = unique_vertices[vertex];
						} else {
							id = vertices.size();
							unique_vertices[vertex] = id;
							vertices.push_back(vertex);
						}

						index_map[index] = id;
						indices.push_back(id);
					}

					// Update the offset
					offset += fv;

					// If last face, or material changes
					// push back the submesh
					if (f == mesh.num_face_vertices.size() - 1 ||
							mesh.material_ids[f] != mesh.material_ids[f + 1]) {
						// Material
						Material mat;

						// TODO: method
						if (mesh.material_ids[f] < materials.size()) {
							tinyobj::material_t m = materials[mesh.material_ids[f]];
                                                        mat.name = m.name;
							mat.diffuse = {m.diffuse[0], m.diffuse[1], m.diffuse[2]};
							mat.specular = {m.specular[0], m.specular[1], m.specular[2]};
							// mat.ambient = {m.ambient[0], m.ambient[1], m.ambient[2]};
							mat.emission = {m.emission[0], m.emission[1], m.emission[2]};

							// Check emission
							if (length(mat.emission) > 0.0f) {
								std::cout << "Emission: " << mat.emission.x << ", " << mat.emission.y << ", " << mat.emission.z << std::endl;
								mat.type = eEmissive;
							}

							// Surface properties
							// mat.shininess = m.shniness;
							// mat.roughness = sqrt(2.0f / (mat.shininess + 2.0f));
							mat.roughness = glm::clamp(1.0f - m.shininess/1000.0f, 1e-3f, 0.999f);
							mat.refraction = m.ior;

							// TODO: handle types of rays/materials
							// in the shader
							switch (m.illum) {
							/* case 4:
								mat.type = Shading::eTransmission;
								break; */
							case 7:
								mat.type = eTransmission;
								break;
							}

							// Albedo texture
							if (!m.diffuse_texname.empty()) {
								mat.diffuse_texture = m.diffuse_texname;
								mat.diffuse_texture = common::resolve_path(
									m.diffuse_texname, {reader_config.mtl_search_path}
								);
							}

							// Normal texture
							if (!m.normal_texname.empty()) {
								mat.normal_texture = m.normal_texname;
								mat.normal_texture = common::resolve_path(
									m.normal_texname, {reader_config.mtl_search_path}
								);
							}

							// Specular texture
							if (!m.specular_texname.empty()) {
								mat.specular_texture = m.specular_texname;
								mat.specular_texture = common::resolve_path(
									m.specular_texname, {reader_config.mtl_search_path}
								);
							}

							// Emission texture
							if (!m.emissive_texname.empty()) {
								mat.emission_texture = m.emissive_texname;
								mat.emission_texture = common::resolve_path(
									m.emissive_texname, {reader_config.mtl_search_path}
								);

								mat.type = eEmissive;
							}
						}

						// Push the material
						// uint32_t mat_index = Material::all.size();
						// Material::all.push_back(mat);
                                                mats.push_back(mat);

						// Add submesh
						submeshes_mutex.lock();
						submeshes.push_back(Submesh { vertices, indices, -1 });
						submeshes_mutex.unlock();

						// Clear the vertices and indices
						unique_vertices.clear();
						index_map.clear();
						vertices.clear();
						indices.clear();
					}
				}

			};

			tasks.push(task);
		}

		core::run_tasks(tasks, 1);

	}

	return {{ Mesh { submeshes }, mats }};
}

}

// Load mesh from file
std::optional <std::tuple <Mesh, std::vector <Material>>> Mesh::load(const std::string &path)
{
	// Special cases
	// if (path == "box")
	// 	return box({0, 0, 0}, {0.5, 0.5, 0.5});

	// Check if the file exists
	std::ifstream file(path);
	if (!file.is_open()) {
		KOBRA_LOG_FUNC(Log::ERROR) << "Could not open file: " << path << std::endl;
		return {};
	}

	/* Check if cached
	// TODO: central filesystem manager for caching, etc
	std::string filename = ".kobra/cached/" + common::get_filename(path) + ".cache";
	// TODO: create this directory if it doesn't exist
	if (common::file_exists(filename))
		return Mesh::cache_load(filename); */

	// Load the mesh
	// TODO: use filesystem C++
	std::string ext = common::file_extension(path);
	std::cout << "Loading mesh: " << path << " - " << ext << std::endl;

	// TODO: sphere primitives...
	std::optional <std::tuple <Mesh, std::vector <Material>>> opt;
	if (ext == "obj") // TODO: fix this for smooth normals (option...)
		opt = tinyobjloader::load_mesh(path);
	else
		opt = assimp::load_mesh(path);

	if (!opt.has_value()) {
		KOBRA_LOG_FUNC(Log::ERROR) << "Could not load mesh: " << path << std::endl;
		return {};
	}

	// Mesh m = std::get <0> (opt.value());
	// m._source = path; // TODO: deprecate this field

	// KOBRA_LOG_FUNC(Log::INFO) << "Loaded mesh with " << m.submeshes.size()
	// 	<< " submeshes (#verts = " << m.vertices() << ", #triangles = "
	// 	<< m.triangles() << "), from " << path << std::endl;

	// Cache the mesh
	// Mesh::cache_save(m, filename);

	return opt;
}

/* Cache mesh data to file
// TODO: cache load entire files, store materials in a separate file
// TODO: cache dir per scene
void Mesh::cache_save(const Mesh &mesh, const std::string &path)
{
	// TODO: ensure that the directory is created
	std::ofstream file(path, std::ios::binary);

	// Write the number of submeshes
	uint32_t num_submeshes = mesh.submeshes.size();
	file.write((char *) &num_submeshes, sizeof(uint32_t));

	// Write data offsets
	uint32_t offset = file.tellp();
	offset += sizeof(uint32_t) * num_submeshes;

	for (const Submesh &submesh : mesh.submeshes) {
		file.write((char *) &offset, sizeof(uint32_t));

		offset += sizeof(uint32_t) + submesh.vertices.size() * sizeof(Vertex);
		offset += sizeof(uint32_t) + submesh.indices.size() * sizeof(uint32_t);

		offset += sizeof(glm::vec3) * 4;
		offset += sizeof(float) * 3;
		offset += sizeof(int) + submesh.material.diffuse_texture.size();
		offset += sizeof(int) + submesh.material.normal_texture.size();
		offset += sizeof(int) + submesh.material.specular_texture.size();
		offset += sizeof(int) + submesh.material.emission_texture.size();
		offset += sizeof(int) + submesh.material.roughness_texture.size();

		offset += sizeof(Shading);
	}

	for (const Submesh &s : mesh.submeshes) {
		// TODO: static kernel function...
		// Write the number of vertices
		uint32_t num_vertices = s.vertices.size();
		file.write((char *) &num_vertices, sizeof(uint32_t));

		// Write the vertices
		file.write((char *) s.vertices.data(), sizeof(Vertex) * num_vertices);

		// Write the number of indices
		uint32_t num_indices = s.indices.size();
		file.write((char *) &num_indices, sizeof(uint32_t));

		// Write the indices
		file.write((char *) s.indices.data(), sizeof(uint32_t) * num_indices);

		// Write the material
		file.write((char *) &s.material.diffuse, sizeof(glm::vec3));
		file.write((char *) &s.material.specular, sizeof(glm::vec3));
		file.write((char *) &s.material.emission, sizeof(glm::vec3));
		file.write((char *) &s.material.ambient, sizeof(glm::vec3));

		file.write((char *) &s.material.shininess, sizeof(float));
		file.write((char *) &s.material.roughness, sizeof(float));
		file.write((char *) &s.material.refraction, sizeof(float));

		// TODO: organize so that errors are not as prone
		int tex_albedo_length = s.material.diffuse_texture.length();
		file.write((char *) &tex_albedo_length, sizeof(int));
		file.write(s.material.diffuse_texture.c_str(), tex_albedo_length);

		int tex_normal_length = s.material.normal_texture.length();
		file.write((char *) &tex_normal_length, sizeof(int));
		file.write(s.material.normal_texture.c_str(), tex_normal_length);

		int tex_specular_length = s.material.specular_texture.length();
		file.write((char *) &tex_specular_length, sizeof(int));
		file.write(s.material.specular_texture.c_str(), tex_specular_length);

		int tex_emission_length = s.material.emission_texture.length();
		file.write((char *) &tex_emission_length, sizeof(int));
		file.write(s.material.emission_texture.c_str(), tex_emission_length);

		int tex_roughness_length = s.material.roughness_texture.length();
		file.write((char *) &tex_roughness_length, sizeof(int));
		file.write(s.material.roughness_texture.c_str(), tex_roughness_length);

		file.write((char *) &s.material.type, sizeof(Shading));
	}

	file.close();
}

// Load mesh from cache file
std::optional <Mesh> Mesh::cache_load(const std::string &path)
{
	KOBRA_PROFILE_FUNCTION();

	std::vector <char> data;

	{
		KOBRA_PROFILE_TASK(Reading file binary data);

		std::ifstream file(path, std::ios::binary);
		if (!file.is_open()) {
			KOBRA_LOG_FUNC(Log::ERROR) << "Could not open cache file: "
				<< path << std::endl;
			return {};
		}

		// Read all the data at once
		data = std::vector <char> (
			(std::istreambuf_iterator <char> (file)),
			std::istreambuf_iterator <char> ()
		);

		file.close();
	}

	// Read the number of submeshes
	uint32_t num_submeshes = *(uint32_t *) data.data();
	std::cout << "Reading " << num_submeshes << " submeshes from cache file @" << path << std::endl;

	// Read offsets
	std::vector <uint32_t> offsets(num_submeshes);
	std::memcpy(offsets.data(), data.data() + sizeof(uint32_t), sizeof(uint32_t) * num_submeshes);

	std::vector <Submesh> submeshes;
	submeshes.reserve(num_submeshes);

	// Extracting tasks
	core::TaskQueue tasks;

	std::mutex submeshes_mutex;
	for (int i = 0; i < num_submeshes; i++) {
		// TODO: static method kernel...
		core::Task task = [i, &offsets, &data, &submeshes, &submeshes_mutex]() {
			// Read the number of vertices
			uint32_t offset = offsets[i];
			uint32_t num_vertices = *(uint32_t *) &data[offset];
			offset += sizeof(uint32_t);

			// Read the vertices
			std::vector <Vertex> vertices;
			vertices.resize(num_vertices);
			std::memcpy(vertices.data(), &data[offset], sizeof(Vertex) * num_vertices);
			offset += sizeof(Vertex) * num_vertices;

			// Read the number of indices
			uint32_t num_indices = *(uint32_t *) &data[offset];
			offset += sizeof(uint32_t);

			// Read the indices
			std::vector <uint32_t> indices;
			indices.resize(num_indices);
			std::memcpy(indices.data(), &data[offset], sizeof(uint32_t) * num_indices);
			offset += sizeof(uint32_t) * num_indices;

			// Read the material
			Material material;

			material.diffuse = *(glm::vec3 *) &data[offset];
			offset += sizeof(glm::vec3);

			material.specular = *(glm::vec3 *) &data[offset];
			offset += sizeof(glm::vec3);

			material.emission = *(glm::vec3 *) &data[offset];
			offset += sizeof(glm::vec3);

			material.ambient = *(glm::vec3 *) &data[offset];
			offset += sizeof(glm::vec3);

			material.shininess = *(float *) &data[offset];
			offset += sizeof(float);

			material.roughness = *(float *) &data[offset];
			offset += sizeof(float);

			material.refraction = *(float *) &data[offset];
			offset += sizeof(float);

			int tex_albedo_length = *(int *) &data[offset];
			offset += sizeof(int);
			material.diffuse_texture = std::string(&data[offset], tex_albedo_length);
			offset += tex_albedo_length;

			int tex_normal_length = *(int *) &data[offset];
			offset += sizeof(int);
			material.normal_texture = std::string(&data[offset], tex_normal_length);
			offset += tex_normal_length;

			int tex_specular_length = *(int *) &data[offset];
			offset += sizeof(int);
			material.specular_texture = std::string(&data[offset], tex_specular_length);
			offset += tex_specular_length;

			int tex_emission_length = *(int *) &data[offset];
			offset += sizeof(int);
			material.emission_texture = std::string(&data[offset], tex_emission_length);
			offset += tex_emission_length;

			int tex_roughness_length = *(int *) &data[offset];
			offset += sizeof(int);
			material.roughness_texture = std::string(&data[offset], tex_roughness_length);
			offset += tex_roughness_length;

			material.type = *(Shading *) &data[offset];
			offset += sizeof(Shading);

			// Create the submesh
			submeshes_mutex.lock();

			submeshes.push_back(
				Submesh {
					vertices, indices,
					material, false
				}
			);

			submeshes_mutex.unlock();
		};

		tasks.push(task);
	}

	{
		KOBRA_PROFILE_TASK(Extracting submesh data);
		core::run_tasks(tasks);
	}

	return Mesh {submeshes};
} */

}
