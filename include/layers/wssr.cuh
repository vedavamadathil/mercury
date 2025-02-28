#ifndef KOBRA_LAYERS_WSSR_H_
#define KOBRA_LAYERS_WSSR_H_

// Standard headers
#include <map>
#include <random>
#include <vector>

// OptiX headers
#include <optix.h>
#include <optix_stubs.h>

// Engine headers
#include "../amadeus/system.cuh"
#include "../backend.hpp"
#include "../core/async.hpp"
#include "../core/kd.cuh"
#include "../optix/parameters.cuh"
#include "../optix/sbt.cuh"
#include "../timer.hpp"
#include "../vertex.hpp"
#include "wssr_grid_parameters.cuh"
#include "mesh_memory.hpp"

namespace kobra {

// Forward declarations
class ECS;
class Camera;
class Transform;
class Renderable;

namespace layers {

struct GridBasedReservoirs {
	// Raytracing backend
	std::shared_ptr <amadeus::System> m_system;
	std::shared_ptr <MeshMemory> m_mesh_memory;

	// Critical Vulkan structures
	vk::raii::Device *device = nullptr;
	vk::raii::PhysicalDevice *phdev = nullptr;
	vk::raii::DescriptorPool *descriptor_pool = nullptr;

	TextureLoader *m_texture_loader = nullptr;

	// CUDA launch stream
	CUstream optix_stream = 0;

	// Vulkan structures
	vk::Extent2D extent = { 0, 0 };

	// Optix structures
	OptixDeviceContext optix_context = nullptr;
	OptixPipeline optix_pipeline = nullptr;

	OptixShaderBindingTable sampling_sbt = {};
	OptixShaderBindingTable eval_sbt = {};

	// OptiX modules
	// TODO: we should delete this?
	OptixModule optix_module = nullptr;

	// Program groups
	struct {
		OptixProgramGroup sampling_raygen = nullptr;
		OptixProgramGroup sampling_hit = nullptr;

		OptixProgramGroup raygen = nullptr;
		OptixProgramGroup miss = nullptr;
		OptixProgramGroup hit = nullptr;

		OptixProgramGroup shadow_miss = nullptr;
		OptixProgramGroup shadow_hit = nullptr;
	} optix_programs;

	// Launch parameters
	optix::GridBasedReservoirsParameters launch_params;

	CUdeviceptr launch_params_buffer = 0;

	// Host buffer analogues
	// TODO: common algorithm for BVH construction...
	struct {
		std::vector <optix::QuadLight> quad_lights;
		std::vector <optix::TriangleLight> tri_lights;
	} host;

	// Timer
	Timer timer;

	// Default constructor
	GridBasedReservoirs() = default;

	// Constructor 
	GridBasedReservoirs(
		const Context &,
		const std::shared_ptr <amadeus::System> &,
		const std::shared_ptr <MeshMemory> &,
		const vk::Extent2D &
	);

	// Proprety methods
	size_t size() {
		return extent.width * extent.height;
	}

	// Buffer accessors
	CUdeviceptr color_buffer() {
		return (CUdeviceptr) launch_params.color_buffer;
	}

	CUdeviceptr normal_buffer() {
		return (CUdeviceptr) launch_params.normal_buffer;
	}

	CUdeviceptr albedo_buffer() {
		return (CUdeviceptr) launch_params.albedo_buffer;
	}

	CUdeviceptr position_buffer() {
		return (CUdeviceptr) launch_params.position_buffer;
	}

	// Other methods
	void set_envmap(const std::string &);

	void render(
		const ECS &,
		const Camera &,
		const Transform &,
		bool = false
	);
private:
	// TODO: pack these...
	int *d_sample_indices;
	int *d_sample_sources;
	int *d_cell_sizes;

	float3 p_camera;
	bool first_frame = true;

	std::default_random_engine generator;

	void initialize_optix();

	void preprocess_scene(
		const ECS &,
		const Camera &,
		const Transform &
	);
};

}

}

#endif
