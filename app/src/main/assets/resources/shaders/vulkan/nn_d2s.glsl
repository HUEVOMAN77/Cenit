// MIT License
//
// Copyright (c) 2019-2021 bloc97
// All rights reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//
// Adaptado de Anime4K v3.2 "Upscale_CNN_x2_S" (bloc97) para el compute de Cenit:
// el hook de mpv pasa a ser un dispatch uno a uno, las muestras pasan de
// texture() a texelFetch con borde fijado, y los pesos no se tocan.
//
// Cenit 0.6.41 — pasada final depth-to-space x2 de la cadena neuronal.
// Cada pixel de salida toma un canal del pixel de convolucion que le toca
// (bloque 2x2: x0y0, x1y0, x0y1, x1y1) y le suma la base bilineal, el
// residual que pide Anime4K ("return vec4(c0..) + MAIN_tex(MAIN_pos)").

#version 460 core
#extension GL_EXT_samplerless_texture_functions : require

layout(push_constant) uniform nn_consts
{
    // xy = offset de la imagen de entrada dentro de imgSrc, zw = tamano a procesar.
    ivec4 srcRect;
} pc;

layout(set = 0, binding = 0) uniform texture2D imgSrc;
layout(set = 0, binding = 1, rgba16f) uniform writeonly image2D imgDst;
layout(set = 0, binding = 2) uniform sampler imgSampler;
layout(set = 0, binding = 3) uniform texture2D imgBase;

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

// Borde fijado (repite el pixel del borde): lo mismo que hace el hook de mpv
// cuando una muestra cae fuera de la textura.
ivec2 nnClamp(ivec2 p)
{
    return clamp(p, pc.srcRect.xy, pc.srcRect.xy + pc.srcRect.zw - 1);
}

vec4 nnFetch(ivec2 off)
{
    return texelFetch(imgSrc, nnClamp(ivec2(gl_GlobalInvocationID.xy) + off), 0);
}

bool nnOutOfRange(ivec2 p)
{
    const ivec2 size = imageSize(imgDst);
    return p.x >= size.x || p.y >= size.y;
}

void main()
{
    const ivec2 d = ivec2(gl_GlobalInvocationID.xy);
    if (nnOutOfRange(d))
        return;

    const ivec2 s = d / 2;
    const vec4 conv = texelFetch(imgSrc, s, 0);
    const int channel = (d.y & 1) * 2 + (d.x & 1);
    const float residual = conv[channel];

    // Centro del pixel destino proyectado a la recta fuente, en texeles.
    const vec2 src_px = vec2(pc.srcRect.xy) + (vec2(d) + 0.5) * 0.5;
    const vec2 base_uv = src_px / vec2(textureSize(imgBase, 0));
    const vec3 base = textureLod(sampler2D(imgBase, imgSampler), base_uv, 0.0).rgb;

    imageStore(imgDst, d, vec4(base + residual, 1.0));
}
