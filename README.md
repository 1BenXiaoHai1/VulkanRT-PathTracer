# VulkanRT-PathTracer

A real-time Vulkan path tracer with dynamic geometry, soft shadows, and TAA denoising.


## Run and Clean

- Run the program: `make run`
- Clean build artifacts: `make clean`

## Project Structure

```c++
VulkanRT-PathTracer/
├── Makefile                                  
├── include/                    
│   ├── GLFW/                   
│   ├── glm/                    
│   ├── stb/                   
│   └── tinyobjloader/         
├── src/
│   ├── main.cpp                
│   └── shaders/
│       ├── shader.rgen         # 光线生成着色器
│       ├── shader.rchit        # 最近命中着色器
│       ├── shader.rmiss        # 主未命中着色器
│       ├── shader_shadow.rmiss # 阴影未命中着色器
│       └── *.spv               # 编译后的 SPIR-V 二进制文件
├── output/                     # 构建输出目录
└── static/
    └── models/
        ├── cube_scene.obj      # 3D 场景几何体
        └── cube_scene.mtl      # 材质定义
```

### Core Initialization

- Window

- Instance

- Surface

- Physical Device

- Logical Device：需要额外开启光线追踪扩展功能。

  - Device Extensions

    | 扩展                              | 目的                         |
    | --------------------------------- | ---------------------------- |
    | `VK_KHR_ray_tracing_pipeline`     | 光线追踪管线创建和执行       |
    | `VK_KHR_acceleration_structure`   | BLAS/TLAS 构建和管理         |
    | `VK_EXT_descriptor_indexing`      | 高级描述符绑定功能           |
    | `VK_KHR_buffer_device_address`    | 用于光线追踪的直接缓冲区寻址 |
    | `VK_KHR_deferred_host_operations` | 异步加速结构构建             |

  - Features
    <img src=".\images\features.png" alt="image-20260312120909743" style="zoom:100%;" />

    | 功能结构                        | 启用的功能              | 目的                   |
    | ------------------------------- | ----------------------- | ---------------------- |
    | `RayTracingPipelineFeatures`    | `rayTracingPipeline`    | 管线创建和光线追踪命令 |
    | `AccelerationStructureFeatures` | `accelerationStructure` | BLAS/TLAS 创建和查询   |
    | `BufferDeviceAddressFeatures`   | `bufferDeviceAddress`   | 直接缓冲区内存寻址     |

- 扩展函数加载：光线追踪功能由不属于核心 Vulkan API 的扩展功能提供，这些函数必须在设备创建后动态加载。

  | 功能指针                                     | 目的                    |
  | -------------------------------------------- | ----------------------- |
  | `vkGetBufferDeviceAddressKHR`                | 查询缓冲区设备地址      |
  | `vkCreateRayTracingPipelinesKHR`             | 创建光线追踪管线        |
  | `vkGetAccelerationStructureBuildSizesKHR`    | 查询加速结构内存需求    |
  | `vkCreateAccelerationStructureKHR`           | 创建加速结构            |
  | `vkDestroyAccelerationStructureKHR`          | 销毁加速结构            |
  | `vkGetAccelerationStructureDeviceAddressKHR` | 查询加速结构地址        |
  | `vkCmdBuildAccelerationStructuresKHR`        | 记录加速结构构建命令    |
  | `vkGetRayTracingShaderGroupHandlesKHR`       | 检索着色器组的 SBT 句柄 |
  | `vkCmdTraceRaysKHR`                          | 记录光线追踪分发命令    |

### SwapChain

- Swapchain
- Swapchain imageviews

### Command System

- Command Pool
- Command Buffer

### Resources

#### Vertex Buffer+Index Buffer

<img src=".\images\VB&IB.png" alt="image-20260313130049109" style="zoom:50%;" />

| 标志                                                         | 用途                                  | 光线追踪上下文          |
| ------------------------------------------------------------ | ------------------------------------- | ----------------------- |
| `VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT`                  | 允许 GPU 着色器通过设备地址访问缓冲区 | BLAS 几何数据引用所必需 |
| `VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR` | 标记缓冲区为加速结构构建的输入        | BLAS 构建所必需         |
| `VK_BUFFER_USAGE_STORAGE_BUFFER_BIT`                         | 允许缓冲区在着色器中作为存储缓        |                         |

- VertexBuffer：存储顶点数据。
- IndexBuffer：存储顶点索引。

#### Material Buffer+Material Index Buffer

- Material Buffer用来存储实际的材质定义，通过材质索引缓冲区中的值进行索引。
- Material Index Buffer是用来存储材质索引的。为图元与其分配材质之间的一对一映射。

#### Uniform Buffer

- Uniform Buffer用于存储相机模型的位置、右向量、上向量、前进向量，以及用于时序抗锯齿的累加器。
- VkDescriptorSetLayoutBinding+VkDescriptorBufferInfo+VkWriteDescriptorSet
-  作用：使光线生成着色器能够使用相机位置和基向量构建相机空间主光线。最近击中着色器访问相机位置以计算基于视角的效果，如镜面高光或阴影光线方向。

#### Ray Tracing Image

- 用来存储光线追踪结果的图片。

  | 标志                            | 用途                   |
  | ------------------------------- | ---------------------- |
  | VK_IMAGE_USAGE_STORAGE_BIT      | 存储，可以读取可以写入 |
  | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | 传输源                 |

- Ray Tracing image的布局转换？

#### Acceleration Structure

<img src=".\images\AS.png" alt="image-20260316121248237" style="zoom:70%;" />

##### Bottom Level Acceleration Structure

-Vertex Buffer和Index Buffer作为BLAS构建的输入数据。
- 构成场景网格的实际几何图元，如三角形或球体。
- BLAS几何配置-查询构建大小- BLAS Buffer的创建- BLAS Scratch Buffer的创建- BLAS的创建-构建BLAS。

##### Top Level Acceleration Structure

- BLAS的Device Address作为Instance的输入。BLAS的instance Device Address作为TLAS构建的输入数据。
- 由**多个BLAS的实例Instances**组成。每个实例包含对一个 BLAS 的引用及其变换矩阵（位置、旋转、缩放）。
- Instance的创建-Instance Buffer的创建- TLAS几何配置-查询构建大小- TLAS Buffer的创建- TLAS Scratch Buffer的创建- TLAS的创建-构建TLAS。

### Resources Binding

#### DescriptorSetLayout

- accelerationStructureLayoutBinding+cameraUniformBufferLayoutBinding+indexBufferStorageBufferLayoutBinding+vertexBufferStorageBufferLayoutBinding+storageImageLayoutBinding。
- descriptorSetLayout

#### Material DescriptorSetLayout

- materialIndexStorageBufferLayoutBinding+materialStorageBufferLayoutBinding
- materialDescriptorSetLayout

#### Descriptor Pool

- accelerationStructurePoolSize+uniformBufferPoolSize+storageBufferPoolSize+storageImagePoolSize

#### Descriptor Set

- accelerationStructureDescriptorInfo+uniformDescriptorInfo+indexDescriptorInfo+vertexDescriptorInfo+rayTraceImageDescriptorInfo
- materialIndexDescriptorInfo+materialDescriptorInfo

### Ray Tracing Pipeline

- Shader Stages+Shader Groups+descriptorSetLayouts

#### Shader Binding Table

- 计算对齐大小和ShaderGroup大小-SBT Buffer的创建-数据复制-配置区域信息



#### Shaders

##### Ray Generate Shader

- 输入：ray payload、TLAS、Camera。

- 输出：ray tracing image。

  ```c++
    // 追踪光线，最多追踪16次，直到光线不再活跃或者达到最大深度
    // 第 1 次：发射主光线，击中物体 -> Hit Shader 计算直接光照，并可能修改 payload.rayOrigin/Direction 准备反射光线。
    // 第 2 次：基于新的原点/方向再次 traceRayEXT，计算二次反弹（间接光照）。
    // ...
    for (int x = 0; x < 16 && payload.rayActive == 1; x++) {
      traceRayEXT(
        topLevelAS, // 顶层加速结构
        gl_RayFlagsOpaqueEXT, // 光线追踪标志，指定光线的行为，gl_RayFlagsOpaqueEXT表示几何体将被视为不透明的，遇到任何几何体都会停止
        0xFF, // 该值是一个掩码，用于指定哪些实例和三角形应该被考虑进行光线追踪，0xFF表示所有的实例和三角形都将被考虑
        0, // 索引，用于指定哪个Hit Shader将被调用，0表示第一个Hit Shader 
        0, // 索引，用于指定哪个Miss Shader将被调用
        0, // 索引，用于指定哪个Callable Shader将被调用
        payload.rayOrigin, // 光线的起点，通常是摄像机的位置
        0.001, 
        payload.rayDirection, // 光线的方向，通常是从摄像机指向场景中的某个点
        10000.0, 
        0
        );
    }
  ```

##### Ray Closet Hit Shader

- 输入：rayPayloadIn、rayPayload、TLAS、Camera、Index Buffer、Vertex Buffer、Material Buffer、Material Index Buffer、
- 负责光线命中物体着色处理

##### Miss Shader

- 未击中物体，光线rayActive设为0，说明光线已死，不会进行有效着色计算。

##### Shadow Miss Shader

- 负责阴影射线未命中物体的处理。

### 光线着色的流程

光照模型假设所有物体表面都是**理想漫反射表面**（无高光、无镜面反射），只计算漫反射分量，带有自定义深度衰减因子的简化路径追踪光照模型。

光线击中场景中的某个三角形后，首先检查 `gl_PrimitiveID`

-  **情况 A：击中光源 (ID 40 或 41)** →→ 进入 **自发光计算**。
-  **情况 B：击中普通物体** →→ 进入 **直接光照采样 + 阴影判断**。

#### 自发光计算

- **获取材质**：读取该三角形的 `emission` (自发光颜色)。

- 根据光线深度，选择不同的计算方式

  - **`rayDepth == 0`**：

    $$\text{payload.directColor} = \mathbf{E}_{\text{emission}}$$

  - **`rayDepth > 0`**：

    $$\text{payload.indirectColor} \mathrel{+}= \mathbf{E}_{\text{emission}} \times \left( \frac{1.0}{\text{rayDepth}} \right) \times \cos(\theta_{\text{in}}) \\\cos(\theta_{\text{in}}) = \dot(\mathbf{N}_{\text{prev}}, \mathbf{W}_{\text{in}})$$

#### 击中普通物体

- **随机采样光源点（Next Event Estimation, NEE）**

- 发射**阴影光线**，用于判断交点的可见性。调用 `traceRayEXT` (Miss Index = 1)。

  - **未被遮挡 (Miss Shader 运行)**：`isShadow = false`。
  - **被遮挡 (Hit Shader 运行)**：`isShadow = true` (保持默认值)。

- 根据**是否被遮挡**计算光照贡献

  $$L_{LambertN} = N \cdot L = \max(0, \dot(\mathbf{N}_{\text{geo}}, \mathbf{L}))$$

  - 没有被遮挡：

    - **`rayDepth == 0`**：直接光照。

      $$\text{payload.directColor} = \mathbf{C}_{\text{surf}} \times \mathbf{C}_{\text{light}} \times L_{LambertN}$$

    - **`rayDepth > 0`**：间接光照。

      $$\text{payload.indirectColor} \mathrel{+}= \left( \frac{1.0}{\text{rayDepth}} \right) \times \mathbf{C}_{\text{surf}} \times \mathbf{C}_{\text{light}} \times \cos(\theta_{\text{prev}}) \times L_{LambertN}$$

  - 被遮挡：

    - **`rayDepth == 0`**：

      $$\text{payload.directColor} = \begin{pmatrix} 0.0 \\ 0.0 \\ 0.0 \end{pmatrix}$$

    - **`rayDepth > 0`**：

      $$\text{payload.rayActive} = 0$$

#### 准备下一次反弹（路径反弹）

- 随机采样半球，获取下一个半球方向，追踪下一条光线。
- 更新下一次的payload-rayOrigin、rayDirection、previousNormal、rayDepth

#### 合成

$$\text{FinalColor} = \text{payload.directColor} + \text{payload.indirectColor}$$
