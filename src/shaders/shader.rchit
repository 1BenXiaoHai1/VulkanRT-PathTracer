#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : enable

#define M_PI 3.1415926535897932384626433832795

struct Material {
  vec3 ambient;
  vec3 diffuse;
  vec3 specular;
  vec3 emission;
};

hitAttributeEXT vec2 hitCoordinate; // 内置变量，表示当前光线与三角形相交的重心坐标

// 从 RayGen 传过来的payload
// rayPayloadInEXT表示这是一个输入的payload，Hit Shader只能读取它的值，不能修改它
layout(location = 0) rayPayloadInEXT Payload {
  vec3 rayOrigin;
  vec3 rayDirection;
  vec3 previousNormal;
  vec3 directColor;
  vec3 indirectColor;
  int rayDepth;
  int rayActive;
}payload;

// payload是专门为阴影光线准备的，Hit Shader会修改它的值来告诉RayGen当前光线是否被遮挡了
layout(location = 1) rayPayloadEXT bool isShadow;

layout(binding = 0, set = 0) uniform accelerationStructureEXT topLevelAS;
layout(binding = 1, set = 0) uniform Camera {
  vec4 position;
  vec4 right;
  vec4 up;
  vec4 forward;
  uint frameCount;
}camera;

// 几何数据
layout(binding = 2, set = 0) buffer IndexBuffer { 
  uint data[]; 
}indexBuffer;
layout(binding = 3, set = 0) buffer VertexBuffer { 
  float data[]; 
}vertexBuffer;

// 材质数据
layout(binding = 0, set = 1) buffer MaterialIndexBuffer { 
  uint data[]; 
}materialIndexBuffer; // 每个三角形对应一个材质索引
layout(binding = 1, set = 1) buffer MaterialBuffer { 
  Material data[]; 
}materialBuffer; // 材质索引对应的材质数据

float random(vec2 uv, float seed) {
  return fract(sin(mod(dot(uv, vec2(12.9898, 78.233)) + 1113.1 * seed, M_PI)) * 43758.5453);
}

vec3 uniformSampleHemisphere(vec2 uv) {
  float z = uv.x;
  float r = sqrt(max(0, 1.0 - z * z));
  float phi = 2.0 * M_PI * uv.y;

  return vec3(r * cos(phi), z, r * sin(phi));
}

vec3 alignHemisphereWithCoordinateSystem(vec3 hemisphere, vec3 up) {
  vec3 right = normalize(cross(up, vec3(0.0072f, 1.0f, 0.0034f)));
  vec3 forward = cross(right, up);

  return hemisphere.x * right + hemisphere.y * up + hemisphere.z * forward;
}

void main() {
  if (payload.rayActive == 0) { // 非活跃光线直接返回，不进行任何计算
    return;
  }

  // 获取当前相交的三角形的顶点索引
  ivec3 indices = ivec3(indexBuffer.data[3 * gl_PrimitiveID + 0],
                        indexBuffer.data[3 * gl_PrimitiveID + 1],
                        indexBuffer.data[3 * gl_PrimitiveID + 2]);
  // 计算重心坐标，将硬件给的(u,v)转换成(w,u,v)，其中w=1-u-v
  vec3 barycentric = vec3(1.0 - hitCoordinate.x - hitCoordinate.y,hitCoordinate.x, hitCoordinate.y);

  // 获取当前相交的三角形的三个顶点位置
  vec3 vertexA = vec3(vertexBuffer.data[3 * indices.x + 0],
                      vertexBuffer.data[3 * indices.x + 1],
                      vertexBuffer.data[3 * indices.x + 2]);
  vec3 vertexB = vec3(vertexBuffer.data[3 * indices.y + 0],
                      vertexBuffer.data[3 * indices.y + 1],
                      vertexBuffer.data[3 * indices.y + 2]);
  vec3 vertexC = vec3(vertexBuffer.data[3 * indices.z + 0],
                      vertexBuffer.data[3 * indices.z + 1],
                      vertexBuffer.data[3 * indices.z + 2]);
  // 计算击中点位置和几何法线
  vec3 position = vertexA * barycentric.x + vertexB * barycentric.y + vertexC * barycentric.z;
  vec3 geometricNormal = normalize(cross(vertexB - vertexA, vertexC - vertexA));

  // 获取当前相交的三角形对应的材质颜色
  vec3 surfaceColor = materialBuffer.data[materialIndexBuffer.data[gl_PrimitiveID]].diffuse;

  // 根据击中的物体，选择不同的处理逻辑
  if (gl_PrimitiveID == 40 || gl_PrimitiveID == 41) { // 击中光源 
    if (payload.rayDepth == 0) { // 第一条光线直接击中光源，直接光照
      payload.directColor = materialBuffer.data[materialIndexBuffer.data[gl_PrimitiveID]].emission;
    } else { // 经过多次反弹光线击中光源，间接光照
      float cosPrev = max(0.0, dot(payload.previousNormal, payload.rayDirection));
      payload.indirectColor += (1.0 / payload.rayDepth) * materialBuffer.data[materialIndexBuffer.data[gl_PrimitiveID]].emission 
          * cosPrev; // 累加到 indirectColor
    }
    payload.rayActive = 0;
  } else { // 击中普通物体
    // 随机采样光源位置
    // 随机选择一个光源三角形 40or41
    int randomIndex = int(random(gl_LaunchIDEXT.xy, camera.frameCount) * 2 + 40); 
    vec3 lightColor = vec3(0.6, 0.6, 0.6); // 光源颜色
    // 获取随机采样的光源三角形的索引和顶点位置
    ivec3 lightIndices = ivec3(indexBuffer.data[3 * randomIndex + 0],
                               indexBuffer.data[3 * randomIndex + 1],
                               indexBuffer.data[3 * randomIndex + 2]);
    vec3 lightVertexA = vec3(vertexBuffer.data[3 * lightIndices.x + 0],
                             vertexBuffer.data[3 * lightIndices.x + 1],
                             vertexBuffer.data[3 * lightIndices.x + 2]);
    vec3 lightVertexB = vec3(vertexBuffer.data[3 * lightIndices.y + 0],
                             vertexBuffer.data[3 * lightIndices.y + 1],
                             vertexBuffer.data[3 * lightIndices.y + 2]);
    vec3 lightVertexC = vec3(vertexBuffer.data[3 * lightIndices.z + 0],
                             vertexBuffer.data[3 * lightIndices.z + 1],
                             vertexBuffer.data[3 * lightIndices.z + 2]);
    // 在光源三角形上随机采样一个点作为光源位置
    vec2 uv = vec2(random(gl_LaunchIDEXT.xy, camera.frameCount), random(gl_LaunchIDEXT.xy, camera.frameCount + 1));
    if (uv.x + uv.y > 1.0f) {
      uv.x = 1.0f - uv.x;
      uv.y = 1.0f - uv.y;
    }
    vec3 lightBarycentric = vec3(1.0 - uv.x - uv.y, uv.x, uv.y); // 光源三角形上采样点的重心坐标
    // 计算光源位置
    vec3 lightPosition = lightVertexA * lightBarycentric.x +
                         lightVertexB * lightBarycentric.y +
                         lightVertexC * lightBarycentric.z;

    // 计算从击中点到光源位置的方向和距离
    vec3 positionToLightDirection = normalize(lightPosition - position);

    // 阴影判断。从当前击中点向光源发射一条阴影光线，如果这根光线在到达光源前被挡住了，则为阴影；否则计算光照亮度。
    vec3 shadowRayOrigin = position;
    vec3 shadowRayDirection = positionToLightDirection;
    // 最大距离，减去一个小偏移以避免自相交（浮点数精度问题可能导致光线在击中点附近就与自身相交了）
    float shadowRayDistance = length(lightPosition - position) - 0.001f; 

    uint shadowRayFlags = gl_RayFlagsTerminateOnFirstHitEXT | // 只要有任何相交就算遮挡了
                          gl_RayFlagsOpaqueEXT | // 不需要计算透明度，任何相交都算遮挡
                          gl_RayFlagsSkipClosestHitShaderEXT; // 阴影光线不需要执行任何Hit Shader

    isShadow = true;
    // 发射阴影光线，miss shader会把isShadow设置为false，表示没有被遮挡；
    // 如果被遮挡了，Hit Shader会把isShadow设置为true。
    traceRayEXT(topLevelAS, shadowRayFlags, 0xFF, 0, 0, 1, shadowRayOrigin,
                0.001, shadowRayDirection, shadowRayDistance, 1);

    if (!isShadow) { // 没有被遮挡，计算光照
      if (payload.rayDepth == 0) { // 第一条光线直接击中光源，直接光照
        payload.directColor = surfaceColor * lightColor * dot(geometricNormal, positionToLightDirection);
      } else { // 间接光照，路径越长贡献越小，所以除以rayDepth
        payload.indirectColor +=
            (1.0 / payload.rayDepth) * surfaceColor * lightColor *
            dot(payload.previousNormal, payload.rayDirection) *
            dot(geometricNormal, positionToLightDirection);
      }
    } else { // 被遮挡了
      if (payload.rayDepth == 0) { // 第一条光线直接击中光源，但被遮挡了，直接黑色
        payload.directColor = vec3(0.0, 0.0, 0.0);
      } else { // 间接光被挡住，这条路径终止，不再反弹
        payload.rayActive = 0;
      }
    }
      // 随机采样一个半球方向，继续追踪下一条光线
    vec3 hemisphere = uniformSampleHemisphere(
        vec2(random(gl_LaunchIDEXT.xy, camera.frameCount),
            random(gl_LaunchIDEXT.xy, camera.frameCount + 1)));
    // 将采样的半球方向与当前击中点的几何法线对齐，得到世界坐标系下的半球方向
    vec3 alignedHemisphere =
        alignHemisphereWithCoordinateSystem(hemisphere, geometricNormal);
    // 更新payload，继续追踪下一条光线
    payload.rayOrigin = position;
    payload.rayDirection = alignedHemisphere; // 下一条光线的方向是随机采样的半球方向
    payload.previousNormal = geometricNormal;
    payload.rayDepth += 1; // 光线深度加1
  }

}
