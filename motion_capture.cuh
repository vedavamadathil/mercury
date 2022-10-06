#ifndef MOTION_CAPTURE_H_
#define MOTION_CAPTURE_H_

// GLM headers
#include <glm/glm.hpp>

// OpenCV for video capture
#include <opencv2/core.hpp>
#include <opencv2/videoio.hpp>
#include <opencv2/imgproc.hpp>

// Engine headers
#include "include/app.hpp"
#include "include/capture.hpp"
#include "include/core/interpolation.hpp"
#include "include/layers/hybrid_tracer.cuh"
#include "include/layers/optix_tracer.cuh"
#include "include/optix/options.cuh"
#include "include/scene.hpp"

// TODO: do base app without inheritance (simple struct..., app and baseapp not
// related)
struct MotionCapture : public kobra::BaseApp {
	// TODO: let the scene run on any virtual device?
	kobra::Entity camera;
	kobra::Scene scene;

	kobra::layers::OptixTracer tracer;
	kobra::layers::HybridTracer hybrid_tracer;

	// Capture
	cv::VideoWriter capture;
	std::vector <byte> frame;

	const std::vector <glm::vec3> camera_pos {
		{0.00, 27.36, 31.03},
		{62.62, 25.96, 9.59},
		{58.98, 23.88, -41.79},
		{17.60, 23.88, -60.29},
		{-25.29, 34.61, -30.43}
	};

	const std::vector <glm::vec3> camera_rot {
		{-0.30, -0.09, 0.00},
		{-0.28, 0.74, 0.00},
		{-0.31, 2.20, 0.00},
		{-0.18, 3.01, 0.00},
		{-0.67, 4.47, 0.00}
	};

	const std::vector <float> times {
		0.0f, 1.0f, 2.0f, 3.0f, 4.0f
	};

	kobra::core::Sequence <glm::vec3> camera_pos_seq {
		.values = camera_pos,
		.times = times
	};

	kobra::core::Sequence <glm::vec3> camera_rot_seq {
		.values = camera_rot,
		.times = times
	};
	
	MotionCapture(const vk::raii::PhysicalDevice &phdev,
			const std::vector <const char *> &extensions,
			const std::string &scene_path)
			: BaseApp(phdev, "MotionCapture",
				vk::Extent2D {1600, 1200},
				extensions, vk::AttachmentLoadOp::eLoad
			),
			tracer(get_context(),
				vk::AttachmentLoadOp::eClear,
				1000, 1000
			) {
		// Load scene and camera
		scene.load(get_device(), scene_path);
		camera = scene.ecs.get_entity("Camera");

		// Setup tracer
		tracer.environment_map("resources/skies/background_1.jpg");
		tracer.sampling_strategy = kobra::optix::eSpatioTemporal;
		tracer.denoiser_enabled = false;

		// Setup hybrid tracer
		KOBRA_LOG_FILE(kobra::Log::INFO) << "Hybrid tracer setup\n";
		hybrid_tracer = kobra::layers::HybridTracer::make(get_context());

		// Setup capture
		capture.open(
			"animation.mp4",
			cv::VideoWriter::fourcc('A', 'V', 'C', '1'),
			60, cv::Size(1000, 1000)
		);

		if (!capture.isOpened())
			std::cout << "Failed to open capture" << std::endl;
		else
			std::cout << "Capture opened" << std::endl;
	}

	float time = 0.0f;
	void record(const vk::raii::CommandBuffer &cmd,
			const vk::raii::Framebuffer &framebuffer) override {
		// Move the camera
		auto &transform = camera.get <kobra::Transform> ();
		
		static const glm::vec3 origin {10, 5, -10};
		transform.position = glm::vec3 {
			5.0f * std::sin(time) + 7,
			6.0f,
			5.0f * std::cos(time) - 10
		};

		// Look at the origin always
		glm::vec3 eye = transform.position;
		glm::vec3 dir = glm::normalize(origin - eye);
		
		transform.look(dir);

		// Interpolation sequence
		if (time > 4.0f) time = 0.0f;

		// Interpolate camera position
		glm::vec3 pos = kobra::core::piecewise_linear(camera_pos_seq, time);
		glm::vec3 rot = kobra::core::piecewise_linear(camera_rot_seq, time);

		transform.position = pos;
		transform.rotation = rot;

		// Now trace and render
		cmd.begin({});
			for (int i = 0; i < 4; i++)
				tracer.compute(scene.ecs);
			tracer.render(cmd, framebuffer);

			kobra::layers::compute(hybrid_tracer,
				scene.ecs,
				camera.get <kobra::Camera> (),
				camera.get <kobra::Transform> ()
			);
		cmd.end();

		// Write the frame to the video
		tracer.capture(frame);

		cv::Mat mat(1000, 1000, CV_8UC4, frame.data());
		cv::cvtColor(mat, mat, cv::COLOR_BGRA2RGB);
		/* capture.write(mat);

		if (time > 5) {
			capture.release();
			terminate_now();
		} */

		// Update time (fixed)
		time += 1/60.0f;
	}
};

#endif
