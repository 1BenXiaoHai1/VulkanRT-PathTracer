#version 460
#extension GL_EXT_ray_tracing : require

// isShadow是专门为阴影光线准备的payload变量
layout(location = 1) rayPayloadInEXT bool isShadow;

// 阴影光线的Miss Shader只要把isShadow设置为false，表示没有被遮挡；
// 如果被遮挡了，Hit Shader会把isShadow设置为true。
void main() { 
    isShadow = false; 
}


