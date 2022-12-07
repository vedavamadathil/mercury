#include "../../include/layers/framer.hpp"

namespace kobra {

namespace layers {

// Static member variables
const std::vector <DSLB> Framer::dsl_bindings = {
	DSLB {
		0, vk::DescriptorType::eCombinedImageSampler,
		1, vk::ShaderStageFlagBits::eFragment
	}
};

// Create the layer
Framer::Framer(const Context &context)
{
	// Extract critical Vulkan structures
	device = context.device;
	phdev = context.phdev;
	descriptor_pool = context.descriptor_pool;
	sync_queue = context.sync_queue;

	// Create the present render pass
	render_pass = make_render_pass(*context.device,
		{context.swapchain_format},
		{vk::AttachmentLoadOp::eClear},
		context.depth_format,
		vk::AttachmentLoadOp::eClear
	);

	// Descriptor set layout
	dsl = make_descriptor_set_layout(*context.device, dsl_bindings);

	// Allocate present descriptor set
	auto dsets = vk::raii::DescriptorSets {
		*context.device,
		{**context.descriptor_pool, *dsl}
	};

	dset = std::move(dsets.front());

	// Push constants and pipeline layout
	ppl = vk::raii::PipelineLayout {
		*context.device,
		{{}, *dsl, {}}
	};

	// Create the present pipeline
	auto shaders = make_shader_modules(*context.device, {
		"bin/spv/spit_vert.spv",
		"bin/spv/spit_frag.spv"
	});
	
	GraphicsPipelineInfo present_grp_info {
		*context.device, render_pass,
		std::move(shaders[0]), nullptr,
		std::move(shaders[1]), nullptr,
		{}, {},
		ppl
	};

	present_grp_info.no_bindings = true;
	present_grp_info.depth_test = false;
	present_grp_info.depth_write = false;

	pipeline = make_graphics_pipeline(present_grp_info);

	// Allocate resources for rendering results
	result_image = ImageData(
		*context.phdev, *context.device,
		vk::Format::eR8G8B8A8Unorm,
		context.extent,
		vk::ImageTiling::eOptimal,
		vk::ImageUsageFlagBits::eSampled
			| vk::ImageUsageFlagBits::eTransferDst,
		vk::ImageLayout::eUndefined,
		vk::MemoryPropertyFlagBits::eDeviceLocal,
		vk::ImageAspectFlagBits::eColor
	);

	result_sampler = make_sampler(*context.device, result_image);

	// Allocate staging buffer
	vk::DeviceSize stage_size = context.extent.width
		* context.extent.height
		* sizeof(uint32_t);

	auto usage = vk::BufferUsageFlagBits::eStorageBuffer;
	auto mem_props = vk::MemoryPropertyFlagBits::eDeviceLocal
		| vk::MemoryPropertyFlagBits::eHostCoherent
		| vk::MemoryPropertyFlagBits::eHostVisible;

	result_buffer = BufferData(
		*context.phdev, *context.device, stage_size,
		usage | vk::BufferUsageFlagBits::eTransferSrc, mem_props
	);

	// Bind image sampler to the present descriptor set
	//	immediately, since it will not change
	bind_ds(*context.device,
		dset,
		result_sampler,
		result_image, 0
	);
}

// Resize callback
void Framer::resize_callback(const Image &frame)
{
	// Resize resources
	result_buffer.resize(frame.size());

	result_image = ImageData(
		*phdev, *device,
		vk::Format::eR8G8B8A8Unorm,
		{frame.width, frame.height},
		vk::ImageTiling::eOptimal,
		vk::ImageUsageFlagBits::eSampled
			| vk::ImageUsageFlagBits::eTransferDst,
		vk::ImageLayout::eUndefined,
		vk::MemoryPropertyFlagBits::eDeviceLocal,
		vk::ImageAspectFlagBits::eColor
	);

	result_sampler = make_sampler(*device, result_image);

	bind_ds(*device,
		dset,
		result_sampler,
		result_image, 0
	);
}

// Render to the presentable framebuffer
// TODO: custom extent
void Framer::render
		(const Image &frame,
		const vk::raii::CommandBuffer &cmd,
		const vk::raii::Framebuffer &framebuffer,
		const vk::Extent2D &extent,
		const RenderArea &ra)
{
	// Upload data to the buffer
	// TODO: also allow resize... pass an image struct instead
	bool skip_frame = false;
	if (result_buffer.size != frame.size()) {
		// Sync changes
		sync_queue->push({
			"[Framer] Resized resources",
			[&, frame] () {
				resize_callback(frame);
			}
		});

		skip_frame = true;
	}

	if (!skip_frame) {
		result_buffer.upload(frame.data);
		
		// Copy buffer to image
		result_image.transition_layout(cmd, vk::ImageLayout::eTransferDstOptimal);

		copy_data_to_image(cmd,
			result_buffer.buffer,
			result_image.image,
			result_image.format,
			frame.width, frame.height
		);
	}

	// Transition image back to shader read
	result_image.transition_layout(cmd, vk::ImageLayout::eShaderReadOnlyOptimal);
		
	// Apply render area
	ra.apply(cmd, extent);

	// Clear colors
	// TODO: method
	std::array <vk::ClearValue, 2> clear_values {
		vk::ClearValue {
			vk::ClearColorValue {
				std::array <float, 4> {0.0f, 0.0f, 0.0f, 1.0f}
			}
		},
		vk::ClearValue {
			vk::ClearDepthStencilValue {
				1.0f, 0
			}
		}
	};
	
	// Start the render pass
	cmd.beginRenderPass(
		vk::RenderPassBeginInfo {
			*render_pass,
			*framebuffer,
			vk::Rect2D {
				vk::Offset2D {0, 0},
				extent
			},
			static_cast <uint32_t> (clear_values.size()),
			clear_values.data()
		},
		vk::SubpassContents::eInline
	);

	// Presentation pipeline
	cmd.bindPipeline(
		vk::PipelineBindPoint::eGraphics,
		*pipeline
	);

	// Bind descriptor set
	cmd.bindDescriptorSets(
		vk::PipelineBindPoint::eGraphics,
		*ppl, 0, {*dset}, {}
	);

	// Draw and end
	cmd.draw(6, 1, 0, 0);
	cmd.endRenderPass();
}

}

}
