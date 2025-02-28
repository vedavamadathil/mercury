#ifndef KOBRA_AMADEUS_RESTIR_H_
#define KOBRA_AMADEUS_RESTIR_H_

// Standard headers
#include <random>

// Engine headers
#include "armada.cuh"
#include "reservoir.cuh"
#include "../cuda/material.cuh"
#include "../cuda/random.cuh"

namespace kobra {

namespace amadeus {

// Launch parameters
struct ReSTIR_Parameters : ArmadaLaunchInfo {
	// Scene traversable
	OptixTraversableHandle traversable;

	// Reservoirs
	Reservoir <DirectLightingSample> *current;
	Reservoir <DirectLightingSample> *previous;
	cuda::Material *materials_buffer;
	glm::vec4 *intermediate;
	glm::vec4 *auxiliary;

	// Previous camera (for temporal reprojection)
	Camera previous_camera;
};

// ReSTIR kernel
// TODO: link paper here...
class ReSTIR : public AttachmentRTX {
	// SBT record types
	using RaygenRecord = optix::Record <int>;
	using MissRecord = optix::Record <int>;
	using HitgroupRecord = optix::Record <optix::Hit>;

	vk::Extent2D m_extent;

	// Pipeline related information
	OptixModule m_module;

	OptixProgramGroup m_ray_generation_initial;
	OptixProgramGroup m_ray_generation_temporal;
	OptixProgramGroup m_ray_generation_spatial;
	OptixProgramGroup m_closest_hit;
	OptixProgramGroup m_miss;
	OptixProgramGroup m_miss_shadow;

	OptixPipeline m_pipeline_initial;
	OptixPipeline m_pipeline_temporal;
	OptixPipeline m_pipeline_spatial;

	OptixShaderBindingTable m_sbt_initial;
	OptixShaderBindingTable m_sbt_temporal;
	OptixShaderBindingTable m_sbt_spatial;

	// Buffer for launch parameters
	ReSTIR_Parameters m_parameters;
	CUdeviceptr m_cuda_parameters;

	// TODO: stream

	// Create the program groups and pipeline
	void create_pipeline(const OptixDeviceContext &optix_context) {
		static constexpr const char OPTIX_PTX_FILE[] = "bin/ptx/amadeus_restir.ptx";

		// Load module
		m_module = optix::load_optix_module(
			optix_context, OPTIX_PTX_FILE,
			ppl_compile_options, module_options
		);

		// Load programs
		OptixProgramGroupOptions program_options = {};

		// Descriptions of all the programs
		std::vector <OptixProgramGroupDesc> program_descs = {
			OPTIX_DESC_RAYGEN(m_module, "__raygen__initial"),
			OPTIX_DESC_RAYGEN(m_module, "__raygen__temporal"),
			OPTIX_DESC_RAYGEN(m_module, "__raygen__spatial"),
			OPTIX_DESC_HIT(m_module, "__closesthit__initial"),
			OPTIX_DESC_MISS(m_module, "__miss__"),
			OPTIX_DESC_MISS(m_module, "__miss__shadow")
		};

		// Corresponding program groups
		std::vector <OptixProgramGroup *> program_groups = {
			&m_ray_generation_initial,
			&m_ray_generation_temporal,
			&m_ray_generation_spatial,
			&m_closest_hit,
			&m_miss,
			&m_miss_shadow
		};

		optix::load_program_groups(
			optix_context,
			program_descs,
			program_options,
			program_groups
		);

		// Create the pipelines
		m_pipeline_initial = optix::link_optix_pipeline(
			optix_context,
			{
				m_ray_generation_initial,
				m_closest_hit,
				m_miss,
				m_miss_shadow
			},
			ppl_compile_options,
			ppl_link_options
		);

		m_pipeline_temporal = optix::link_optix_pipeline(
			optix_context,
			{
				m_ray_generation_temporal,
				m_miss_shadow
			},
			ppl_compile_options,
			ppl_link_options
		);

		m_pipeline_spatial = optix::link_optix_pipeline(
			optix_context,
			{
				m_ray_generation_spatial,
				m_miss_shadow
			},
			ppl_compile_options,
			ppl_link_options
		);
	}
public:
	// Constructor
	ReSTIR() : AttachmentRTX(1) {}

	// Attaching and unloading
	void attach(const ArmadaRTX &armada_rtx) override {
		m_extent = armada_rtx.extent();

		// First load the pipeline
		create_pipeline(armada_rtx.system()->context());

		// Allocate the SBT
		std::vector <RaygenRecord> ray_generation_records_initial(1);
		std::vector <RaygenRecord> ray_generation_records_temporal(1);
		std::vector <RaygenRecord> ray_generation_records_spatial(1);

		std::vector <MissRecord> miss_records(2);

		// Fill the SBT
		optix::pack_header(m_ray_generation_initial, ray_generation_records_initial[0]);
		optix::pack_header(m_ray_generation_temporal, ray_generation_records_temporal[0]);
		optix::pack_header(m_ray_generation_spatial, ray_generation_records_spatial[0]);

		optix::pack_header(m_miss, miss_records[0]);
		optix::pack_header(m_miss_shadow, miss_records[1]);

		// Create the SBTs
		m_sbt_initial = {};
		m_sbt_temporal = {};
		m_sbt_spatial = {};

		// TODO: store and free
		CUdeviceptr m_sbt_miss = cuda::make_buffer_ptr(miss_records);

		// Initial SBT
		m_sbt_initial.raygenRecord = cuda::make_buffer_ptr(ray_generation_records_initial);

		m_sbt_initial.missRecordBase = m_sbt_miss;
		m_sbt_initial.missRecordStrideInBytes = sizeof(MissRecord);
		m_sbt_initial.missRecordCount = miss_records.size();

		m_sbt_initial.hitgroupRecordBase = 0;
		m_sbt_initial.hitgroupRecordStrideInBytes = 0;
		m_sbt_initial.hitgroupRecordCount = 0;

		// Temporal SBT
		m_sbt_temporal.raygenRecord = cuda::make_buffer_ptr(ray_generation_records_temporal);

		m_sbt_temporal.missRecordBase = m_sbt_miss;
		m_sbt_temporal.missRecordStrideInBytes = sizeof(MissRecord);
		m_sbt_temporal.missRecordCount = miss_records.size();

		// Spatial SBT
		m_sbt_spatial.raygenRecord = cuda::make_buffer_ptr(ray_generation_records_spatial);

		m_sbt_spatial.missRecordBase = m_sbt_miss;
		m_sbt_spatial.missRecordStrideInBytes = sizeof(MissRecord);
		m_sbt_spatial.missRecordCount = miss_records.size();

		// Initialize the parameters buffer
		m_cuda_parameters = (CUdeviceptr) cuda::alloc <ReSTIR_Parameters> (1);
	}

	void load() override {
		// Allocate the reservoirs and indirect buffer
		std::vector <Reservoir <DirectLightingSample>> reservoirs(m_extent.width * m_extent.height);
		m_parameters.current = cuda::make_buffer(reservoirs);
		m_parameters.previous = cuda::make_buffer(reservoirs);
		m_parameters.materials_buffer = cuda::alloc <cuda::Material> (m_extent.width * m_extent.height);
		m_parameters.intermediate = cuda::alloc <glm::vec4> (m_extent.width * m_extent.height);
		m_parameters.auxiliary = cuda::alloc <glm::vec4> (m_extent.width * m_extent.height);
	}

	void unload() override {
		// Free the reservoirs
		cuda::free(m_parameters.current);
		cuda::free(m_parameters.previous);
		cuda::free(m_parameters.materials_buffer);
		cuda::free(m_parameters.intermediate);
		cuda::free(m_parameters.auxiliary);
	}

	// Rendering
	void render(const ArmadaRTX *armada_rtx,
			const ArmadaLaunchInfo &launch_info,
			const std::optional <OptixTraversableHandle> &handle,
			std::vector <HitRecord> *hit_records,
			const vk::Extent2D &extent) override {
		// TODO: store sample light index instead of Le, since lighting
		// emission could change...

		// Check if hit groups need to be updated, and update them if necessary
		if (hit_records) {
			for (auto &hitgroup_record : *hit_records)
				optix::pack_header(m_closest_hit, hitgroup_record);

			// Free old buffer
			if (m_sbt_initial.hitgroupRecordBase)
				cuda::free(m_sbt_initial.hitgroupRecordBase);

			// Update the SBT
			m_sbt_initial.hitgroupRecordBase = cuda::make_buffer_ptr(*hit_records);
			m_sbt_initial.hitgroupRecordStrideInBytes = sizeof(HitRecord);
			m_sbt_initial.hitgroupRecordCount = hit_records->size();
		}

		// Copy the parameters and launch
		std::memcpy(&m_parameters, &launch_info, sizeof(ArmadaLaunchInfo));

		if (handle)
			m_parameters.traversable = *handle;

		cuda::copy(m_cuda_parameters, &m_parameters, 1, cudaMemcpyHostToDevice);

		//////////////////////////
		// Execute the pipeline //
		//////////////////////////

		// Initial pass
		OPTIX_CHECK(
			optixLaunch(
				m_pipeline_initial, 0,
				m_cuda_parameters, sizeof(ReSTIR_Parameters),
				&m_sbt_initial, extent.width, extent.height, 1
			)
		);

		CUDA_SYNC_CHECK();

		// Temporal pass
		OPTIX_CHECK(
			optixLaunch(
				m_pipeline_temporal, 0,
				m_cuda_parameters, sizeof(ReSTIR_Parameters),
				&m_sbt_temporal, extent.width, extent.height, 1
			)
		);

		CUDA_SYNC_CHECK();

		// Spatial pass
		// TODO: option for multiple spatial reuses
		for (int i = 0; i < 2; i++) {
			OPTIX_CHECK(
				optixLaunch(
					m_pipeline_spatial, 0,
					m_cuda_parameters, sizeof(ReSTIR_Parameters),
					&m_sbt_spatial, extent.width, extent.height, 1
				)
			);

			CUDA_SYNC_CHECK();

			// Swap for next iteration
			// TODO: indicate whether to shade or not
			std::swap(m_parameters.current, m_parameters.previous);
			cuda::copy(m_cuda_parameters, &m_parameters, 1, cudaMemcpyHostToDevice);
		}

		// Swap the reservoirs
		std::swap(m_parameters.current, m_parameters.previous);

		// Copy camera parameters
		std::memcpy(&m_parameters.previous_camera, &m_parameters.camera, sizeof(ArmadaLaunchInfo::Camera));
	}
};

}

}

#endif
