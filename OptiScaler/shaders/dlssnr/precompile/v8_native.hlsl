
#ifdef VK_MODE
[[vk::binding(0, 0)]]
cbuffer Params : register(b0, space0)
#else
cbuffer Params : register(b0)
#endif
{
    uint  gMode;
    float gWhitePoint;
    uint  gWidth;
    uint  gHeight;
    float gTransferStrength;
    float gColourStrength;
    uint  gDebugView;
    float gMaxRatio;
    uint  gPassthrough;
    float gMvScaleX;     // motion vector units -> pixels of this dispatch
    float gMvScaleY;
    uint  gGuideWidth;   // the motion texture's valid region
    uint  gGuideHeight;
    uint  gCompareMode;  // 0 off, 1 side by side, 2 wipe
    float gCompareSplit; // where the wipe cuts, 0..1
    float gCompareZoom;  // side by side: 1 fits the frame, 2 fills the half
    uint  gCompareSwap;  // put the edited frame on the other side
    uint  gTransfer;     // 0 classic, 1 matched residual -- how a below-size model comes back
    float gDebugScale;   // what the debug views are scaled by, held still while the meter moves
    uint  gPreserveHighFrequency;
    float gNetworkRatioX;
    float gNetworkRatioY;
    uint  gMotionAdaptive;
    float gMotionStart;
    float gMotionEnd;
    float gMismatchStart;
    float gMismatchEnd;
    float gFrequencyRadius;
    float gLumaTrust;
    float gChromaTrust;
    uint  gSourceWidth;
    uint  gSourceHeight;
    uint gGuidedReconstruction;
    float gPostSharpness;
    uint gCatmullRomInput;
    uint gExperimentalCompose;
    uint gValidX; uint gValidY; uint gValidWidth; uint gValidHeight;
    uint gMotionX; uint gMotionY;
    uint gHighlightEncoding;
    uint gRelativeColour;
    float gMaxDarken; // offset 176; auto preserves the fixed V8 0.5 lower bound

};
#if !defined(VK_MODE) && !defined(DX12_HIGHLIGHT_ENCODING)
#define gHighlightEncoding 0
#endif
#ifndef VK_MODE
#define gRelativeColour 0
#endif

// Bringing an impossible colour back into a possible one.
//
// A colour with a negative component is not a colour any display can show, and the composition can
// produce one: the model's answer is rescaled by a ratio and its chroma rebuilt, and either step can
// push a saturated pixel past the edge of the gamut.
//
// This used to be a hard clamp -- convert to AP1, max() every channel against zero, convert back --
// which is a per-channel operation on exactly the pixels most likely to breach, and per-channel
// operations on saturated pixels are the hue distorter this file warns about everywhere else. The
// channel that hits the wall first decides the colour of the rest.
//
// Instead the whole colour is scaled toward the neutral axis by one factor, so its hue survives and
// only its saturation gives way. And it is exactly nothing when nothing is out of gamut: with every
// component non-negative the scale is 1 and the colour comes back bit-for-bit.
//
// Taken from RenoDX's DLSS 5 addon by clshortfuse (https://github.com/clshortfuse/renodx), whose
// implementation this is -- the D65 adaptation state, the reversible scale and the LMS basis are
// theirs. See Licenses/RenoDX_ATTRIBUTION.txt.

float SanitizeFinite(float v, float fallback) { return isfinite(v) ? v : fallback; }

float3 SanitizeFinite3(float3 v, float3 fallback)
{
    return float3(SanitizeFinite(v.x, fallback.x), SanitizeFinite(v.y, fallback.y),
                  SanitizeFinite(v.z, fallback.z));
}

float SafeDivide(float numerator, float denominator, float fallback)
{
    return abs(denominator) > 1e-8 ? numerator / denominator : fallback;
}

// Hunt-Pointer-Estevez LMS over linear BT.709, carrying the fixed D65 adaptation state the
// compression is defined against. The signal itself never leaves BT.709.
float3 LMSToBT709(float3 color)
{
    const float3x3 m = { 5.62059812, -4.57145756, 0.15577924,
                         -1.15555585, 2.25800438, -0.15415806,
                         0.03059913, -0.19018011, 1.06820532 };
    return mul(m, color);
}

float3 BT709ToLMS(float3 color)
{
    const float3x3 m = { 0.30569589, 0.62271286, 0.04528636,
                         0.15776262, 0.76968599, 0.08807030,
                         0.01933082, 0.11919478, 0.95053215 };
    return mul(m, color);
}

// The neutral colour of the same luminance as what is being compressed -- the point everything is
// pulled toward, so that pulling changes saturation and not hue.
float3 D65NeutralBT709(float3 adaptiveStateLms, float luminance)
{
    float3 d65 = LMSToBT709(max(adaptiveStateLms, 1e-8));
    float d65Y = max(dot(d65, float3(0.2126, 0.7152, 0.0722)), 1e-8);
    return d65 * (luminance / d65Y);
}

// The largest scale toward the neutral axis that leaves no channel negative. One for a colour that
// was already representable, which is why this is safe to run on every pixel.
float GamutCompressionScale(float3 color, float3 adaptiveStateLms)
{
    color = SanitizeFinite3(color, float3(0.0, 0.0, 0.0));

    const float y = dot(color, float3(0.2126, 0.7152, 0.0722));

    if (!(y > 1e-8))
        return 1.0;

    const float3 neutral = D65NeutralBT709(adaptiveStateLms, y);
    float scale = 1.0;

    if (color.r < 0.0 && neutral.r > color.r)
        scale = min(scale, SafeDivide(neutral.r, neutral.r - color.r, 1.0));

    if (color.g < 0.0 && neutral.g > color.g)
        scale = min(scale, SafeDivide(neutral.g, neutral.g - color.g, 1.0));

    if (color.b < 0.0 && neutral.b > color.b)
        scale = min(scale, SafeDivide(neutral.b, neutral.b - color.b, 1.0));

    return saturate(SanitizeFinite(scale, 1.0));
}

float3 ClampAp1(float3 color)
{
    const float3 adaptiveStateLms = BT709ToLMS(float3(0.18, 0.18, 0.18));
    const float scale = GamutCompressionScale(color, adaptiveStateLms);

    // Nothing was out of gamut. Leave the colour exactly as it arrived.
    if (scale >= 1.0)
        return color;

    const float y = dot(color, float3(0.2126, 0.7152, 0.0722));
    const float3 neutral = D65NeutralBT709(adaptiveStateLms, y);

    return SanitizeFinite3(neutral + (color - neutral) * scale, max(neutral, 0.0));
}

// ---------------------------------------------------------------------------------------------
// The composition below (UpgradeToneMap's two-branch ratio, the OkLab hue correction, and the blend
// between a luminance-only result and the model's own colour) is taken from RenoDX's DLSS 5 addon by
// clshortfuse -- https://github.com/clshortfuse/renodx. It is their design, not ours; see
// Licenses/RenoDX_ATTRIBUTION.txt. The OkLab matrices are Bjorn Ottosson's published constants and the
// AP1, sRGB and PQ transforms are standard colour science.
// ---------------------------------------------------------------------------------------------

// OkLab, so the model's colour can be reached without its hue being invented on the way. A ratio
// applied to an RGB triple does not move hue, but a difference added to one does -- which is what the
// old composition did, and why a warm subject could come back green. Here the result's chroma is
// rebuilt in the model's own hue direction and only its magnitude is taken from the scaled colour.
float3 CbrtSigned(float3 v) { return sign(v) * pow(abs(v), 1.0 / 3.0); }

float3 ToOkLab(float3 color)
{
    const float3x3 rgb_to_lms = { 0.4122214708, 0.5363325363, 0.0514459929,
                                  0.2119034982, 0.6806995451, 0.1073969566,
                                  0.0883024619, 0.2817188376, 0.6299787005 };
    const float3x3 lms_to_lab = { 0.2104542553, 0.7936177850, -0.0040720468,
                                  1.9779984951, -2.4285922050, 0.4505937099,
                                  0.0259040371, 0.7827717662, -0.8086757660 };
    return mul(lms_to_lab, CbrtSigned(mul(rgb_to_lms, color)));
}

float3 FromOkLab(float3 lab)
{
    const float3x3 lab_to_lms = { 1.0, 0.3963377774, 0.2158037573,
                                  1.0, -0.1055613458, -0.0638541728,
                                  1.0, -0.0894841775, -1.2914855480 };
    const float3x3 lms_to_rgb = { 4.0767416621, -3.3077115913, 0.2309699292,
                                  -1.2684380046, 2.6097574011, -0.3413193965,
                                  -0.0041960863, -0.7034186147, 1.7076147010 };
    float3 lms = mul(lab_to_lms, lab);
    return mul(lms_to_rgb, lms * lms * lms);
}

// Takes the hue and the chroma direction from `correct`, and only the chroma magnitude from
// `incorrect`. Scaling a colour by a luminance ratio changes how saturated it reads; this puts the
// saturation back where the model meant it without letting the hue drift.
// Takes the hue and chroma direction from `correct` and only the chroma magnitude from
// `incorrect`, so a rescaled colour keeps the model's own hue rather than drifting toward whatever
// the scaling did to its channels.
float3 HueOkLab(float3 incorrect, float3 correct)
{
    float3 incorrectLab = ToOkLab(incorrect);
    const float3 correctLab = ToOkLab(correct);
    const float incorrectChroma = length(incorrectLab.yz);
    const float correctChroma = length(correctLab.yz);

    // Normalise the direction before scaling it, rather than scaling by a ratio of magnitudes.
    //
    // The two are the same algebra -- correctLab.yz * (incorrectChroma / correctChroma) is
    // (correctLab.yz / correctChroma) * incorrectChroma -- but only this order is bounded. The
    // other divides by correctChroma while guarding it with `== 0.0`, which is an exact float
    // comparison and so catches only a chroma that is precisely zero. A model pixel that is merely
    // very close to grey has a chroma of about 1e-7, sails past that guard, and turns a hue
    // direction with no meaningful magnitude into a multiplier of ten thousand. The result is a
    // saturated colour pulled out of numerical noise.
    //
    // Written this way the direction is unit length by construction and the result cannot exceed
    // incorrectChroma, whatever the model returned. Nioh 3 is where this showed: a night scene
    // leaves most of the frame near-achromatic, so near-zero chroma is the common case rather than
    // the edge, and the speckle it produced was reported as green noise.
    const float2 hueDirection = correctChroma > 1e-5 ? correctLab.yz / correctChroma : float2(0.0, 0.0);

    incorrectLab.yz = hueDirection * incorrectChroma;

    return ClampAp1(FromOkLab(incorrectLab));
}


// Fixed J0 V8: normalized luminance only; r=4, k=.05, no parameter search.
[[vk::binding(1,0)]] Texture2D<float4> gSource;
[[vk::binding(2,0)]] Texture2D<float4> gModel;
[[vk::binding(3,0)]] Texture2D<float4> gOriginal;
[[vk::binding(4,0)]] Texture2D<float4> gCoeff;
[[vk::binding(5,0)]] RWTexture2D<float4> gTarget;
[[vk::binding(6,0)]] RWTexture2D<float4> gKeep;
static const float3 LY=float3(.2126,.7152,.0722);
static const float W3[12]={0.0036891354301046668,0.015056142680948825,-0.033998631514275894,-0.066637317767980736,0.13550528412853946,0.44638538704266362,0.44638538704266362,0.13550528412853946,-0.066637317767980736,-0.033998631514275894,0.015056142680948825,0.0036891354301046668};
static const float T5[5]={1.0/9,2.0/9,3.0/9,2.0/9,1.0/9};
int2 fullClamp(int2 p){return clamp(p,int2(0,0),int2(gWidth,gHeight)-1);}
int2 lowClamp(int2 p){return clamp(p,int2(0,0),int2(gWidth/2,gHeight/2)-1);}
float3 decode3(float3 v){v=saturate(v);return float3(v.x<=.04045?v.x/12.92:pow((v.x+.055)/1.055,2.4),v.y<=.04045?v.y/12.92:pow((v.y+.055)/1.055,2.4),v.z<=.04045?v.z/12.92:pow((v.z+.055)/1.055,2.4));}
float4 linearLoad(Texture2D<float4> im,float2 p,int2 size){int2 b=(int2)floor(p);float2 f=p-b;int2 b0=clamp(b,0,size-1),b1=clamp(b+1,0,size-1);return lerp(lerp(im.Load(int3(b0,0)),im.Load(int3(b1.x,b0.y,0)),f.x),lerp(im.Load(int3(b0.x,b1.y,0)),im.Load(int3(b1,0)),f.x),f.y);}
float3 Pphysical(float3 o,float3 i,float3 m,float ym){
 float yo=dot(o,LY),yi=dot(i,LY);
 float ratio=yo<yi?yo/max(yi,1e-6):(ym+max(0,yo-yi))/max(ym,1e-30);
 float3 u=ym<=1e-5?o:HueOkLab(m*ratio,m);
 float lr=(dot(u,LY)+1.0/512)/(yo+1.0/512);
 return u*(clamp(lr,gMaxDarken>0?1.0/max(gMaxDarken,1.0):.5,2)/max(lr,1e-6))*gWhitePoint;
}
[numthreads(8,8,1)]void CSMain(uint3 id:SV_DispatchThreadID){
 int2 p=int2(id.xy);int2 wh=int2(gWidth,gHeight),lo=wh/2;
 if(gMode==40){ // SR Lanczos3 horizontal, 1920x2160. B0 phase2j+.5.
  if(any(p>=int2(lo.x,wh.y)))return;
  float s=0;
  [unroll]for(int k=0;k<12;++k)s+=W3[k]*dot(max(gOriginal.Load(int3(fullClamp(int2(2*p.x+k-5,p.y)),0)).rgb,0),LY)/gWhitePoint;
  gTarget[p]=float4(s,0,0,0);return;
 }
 if(gMode==41){ // SR vertical and deduplicated decoded MO moments.
  if(any(p>=lo))return;
  float s=0;
  [unroll]for(int k=0;k<12;++k)s+=W3[k]*gSource.Load(int3(p.x,clamp(2*p.y+k-5,0,wh.y-1),0)).x;
  float m=dot(decode3(gModel.Load(int3(p*2,0)).rgb),LY);
  gTarget[p]=float4(s,m,s*s,s*m);return;
 }
 if(gMode==42||gMode==44){
  if(any(p>=lo))return;
  float4 v=0;[unroll]for(int k=-4;k<=4;++k)v+=gSource.Load(int3(lowClamp(p+int2(k,0)),0));
  gTarget[p]=v/9;return;
 }
 if(gMode==43){
  if(any(p>=lo))return;
  float4 q=0;[unroll]for(int k=-4;k<=4;++k)q+=gSource.Load(int3(lowClamp(p+int2(0,k)),0));q/=9;
  float var=max(q.z-q.x*q.x,0),cov=q.w-q.x*q.y,eps=(.05*q.x)*(.05*q.x);float den=var+eps;
  float a=den>0?cov/den:0,b=q.y-a*q.x;gTarget[p]=float4(a,b,0,0);return;
 }
 if(gMode==45){
  if(any(p>=lo))return;
  float4 q=0;[unroll]for(int k=-4;k<=4;++k)q+=gSource.Load(int3(lowClamp(p+int2(0,k)),0));
  gTarget[p]=q/9;return;
 }
 if(any(p>=wh))return;
 if(gMode==46){ // P luma from Mhat, ORIGINAL P chroma for V2; point verdict for V8 Lm.
  float3 o=max(gOriginal.Load(int3(p,0)).rgb,0)/gWhitePoint;
  float2 uv=(float2(p)+.5)/float2(wh);float2 pos=uv*float2(wh)-.5;
  float3 i=decode3(linearLoad(gSource,pos,wh).rgb),m=decode3(linearLoad(gModel,pos,wh).rgb);
  float yo=dot(o,LY),yi=dot(i,LY),ym=dot(m,LY);
  float2 ab=linearLoad(gCoeff,(float2(p)+.5)/2-.5,lo).xy;
  float mh=max(ab.x*yo+ab.y,0);
  float3 oldP=Pphysical(o,i,m,ym),virtualM=(ym>0?m/ym:float3(1,1,1))*mh;
  float3 newP=Pphysical(o,i,virtualM,mh);float py=dot(newP,LY);
  float3 pc=oldP-dot(oldP,LY);gTarget[p]=float4(pc+py,1);
  float verdict=mh<=1e-5?yo:yo<yi?mh*yo/max(yi,1e-6):mh+max(0,yo-yi);
  gKeep[p]=float4(yo,verdict,py/gWhitePoint,0);return;
 }
 if(gMode==47){ // V2/V3 five-tap horizontal: chroma and low-frequency scalar fields.
  float3 cr=0;float2 q=0;
  [unroll]for(int k=-2;k<=2;++k){int2 t=fullClamp(p+int2(k,0));float3 rgb=gSource.Load(int3(t,0)).rgb;cr+=T5[k+2]*(rgb-dot(rgb,LY));q+=T5[k+2]*gModel.Load(int3(t,0)).xy;}
  float4 centre=gModel.Load(int3(p,0));gTarget[p]=float4(cr,0);gKeep[p]=float4(q,centre.x,centre.z);return;
 }
 if(gMode==48){ // Vertical tent + original PHF scalar guard; one final native output store.
  float3 cr=0;float2 q=0;
  [unroll]for(int k=-2;k<=2;++k){int2 t=fullClamp(p+int2(0,k));cr+=T5[k+2]*gSource.Load(int3(t,0)).rgb;q+=T5[k+2]*gModel.Load(int3(t,0)).xy;}
  float4 centre=gModel.Load(int3(p,0));float target=max(0,centre.z-q.x+q.y);
  float gain=clamp((target+1.0/512)/(centre.w+1.0/512),gMaxDarken>0?1.0/max(gMaxDarken,1.0):.5,2);
  gTarget[p]=float4(max((cr+centre.w*gWhitePoint)*gain,0),1);return;
 }
}
