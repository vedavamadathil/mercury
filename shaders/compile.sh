# Create binary directories
mkdir -p bin bin/gui bin/generic bin/raster

# Compile GENERIC mode shaders
glslc -fshader-stage=compute rt/pixel.glsl -o bin/generic/pixel.spv
glslc -fshader-stage=vertex postproc/postproc.vert -o bin/generic/pp_vert.spv
glslc -fshader-stage=fragment postproc/postproc.frag -o bin/generic/pp_frag.spv

glslc -fshader-stage=compute rt/mesh.glsl -o bin/generic/mesh.spv
glslc -fshader-stage=vertex rt/postproc/postproc.vert -o bin/generic/postproc_vert.spv
glslc -fshader-stage=fragment rt/postproc/postproc.frag -o bin/generic/postproc_frag.spv

# Compile GUI mode shaders
glslc -fshader-stage=vertex gui/basic.vert -o bin/gui/basic_vert.spv
glslc -fshader-stage=fragment gui/basic.frag -o bin/gui/basic_frag.spv

glslc -fshader-stage=vertex gui/glyph.vert -o bin/gui/glyph_vert.spv
glslc -fshader-stage=fragment gui/glyph.frag -o bin/gui/glyph_frag.spv

glslc -fshader-stage=fragment gui/bitmap.frag -o bin/gui/bitmap_frag.spv

# Compile rasteization shaders
glslc -fshader-stage=vertex raster/vertex.vert -o bin/raster/vertex.spv
glslc -fshader-stage=fragment raster/color.frag -o bin/raster/color_frag.spv
glslc -fshader-stage=fragment raster/normal.frag -o bin/raster/normal_frag.spv
glslc -fshader-stage=fragment raster/blinn_phong.frag -o bin/raster/blinn_phong_frag.spv
