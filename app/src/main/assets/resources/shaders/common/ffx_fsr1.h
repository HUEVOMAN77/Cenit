//_____________________________________________________________/\_______________________________________________________________
//==============================================================================================================================
//
//
//                    AMD FidelityFX SUPER RESOLUTION [FSR 1] ::: SPATIAL SCALING & EXTRAS - v1.20210629
//
//
//------------------------------------------------------------------------------------------------------------------------------
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------------------------------------------------------
// FidelityFX Super Resolution Sample
//
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
//------------------------------------------------------------------------------------------------------------------------------
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------------------------------------------------------
// ABOUT
// =====
// FSR is a collection of algorithms relating to generating a higher resolution image.
// This specific header focuses on single-image non-temporal image scaling, and related tools.
// 
// The core functions are EASU and RCAS:
//  [EASU] Edge Adaptive Spatial Upsampling ....... 1x to 4x area range spatial scaling, clamped adaptive elliptical filter.
//  [RCAS] Robust Contrast Adaptive Sharpening .... A non-scaling variation on CAS.
// RCAS needs to be applied after EASU as a separate pass.
// 
// Optional utility functions are:
//  [LFGA] Linear Film Grain Applicator ........... Tool to apply film grain after scaling.
//  [SRTM] Simple Reversible Tone-Mapper .......... Linear HDR {0 to FP16_MAX} to {0 to 1} and back.
//  [TEPD] Temporal Energy Preserving Dither ...... Temporally energy preserving dithered {0 to 1} linear to gamma 2.0 conversion.
// See each individual sub-section for inline documentation.
//------------------------------------------------------------------------------------------------------------------------------
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------------------------------------------------------
// FUNCTION PERMUTATIONS
// =====================
// *F() ..... Single item computation with 32-bit.
// *H() ..... Single item computation with 16-bit, with packing (aka two 16-bit ops in parallel) when possible.
// *Hx2() ... Processing two items in parallel with 16-bit, easier packing.
//            Not all interfaces in this file have a *Hx2() form.
//==============================================================================================================================
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//_____________________________________________________________/\_______________________________________________________________
//==============================================================================================================================
//
//                                        FSR - [EASU] EDGE ADAPTIVE SPATIAL UPSAMPLING
//
//------------------------------------------------------------------------------------------------------------------------------
// EASU provides a high quality spatial-only scaling at relatively low cost.
// Meaning EASU is appropiate for laptops and other low-end GPUs.
// Quality from 1x to 4x area scaling is good.
//------------------------------------------------------------------------------------------------------------------------------
// The scalar uses a modified fast approximation to the standard lanczos(size=2) kernel.
// EASU runs in a single pass, so it applies a directionally and anisotropically adaptive radial lanczos.
// This is also kept as simple as possible to have minimum runtime.
//------------------------------------------------------------------------------------------------------------------------------
// The lanzcos filter has negative lobes, so by itself it will introduce ringing.
// To remove all ringing, the algorithm uses the nearest 2x2 input texels as a neighborhood,
// and limits output to the minimum and maximum of that neighborhood.
//------------------------------------------------------------------------------------------------------------------------------
// Input image requirements:
// 
// Color needs to be encoded as 3 channel[red, green, blue](e.g.XYZ not supported)
// Each channel needs to be in the range[0, 1]
// Any color primaries are supported
// Display / tonemapping curve needs to be as if presenting to sRGB display or similar(e.g.Gamma 2.0)
// There should be no banding in the input
// There should be no high amplitude noise in the input
// There should be no noise in the input that is not at input pixel granularity
// For performance purposes, use 32bpp formats
//------------------------------------------------------------------------------------------------------------------------------
// Best to apply EASU at the end of the frame after tonemapping 
// but before film grain or composite of the UI.
//------------------------------------------------------------------------------------------------------------------------------
// Example of including this header for D3D HLSL :
// 
//  #define A_GPU 1
//  #define A_HLSL 1
//  #define A_HALF 1
//  #include "ffx_a.h"
//  #define FSR_EASU_H 1
//  #define FSR_RCAS_H 1
//  //declare input callbacks
//  #include "ffx_fsr1.h"
// 
// Example of including this header for Vulkan GLSL :
// 
//  #define A_GPU 1
//  #define A_GLSL 1
//  #define A_HALF 1
//  #include "ffx_a.h"
//  #define FSR_EASU_H 1
//  #define FSR_RCAS_H 1
//  //declare input callbacks
//  #include "ffx_fsr1.h"
// 
// Example of including this header for Vulkan HLSL :
// 
//  #define A_GPU 1
//  #define A_HLSL 1
//  #define A_HLSL_6_2 1
//  #define A_NO_16_BIT_CAST 1
//  #define A_HALF 1
//  #include "ffx_a.h"
//  #define FSR_EASU_H 1
//  #define FSR_RCAS_H 1
//  //declare input callbacks
//  #include "ffx_fsr1.h"
// 
//  Example of declaring the required input callbacks for GLSL :
//  The callbacks need to gather4 for each color channel using the specified texture coordinate 'p'.
//  EASU uses gather4 to reduce position computation logic and for free Arrays of Structures to Structures of Arrays conversion.
// 
//  AH4 FsrEasuRH(AF2 p){return AH4(textureGather(sampler2D(tex,sam),p,0));}
//  AH4 FsrEasuGH(AF2 p){return AH4(textureGather(sampler2D(tex,sam),p,1));}
//  AH4 FsrEasuBH(AF2 p){return AH4(textureGather(sampler2D(tex,sam),p,2));}
//  ...
//  The FsrEasuCon function needs to be called from the CPU or GPU to set up constants.
//  The difference in viewport and input image size is there to support Dynamic Resolution Scaling.
//  To use FsrEasuCon() on the CPU, define A_CPU before including ffx_a and ffx_fsr1.
//  Including a GPU example here, the 'con0' through 'con3' values would be stored out to a constant buffer.
//  AU4 con0,con1,con2,con3;
//  FsrEasuCon(con0,con1,con2,con3,
//    1920.0,1080.0,  // Viewport size (top left aligned) in the input image which is to be scaled.
//    3840.0,2160.0,  // The size of the input image.
//    2560.0,1440.0); // The output resolution.
//==============================================================================================================================
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//_____________________________________________________________/\_______________________________________________________________
//==============================================================================================================================
//                                                      CONSTANT SETUP
//==============================================================================================================================
// Call to setup required constant values (works on CPU or GPU).
A_STATIC void FsrEasuCon(
outAU4 con0,
outAU4 con1,
outAU4 con2,
outAU4 con3,
// This the rendered image resolution being upscaled
AF1 inputViewportInPixelsX,
AF1 inputViewportInPixelsY,
// This is the resolution of the resource containing the input image (useful for dynamic resolution)
AF1 inputSizeInPixelsX,
AF1 inputSizeInPixelsY,
// This is the display resolution which the input image gets upscaled to
AF1 outputSizeInPixelsX,
AF1 outputSizeInPixelsY){
 // Output integer position to a pixel position in viewport.
 con0[0]=AU1_AF1(inputViewportInPixelsX*ARcpF1(outputSizeInPixelsX));
 con0[1]=AU1_AF1(inputViewportInPixelsY*ARcpF1(outputSizeInPixelsY));
 con0[2]=AU1_AF1(AF1_(0.5)*inputViewportInPixelsX*ARcpF1(outputSizeInPixelsX)-AF1_(0.5));
 con0[3]=AU1_AF1(AF1_(0.5)*inputViewportInPixelsY*ARcpF1(outputSizeInPixelsY)-AF1_(0.5));
 // Viewport pixel position to normalized image space.
 // This is used to get upper-left of 'F' tap.
 con1[0]=AU1_AF1(ARcpF1(inputSizeInPixelsX));
 con1[1]=AU1_AF1(ARcpF1(inputSizeInPixelsY));
 // Centers of gather4, first offset from upper-left of 'F'.
 //      +---+---+
 //      |   |   |
 //      +--(0)--+
 //      | b | c |
 //  +---F---+---+---+
 //  | e | f | g | h |
 //  +--(1)--+--(2)--+
 //  | i | j | k | l |
 //  +---+---+---+---+
 //      | n | o |
 //      +--(3)--+
 //      |   |   |
 //      +---+---+
 con1[2]=AU1_AF1(AF1_( 1.0)*ARcpF1(inputSizeInPixelsX));
 con1[3]=AU1_AF1(AF1_(-1.0)*ARcpF1(inputSizeInPixelsY));
 // These are from (0) instead of 'F'.
 con2[0]=AU1_AF1(AF1_(-1.0)*ARcpF1(inputSizeInPixelsX));
 con2[1]=AU1_AF1(AF1_( 2.0)*ARcpF1(inputSizeInPixelsY));
 con2[2]=AU1_AF1(AF1_( 1.0)*ARcpF1(inputSizeInPixelsX));
 con2[3]=AU1_AF1(AF1_( 2.0)*ARcpF1(inputSizeInPixelsY));
 con3[0]=AU1_AF1(AF1_( 0.0)*ARcpF1(inputSizeInPixelsX));
 con3[1]=AU1_AF1(AF1_( 4.0)*ARcpF1(inputSizeInPixelsY));
 con3[2]=con3[3]=0;}

//If the an offset into the input image resource
A_STATIC void FsrEasuConOffset(
    outAU4 con0,
    outAU4 con1,
    outAU4 con2,
    outAU4 con3,
    // This the rendered image resolution being upscaled
    AF1 inputViewportInPixelsX,
    AF1 inputViewportInPixelsY,
    // This is the resolution of the resource containing the input image (useful for dynamic resolution)
    AF1 inputSizeInPixelsX,
    AF1 inputSizeInPixelsY,
    // This is the display resolution which the input image gets upscaled to
    AF1 outputSizeInPixelsX,
    AF1 outputSizeInPixelsY,
    // This is the input image offset into the resource containing it (useful for dynamic resolution)
    AF1 inputOffsetInPixelsX,
    AF1 inputOffsetInPixelsY) {
    FsrEasuCon(con0, con1, con2, con3, inputViewportInPixelsX, inputViewportInPixelsY, inputSizeInPixelsX, inputSizeInPixelsY, outputSizeInPixelsX, outputSizeInPixelsY);
    con0[2] = AU1_AF1(AF1_(0.5) * inputViewportInPixelsX * ARcpF1(outputSizeInPixelsX) - AF1_(0.5) + inputOffsetInPixelsX);
    con0[3] = AU1_AF1(AF1_(0.5) * inputViewportInPixelsY * ARcpF1(outputSizeInPixelsY) - AF1_(0.5) + inputOffsetInPixelsY);
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//_____________________________________________________________/\_______________________________________________________________
//==============================================================================================================================
//                                                   NON-PACKED 32-BIT VERSION
//==============================================================================================================================
#if defined(A_GPU)&&defined(FSR_EASU_F)
 // Input callback prototypes, need to be implemented by calling shader
 AF4 FsrEasuRF(AF2 p);
 AF4 FsrEasuGF(AF2 p);
 AF4 FsrEasuBF(AF2 p);
//------------------------------------------------------------------------------------------------------------------------------
 // Filtering for a given tap for the scalar.
 void FsrEasuTapF(
 inout AF3 aC, // Accumulated color, with negative lobe.
 inout AF1 aW, // Accumulated weight.
 AF2 off, // Pixel offset from resolve position to tap.
 AF2 dir, // Gradient direction.
 AF2 len, // Length.
 AF1 lob, // Negative lobe strength.
 AF1 clp, // Clipping point.
 AF3 c){ // Tap color.
  // Rotate offset by direction.
  AF2 v;
  v.x=(off.x*( dir.x))+(off.y*dir.y);
  v.y=(off.x*(-dir.y))+(off.y*dir.x);
  // Anisotropy.
  v*=len;
  // Compute distance^2.
  AF1 d2=v.x*v.x+v.y*v.y;
  // Limit to the window as at corner, 2 taps can easily be outside.
  d2=min(d2,clp);
  // Approximation of lancos2 without sin() or rcp(), or sqrt() to get x.
  //  (25/16 * (2/5 * x^2 - 1)^2 - (25/16 - 1)) * (1/4 * x^2 - 1)^2
  //  |_______________________________________|   |_______________|
  //                   base                             window
  // The general form of the 'base' is,
  //  (a*(b*x^2-1)^2-(a-1))
  // Where 'a=1/(2*b-b^2)' and 'b' moves around the negative lobe.
  AF1 wB=AF1_(2.0/5.0)*d2+AF1_(-1.0);
  AF1 wA=lob*d2+AF1_(-1.0);
  wB*=wB;
  wA*=wA;
  wB=AF1_(25.0/16.0)*wB+AF1_(-(25.0/16.0-1.0));
  AF1 w=wB*wA;
  // Do weighted average.
  aC+=c*w;aW+=w;}
//------------------------------------------------------------------------------------------------------------------------------
 // Accumulate direction and length.
 void FsrEasuSetF(
 inout AF2 dir,
 inout AF1 len,
 AF2 pp,
 AP1 biS,AP1 biT,AP1 biU,AP1 biV,
 AF1 lA,AF1 lB,AF1 lC,AF1 lD,AF1 lE){
  // Compute bilinear weight, branches factor out as predicates are compiler time immediates.
  //  s t
  //  u v
  AF1 w = AF1_(0.0);
  if(biS)w=(AF1_(1.0)-pp.x)*(AF1_(1.0)-pp.y);
  if(biT)w=           pp.x *(AF1_(1.0)-pp.y);
  if(biU)w=(AF1_(1.0)-pp.x)*           pp.y ;
  if(biV)w=           pp.x *           pp.y ;
  // Direction is the '+' diff.
  //    a
  //  b c d
  //    e
  // Then takes magnitude from abs average of both sides of 'c'.
  // Length converts gradient reversal to 0, smoothly to non-reversal at 1, shaped, then adding horz and vert terms.
  AF1 dc=lD-lC;
  AF1 cb=lC-lB;
  AF1 lenX=max(abs(dc),abs(cb));
  lenX=APrxLoRcpF1(lenX);
  AF1 dirX=lD-lB;
  dir.x+=dirX*w;
  lenX=ASatF1(abs(dirX)*lenX);
  lenX*=lenX;
  len+=lenX*w;
  // Repeat for the y axis.
  AF1 ec=lE-lC;
  AF1 ca=lC-lA;
  AF1 lenY=max(abs(ec),abs(ca));
  lenY=APrxLoRcpF1(lenY);
  AF1 dirY=lE-lA;
  dir.y+=dirY*w;
  lenY=ASatF1(abs(dirY)*lenY);
  lenY*=lenY;
  len+=lenY*w;}
//------------------------------------------------------------------------------------------------------------------------------
 void FsrEasuF(
 out AF3 pix,
 AU2 ip, // Integer pixel position in output.
 AU4 con0, // Constants generated by FsrEasuCon().
 AU4 con1,
 AU4 con2,
 AU4 con3){
//------------------------------------------------------------------------------------------------------------------------------
  // Get position of 'f'.
  AF2 pp=AF2(ip)*AF2_AU2(con0.xy)+AF2_AU2(con0.zw);
  AF2 fp=floor(pp);
  pp-=fp;
//------------------------------------------------------------------------------------------------------------------------------
  // 12-tap kernel.
  //    b c
  //  e f g h
  //  i j k l
  //    n o
  // Gather 4 ordering.
  //  a b
  //  r g
  // For packed FP16, need either {rg} or {ab} so using the following setup for gather in all versions,
  //    a b    <- unused (z)
  //    r g
  //  a b a b
  //  r g r g
  //    a b
  //    r g    <- unused (z)
  // Allowing dead-code removal to remove the 'z's.
  AF2 p0=fp*AF2_AU2(con1.xy)+AF2_AU2(con1.zw);
  // These are from p0 to avoid pulling two constants on pre-Navi hardware.
  AF2 p1=p0+AF2_AU2(con2.xy);
  AF2 p2=p0+AF2_AU2(con2.zw);
  AF2 p3=p0+AF2_AU2(con3.xy);
  AF4 bczzR=FsrEasuRF(p0);
  AF4 bczzG=FsrEasuGF(p0);
  AF4 bczzB=FsrEasuBF(p0);
  AF4 ijfeR=FsrEasuRF(p1);
  AF4 ijfeG=FsrEasuGF(p1);
  AF4 ijfeB=FsrEasuBF(p1);
  AF4 klhgR=FsrEasuRF(p2);
  AF4 klhgG=FsrEasuGF(p2);
  AF4 klhgB=FsrEasuBF(p2);
  AF4 zzonR=FsrEasuRF(p3);
  AF4 zzonG=FsrEasuGF(p3);
  AF4 zzonB=FsrEasuBF(p3);
//------------------------------------------------------------------------------------------------------------------------------
  // Simplest multi-channel approximate luma possible (luma times 2, in 2 FMA/MAD).
  AF4 bczzL=bczzB*AF4_(0.5)+(bczzR*AF4_(0.5)+bczzG);
  AF4 ijfeL=ijfeB*AF4_(0.5)+(ijfeR*AF4_(0.5)+ijfeG);
  AF4 klhgL=klhgB*AF4_(0.5)+(klhgR*AF4_(0.5)+klhgG);
  AF4 zzonL=zzonB*AF4_(0.5)+(zzonR*AF4_(0.5)+zzonG);
  // Rename.
  AF1 bL=bczzL.x;
  AF1 cL=bczzL.y;
  AF1 iL=ijfeL.x;
  AF1 jL=ijfeL.y;
  AF1 fL=ijfeL.z;
  AF1 eL=ijfeL.w;
  AF1 kL=klhgL.x;
  AF1 lL=klhgL.y;
  AF1 hL=klhgL.z;
  AF1 gL=klhgL.w;
  AF1 oL=zzonL.z;
  AF1 nL=zzonL.w;
  // Accumulate for bilinear interpolation.
  AF2 dir=AF2_(0.0);
  AF1 len=AF1_(0.0);
  FsrEasuSetF(dir,len,pp,true, false,false,false,bL,eL,fL,gL,jL);
  FsrEasuSetF(dir,len,pp,false,true ,false,false,cL,fL,gL,hL,kL);
  FsrEasuSetF(dir,len,pp,false,false,true ,false,fL,iL,jL,kL,nL);
  FsrEasuSetF(dir,len,pp,false,false,false,true ,gL,jL,kL,lL,oL);
//------------------------------------------------------------------------------------------------------------------------------
  // Normalize with approximation, and cleanup close to zero.
  AF2 dir2=dir*dir;
  AF1 dirR=dir2.x+dir2.y;
  AP1 zro=dirR<AF1_(1.0/32768.0);
  dirR=APrxLoRsqF1(dirR);
  dirR=zro?AF1_(1.0):dirR;
  dir.x=zro?AF1_(1.0):dir.x;
  dir*=AF2_(dirR);
  // Transform from {0 to 2} to {0 to 1} range, and shape with square.
  len=len*AF1_(0.5);
  len*=len;
  // Stretch kernel {1.0 vert|horz, to sqrt(2.0) on diagonal}.
  AF1 stretch=(dir.x*dir.x+dir.y*dir.y)*APrxLoRcpF1(max(abs(dir.x),abs(dir.y)));
  // Anisotropic length after rotation,
  //  x := 1.0 lerp to 'stretch' on edges
  //  y := 1.0 lerp to 2x on edges
  AF2 len2=AF2(AF1_(1.0)+(stretch-AF1_(1.0))*len,AF1_(1.0)+AF1_(-0.5)*len);
  // Based on the amount of 'edge',
  // the window shifts from +/-{sqrt(2.0) to slightly beyond 2.0}.
  AF1 lob=AF1_(0.5)+AF1_((1.0/4.0-0.04)-0.5)*len;
  // Set distance^2 clipping point to the end of the adjustable window.
  AF1 clp=APrxLoRcpF1(lob);
//------------------------------------------------------------------------------------------------------------------------------
  // Accumulation mixed with min/max of 4 nearest.
  //    b c
  //  e f g h
  //  i j k l
  //    n o
  AF3 min4=min(AMin3F3(AF3(ijfeR.z,ijfeG.z,ijfeB.z),AF3(klhgR.w,klhgG.w,klhgB.w),AF3(ijfeR.y,ijfeG.y,ijfeB.y)),
               AF3(klhgR.x,klhgG.x,klhgB.x));
  AF3 max4=max(AMax3F3(AF3(ijfeR.z,ijfeG.z,ijfeB.z),AF3(klhgR.w,klhgG.w,klhgB.w),AF3(ijfeR.y,ijfeG.y,ijfeB.y)),
               AF3(klhgR.x,klhgG.x,klhgB.x));
  // Accumulation.
  AF3 aC=AF3_(0.0);
  AF1 aW=AF1_(0.0);
  FsrEasuTapF(aC,aW,AF2( 0.0,-1.0)-pp,dir,len2,lob,clp,AF3(bczzR.x,bczzG.x,bczzB.x)); // b
  FsrEasuTapF(aC,aW,AF2( 1.0,-1.0)-pp,dir,len2,lob,clp,AF3(bczzR.y,bczzG.y,bczzB.y)); // c
  FsrEasuTapF(aC,aW,AF2(-1.0, 1.0)-pp,dir,len2,lob,clp,AF3(ijfeR.x,ijfeG.x,ijfeB.x)); // i
  FsrEasuTapF(aC,aW,AF2( 0.0, 1.0)-pp,dir,len2,lob,clp,AF3(ijfeR.y,ijfeG.y,ijfeB.y)); // j
  FsrEasuTapF(aC,aW,AF2( 0.0, 0.0)-pp,dir,len2,lob,clp,AF3(ijfeR.z,ijfeG.z,ijfeB.z)); // f
  FsrEasuTapF(aC,aW,AF2(-1.0, 0.0)-pp,dir,len2,lob,clp,AF3(ijfeR.w,ijfeG.w,ijfeB.w)); // e
  FsrEasuTapF(aC,aW,AF2( 1.0, 1.0)-pp,dir,len2,lob,clp,AF3(klhgR.x,klhgG.x,klhgB.x)); // k
  FsrEasuTapF(aC,aW,AF2( 2.0, 1.0)-pp,dir,len2,lob,clp,AF3(klhgR.y,klhgG.y,klhgB.y)); // l
  FsrEasuTapF(aC,aW,AF2( 2.0, 0.0)-pp,dir,len2,lob,clp,AF3(klhgR.z,klhgG.z,klhgB.z)); // h
  FsrEasuTapF(aC,aW,AF2( 1.0, 0.0)-pp,dir,len2,lob,clp,AF3(klhgR.w,klhgG.w,klhgB.w)); // g
  FsrEasuTapF(aC,aW,AF2( 1.0, 2.0)-pp,dir,len2,lob,clp,AF3(zzonR.z,zzonG.z,zzonB.z)); // o
  FsrEasuTapF(aC,aW,AF2( 0.0, 2.0)-pp,dir,len2,lob,clp,AF3(zzonR.w,zzonG.w,zzonB.w)); // n
//------------------------------------------------------------------------------------------------------------------------------
  // Normalize and dering.
  pix=min(max4,max(min4,aC*AF3_(ARcpF1(aW))));}
#endif
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//_____________________________________________________________/\_______________________________________________________________
//==============================================================================================================================
//                                                    PACKED 16-BIT VERSION
//==============================================================================================================================
#if defined(A_GPU)&&defined(A_HALF)&&defined(FSR_EASU_H)
// Input callback prototypes, need to be implemented by calling shader
 AH4 FsrEasuRH(AF2 p);
 AH4 FsrEasuGH(AF2 p);
 AH4 FsrEasuBH(AF2 p);
//------------------------------------------------------------------------------------------------------------------------------
 // This runs 2 taps in parallel.
 void FsrEasuTapH(
 inout AH2 aCR,inout AH2 aCG,inout AH2 aCB,
 inout AH2 aW,
 AH2 offX,AH2 offY,
 AH2 dir,
 AH2 len,
 AH1 lob,
 AH1 clp,
 AH2 cR,AH2 cG,AH2 cB){
  AH2 vX,vY;
  vX=offX*  dir.xx +offY*dir.yy;
  vY=offX*(-dir.yy)+offY*dir.xx;
  vX*=len.x;vY*=len.y;
  AH2 d2=vX*vX+vY*vY;
  d2=min(d2,AH2_(clp));
  AH2 wB=AH2_(2.0/5.0)*d2+AH2_(-1.0);
  AH2 wA=AH2_(lob)*d2+AH2_(-1.0);
  wB*=wB;
  wA*=wA;
  wB=AH2_(25.0/16.0)*wB+AH2_(-(25.0/16.0-1.0));
  AH2 w=wB*wA;
  aCR+=cR*w;aCG+=cG*w;aCB+=cB*w;aW+=w;}
//------------------------------------------------------------------------------------------------------------------------------
 // This runs 2 taps in parallel.
 void FsrEasuSetH(
 inout AH2 dirPX,inout AH2 dirPY,
 inout AH2 lenP,
 AH2 pp,
 AP1 biST,AP1 biUV,
 AH2 lA,AH2 lB,AH2 lC,AH2 lD,AH2 lE){
  AH2 w = AH2_(0.0);
  if(biST)w=(AH2(1.0,0.0)+AH2(-pp.x,pp.x))*AH2_(AH1_(1.0)-pp.y);
  if(biUV)w=(AH2(1.0,0.0)+AH2(-pp.x,pp.x))*AH2_(          pp.y);
  // ABS is not free in the packed FP16 path.
  AH2 dc=lD-lC;
  AH2 cb=lC-lB;
  AH2 lenX=max(abs(dc),abs(cb));
  lenX=ARcpH2(lenX);
  AH2 dirX=lD-lB;
  dirPX+=dirX*w;
  lenX=ASatH2(abs(dirX)*lenX);
  lenX*=lenX;
  lenP+=lenX*w;
  AH2 ec=lE-lC;
  AH2 ca=lC-lA;
  AH2 lenY=max(abs(ec),abs(ca));
  lenY=ARcpH2(lenY);
  AH2 dirY=lE-lA;
  dirPY+=dirY*w;
  lenY=ASatH2(abs(dirY)*lenY);
  lenY*=lenY;
  lenP+=lenY*w;}
//------------------------------------------------------------------------------------------------------------------------------
 void FsrEasuH(
 out AH3 pix,
 AU2 ip,
 AU4 con0,
 AU4 con1,
 AU4 con2,
 AU4 con3){
//------------------------------------------------------------------------------------------------------------------------------
  AF2 pp=AF2(ip)*AF2_AU2(con0.xy)+AF2_AU2(con0.zw);
  AF2 fp=floor(pp);
  pp-=fp;
  AH2 ppp=AH2(pp);
//------------------------------------------------------------------------------------------------------------------------------
  AF2 p0=fp*AF2_AU2(con1.xy)+AF2_AU2(con1.zw);
  AF2 p1=p0+AF2_AU2(con2.xy);
  AF2 p2=p0+AF2_AU2(con2.zw);
  AF2 p3=p0+AF2_AU2(con3.xy);
  AH4 bczzR=FsrEasuRH(p0);
  AH4 bczzG=FsrEasuGH(p0);
  AH4 bczzB=FsrEasuBH(p0);
  AH4 ijfeR=FsrEasuRH(p1);
  AH4 ijfeG=FsrEasuGH(p1);
  AH4 ijfeB=FsrEasuBH(p1);
  AH4 klhgR=FsrEasuRH(p2);
  AH4 klhgG=FsrEasuGH(p2);
  AH4 klhgB=FsrEasuBH(p2);
  AH4 zzonR=FsrEasuRH(p3);
  AH4 zzonG=FsrEasuGH(p3);
  AH4 zzonB=FsrEasuBH(p3);
//------------------------------------------------------------------------------------------------------------------------------
  AH4 bczzL=bczzB*AH4_(0.5)+(bczzR*AH4_(0.5)+bczzG);
  AH4 ijfeL=ijfeB*AH4_(0.5)+(ijfeR*AH4_(0.5)+ijfeG);
  AH4 klhgL=klhgB*AH4_(0.5)+(klhgR*AH4_(0.5)+klhgG);
  AH4 zzonL=zzonB*AH4_(0.5)+(zzonR*AH4_(0.5)+zzonG);
  AH1 bL=bczzL.x;
  AH1 cL=bczzL.y;
  AH1 iL=ijfeL.x;
  AH1 jL=ijfeL.y;
  AH1 fL=ijfeL.z;
  AH1 eL=ijfeL.w;
  AH1 kL=klhgL.x;
  AH1 lL=klhgL.y;
  AH1 hL=klhgL.z;
  AH1 gL=klhgL.w;
  AH1 oL=zzonL.z;
  AH1 nL=zzonL.w;
  // This part is different, accumulating 2 taps in parallel.
  AH2 dirPX=AH2_(0.0);
  AH2 dirPY=AH2_(0.0);
  AH2 lenP=AH2_(0.0);
  FsrEasuSetH(dirPX,dirPY,lenP,ppp,true, false,AH2(bL,cL),AH2(eL,fL),AH2(fL,gL),AH2(gL,hL),AH2(jL,kL));
  FsrEasuSetH(dirPX,dirPY,lenP,ppp,false,true ,AH2(fL,gL),AH2(iL,jL),AH2(jL,kL),AH2(kL,lL),AH2(nL,oL));
  AH2 dir=AH2(dirPX.r+dirPX.g,dirPY.r+dirPY.g);
  AH1 len=lenP.r+lenP.g;
//------------------------------------------------------------------------------------------------------------------------------
  AH2 dir2=dir*dir;
  AH1 dirR=dir2.x+dir2.y;
  AP1 zro=dirR<AH1_(1.0/32768.0);
  dirR=APrxLoRsqH1(dirR);
  dirR=zro?AH1_(1.0):dirR;
  dir.x=zro?AH1_(1.0):dir.x;
  dir*=AH2_(dirR);
  len=len*AH1_(0.5);
  len*=len;
  AH1 stretch=(dir.x*dir.x+dir.y*dir.y)*APrxLoRcpH1(max(abs(dir.x),abs(dir.y)));
  AH2 len2=AH2(AH1_(1.0)+(stretch-AH1_(1.0))*len,AH1_(1.0)+AH1_(-0.5)*len);
  AH1 lob=AH1_(0.5)+AH1_((1.0/4.0-0.04)-0.5)*len;
  AH1 clp=APrxLoRcpH1(lob);
//------------------------------------------------------------------------------------------------------------------------------
  // FP16 is different, using packed trick to do min and max in same operation.
  AH2 bothR=max(max(AH2(-ijfeR.z,ijfeR.z),AH2(-klhgR.w,klhgR.w)),max(AH2(-ijfeR.y,ijfeR.y),AH2(-klhgR.x,klhgR.x)));
  AH2 bothG=max(max(AH2(-ijfeG.z,ijfeG.z),AH2(-klhgG.w,klhgG.w)),max(AH2(-ijfeG.y,ijfeG.y),AH2(-klhgG.x,klhgG.x)));
  AH2 bothB=max(max(AH2(-ijfeB.z,ijfeB.z),AH2(-klhgB.w,klhgB.w)),max(AH2(-ijfeB.y,ijfeB.y),AH2(-klhgB.x,klhgB.x)));
  // This part is different for FP16, working pairs of taps at a time.
  AH2 pR=AH2_(0.0);
  AH2 pG=AH2_(0.0);
  AH2 pB=AH2_(0.0);
  AH2 pW=AH2_(0.0);
  FsrEasuTapH(pR,pG,pB,pW,AH2( 0.0, 1.0)-ppp.xx,AH2(-1.0,-1.0)-ppp.yy,dir,len2,lob,clp,bczzR.xy,bczzG.xy,bczzB.xy);
  FsrEasuTapH(pR,pG,pB,pW,AH2(-1.0, 0.0)-ppp.xx,AH2( 1.0, 1.0)-ppp.yy,dir,len2,lob,clp,ijfeR.xy,ijfeG.xy,ijfeB.xy);
  FsrEasuTapH(pR,pG,pB,pW,AH2( 0.0,-1.0)-ppp.xx,AH2( 0.0, 0.0)-ppp.yy,dir,len2,lob,clp,ijfeR.zw,ijfeG.zw,ijfeB.zw);
  FsrEasuTapH(pR,pG,pB,pW,AH2( 1.0, 2.0)-ppp.xx,AH2( 1.0, 1.0)-ppp.yy,dir,len2,lob,clp,klhgR.xy,klhgG.xy,klhgB.xy);
  FsrEasuTapH(pR,pG,pB,pW,AH2( 2.0, 1.0)-ppp.xx,AH2( 0.0, 0.0)-ppp.yy,dir,len2,lob,clp,klhgR.zw,klhgG.zw,klhgB.zw);
  FsrEasuTapH(pR,pG,pB,pW,AH2( 1.0, 0.0)-ppp.xx,AH2( 2.0, 2.0)-ppp.yy,dir,len2,lob,clp,zzonR.zw,zzonG.zw,zzonB.zw);
  AH3 aC=AH3(pR.x+pR.y,pG.x+pG.y,pB.x+pB.y);
  AH1 aW=pW.x+pW.y;
//------------------------------------------------------------------------------------------------------------------------------
  // Slightly different for FP16 version due to combined min and max.
  pix=min(AH3(bothR.y,bothG.y,bothB.y),max(-AH3(bothR.x,bothG.x,bothB.x),aC*AH3_(ARcpH1(aW))));}
#endif

//------------------------------------------------------------------------------------------------------------------------------
// Cenit 0.6.25: recortado a la seccion EASU (hasta el bloque [RCAS] original).
// Se trajeron la cabecera MIT, el setup de constantes (FsrEasuCon/FsrEasuConOffset)
// y las variantes F/H. Fuera: RCAS (Cenit ya tiene su propio CAS en ffx_cas.h) y
// los extras LFGA/SRTM/TEPD, que usan macros (APrxMedRcpF3, AGtZeroF3, AGtZeroH2)
// que NO existen en el ffx_a.h de 2019 que este proyecto ya usa. Sin recortar, eso
// no compila. Fuente: GPUOpen-Effects/FidelityFX-FSR, ffx-fsr/ffx_fsr1.h v1.20210629.
//------------------------------------------------------------------------------------------------------------------------------
