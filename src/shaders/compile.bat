rem Target Vulkan 1.2 so GL_EXT_ray_tracing is allowed
glslc --target-env=vulkan1.2 shader_shadow.rmiss -o shader_shadow.rmiss.spv
glslc --target-env=vulkan1.2 shader.rchit -o shader.rchit.spv
glslc --target-env=vulkan1.2 shader.rgen -o shader.rgen.spv
glslc --target-env=vulkan1.2 shader.rmiss -o shader.rmiss.spv