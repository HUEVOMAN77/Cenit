// Cenit 0.6.25: reconstruccion EASU (AMD FidelityFX Super Resolution 1).
// Basado en FidelityFX-FSR sample/src/VK/FSR_Pass.glsl
// Copyright (c) 2021 Advanced Micro Devices, Inc. All rights reserved.
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files(the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions :
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

// Variante F (32 bits), no H (16 bits): la version empaquetada de EASU pide
// GL_EXT_shader_16bit_storage / GL_EXT_shader_atomic_add / storage buffers, que
// este pipeline no declara. Cas.glsl hace lo mismo: usa la ruta no empaquetada.

#version 460 core
#extension GL_EXT_samplerless_texture_functions : require

layout(push_constant) uniform const_buffer
{
    uvec4 easuConst0;
    uvec4 easuConst1;
    uvec4 easuConst2;
    uvec4 easuConst3;
};

layout(set = 0, binding = 0) uniform texture2D imgSrc;
layout(set = 0, binding = 1, rgba8) uniform writeonly image2D imgDst;
layout(set = 0, binding = 2) uniform sampler imgSampler;

#define A_GPU 1
#define A_GLSL 1

#include "ffx_a.h"

// Activa la variante de 32 bits del kernel EASU dentro de ffx_fsr1.h. Sin este
// define el header no emite ninguna funcion.
#define FSR_EASU_F 1

// EASU lee con textureGather (4 texelazos por canal), no con texelFetch como CAS.
// El sampler se pasa aparte: imgSrc es una texture sin sampler asociado, igual que
// en cas.glsl, y aqui los unimos en el momento de la coleta.
AF4 FsrEasuRF(AF2 p) { return textureGather(sampler2D(imgSrc, imgSampler), p, 0); }
AF4 FsrEasuGF(AF2 p) { return textureGather(sampler2D(imgSrc, imgSampler), p, 1); }
AF4 FsrEasuBF(AF2 p) { return textureGather(sampler2D(imgSrc, imgSampler), p, 2); }

#include "ffx_fsr1.h"

layout(local_size_x = 64) in;
void main()
{
    // Mismo swizzle de trabajo que cas.glsl (ARmp8x8 sobre un bloque 16x16), para
    // que el patron de acceso a memoria se parezca al del resto de los computes.
    AU2 gxy = ARmp8x8(gl_LocalInvocationID.x) + AU2(gl_WorkGroupID.x << 4u, gl_WorkGroupID.y << 4u);

    AF3 c;
    FsrEasuF(c, gxy, easuConst0, easuConst1, easuConst2, easuConst3);
    imageStore(imgDst, ASU2(gxy), AF4(c, 1.0f));
    gxy.x += 8u;
    FsrEasuF(c, gxy, easuConst0, easuConst1, easuConst2, easuConst3);
    imageStore(imgDst, ASU2(gxy), AF4(c, 1.0f));
    gxy.y += 8u;
    FsrEasuF(c, gxy, easuConst0, easuConst1, easuConst2, easuConst3);
    imageStore(imgDst, ASU2(gxy), AF4(c, 1.0f));
    gxy.x -= 8u;
    FsrEasuF(c, gxy, easuConst0, easuConst1, easuConst2, easuConst3);
    imageStore(imgDst, ASU2(gxy), AF4(c, 1.0f));
}
