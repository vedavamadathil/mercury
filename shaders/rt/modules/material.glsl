// Material structure
struct Material {
	vec3	albedo;
	int	shading;
	float	ior;
	float	has_normal;
};

// Default "constructor"
Material mat_default()
{
	return Material(
		vec3(0.5f, 0.5f, 0.5f),
		0, 1.0f, 1.0f
	);
}

// Convert raw material at index
// TODO: should also contain normal vectors
Material mat_at(uint index, vec2 uv)
{
	// TODO: material stride variable
	vec4 raw0 = materials.data[2 * index];
	vec4 raw1 = materials.data[2 * index + 1];

	vec3 color = vec3(raw0.xyz);
	if (raw1.y < 0.5) {
		// Sample from texture
		color = texture(s2_albedo[index], uv).rgb;
	}

	return Material(
		color,
		floatBitsToInt(raw0.w),
		raw1.x,
		raw1.z
	);
}
