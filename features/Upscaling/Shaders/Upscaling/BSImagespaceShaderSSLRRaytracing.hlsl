Texture2D<float4> t3 : register(t3);

Texture2D<float4> t2 : register(t2);

Texture2D<float4> t1 : register(t1);

Texture2D<float4> t0 : register(t0);

SamplerState s3_s : register(s3);

SamplerState s2_s : register(s2);

SamplerState s1_s : register(s1);

cbuffer cb0 : register(b0)
{
  float4 cb0[1];
}

#define cmp -

void main(
  float4 v0 : SV_POSITION0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  const float4 icb[] = { { 0, 0, 0, 0},
                              { 0.500000, 0, 0, 0},
                              { 0.125000, 0, 0, 0},
                              { 0.625000, 0, 0, 0},
                              { 0.750000, 0, 0, 0},
                              { 0.220000, 0, 0, 0},
                              { 0.875000, 0, 0, 0},
                              { 0.375000, 0, 0, 0},
                              { 0.187500, 0, 0, 0},
                              { 0.687500, 0, 0, 0},
                              { 0.062500, 0, 0, 0},
                              { 0.562500, 0, 0, 0},
                              { 0.937500, 0, 0, 0},
                              { 0.437500, 0, 0, 0},
                              { 0.812500, 0, 0, 0},
                              { 0.312500, 0, 0, 0} };
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8,r9;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.x = t2.SampleLevel(s2_s, v1.xy, 0).x;
  r0.y = cmp(cb0[0].z < r0.x);
  if (r0.y != 0) {
    r0.yz = cb0[0].yx * v1.yx;
    r1.xyzw = float4(4,4,4,4) * r0.yyzz;
    r1.xyzw = cmp(r1.xyzw >= -r1.yyww);
    r1.xyzw = r1.xyzw ? float4(4,0.25,4,0.25) : float4(-4,-0.25,-4,-0.25);
    r0.yz = r1.yw * r0.yz;
    r0.yz = frac(r0.yz);
    r0.yz = r1.xz * r0.yz;
    r0.yz = (uint2)r0.yz;
    r0.z = (uint)r0.z << 2;
    r0.y = (int)r0.y + (int)r0.z;
    r1.xy = t1.SampleLevel(s1_s, v1.xy, 0).xy;
    r0.z = rcp(cb0[0].z);
    r0.w = icb[r0.y+0].x + -0.5;
    r1.z = r0.w * 0.00400000019 + r0.z;
    r2.z = rcp(r0.x);
    r2.xy = v1.xy;
    r0.xzw = r2.xyz + -r1.xyz;
    r2.xy = icb[r0.y+0].xx * r0.xz;
    r2.xy = r2.xy * float2(0.00200000009,0.00200000009) + v1.xy;
    r2.zw = float2(0.125,0.125) * cb0[0].xy;
    r2.zw = floor(r2.zw);
    r3.xy = r2.xy * r2.zw;
    r3.xy = floor(r3.xy);
    r3.zw = cmp(r0.xz >= float2(0,0));
    r3.zw = r3.zw ? float2(1,1) : 0;
    r4.xy = r3.zw * float2(2,2) + float2(-1,-1);
    r4.zw = r3.xy + r3.zw;
    r4.zw = r4.zw / r2.zw;
    r4.zw = r4.zw + -r1.xy;
    r4.zw = r4.zw / r0.xz;
    r0.y = cmp(r4.w >= r4.z);
    r5.x = r0.y ? 1.000000 : 0;
    r5.y = r0.y ? 0 : 1;
    r5.xy = r4.xy * r5.xy + r3.xy;
    r0.y = min(r4.z, r4.w);
    r6.xyz = r0.yyy * r0.xzw + r1.xyz;
    r0.y = rcp(r6.z);
    r3.xy = cmp(float2(0,0) >= r6.xy);
    r4.zw = cmp(r6.xy >= float2(1,1));
    r1.w = (int)r3.x | (int)r4.z;
    r1.w = (int)r3.y | (int)r1.w;
    r1.w = (int)r4.w | (int)r1.w;
    r3.x = cmp(r0.y >= cb0[0].w);
    r1.w = (int)r1.w | (int)r3.x;
    r3.x = ~(int)r1.w;
    if (r1.w == 0) {
      r7.xy = (int2)r5.xy;
      r7.zw = float2(5.60519386e-45,5.60519386e-45);
      r1.w = t0.Load(r7.xyz).x;
      r3.y = cmp(r1.w < r0.y);
      if (r3.y != 0) {
        r4.zw = float2(0.5,0.5) + r5.xy;
        r7.xy = rcp(r2.zw);
        r4.zw = -r4.zw * r7.xy + r6.xy;
        r4.zw = cmp(r4.zw >= float2(0,0));
        r4.zw = r4.zw ? float2(1,1) : 0;
        r5.xy = r5.xy * float2(2,2) + r4.zw;
        r2.zw = r2.zw + r2.zw;
        r5.z = 3;
      } else {
        r4.zw = r5.xy + r3.zw;
        r4.zw = r4.zw / r2.zw;
        r4.zw = r4.zw + -r1.xy;
        r4.zw = r4.zw / r0.xz;
        r3.y = cmp(r4.w >= r4.z);
        r7.x = r3.y ? 1.000000 : 0;
        r7.y = r3.y ? 0 : 1;
        r5.xy = r4.xy * r7.xy + r5.xy;
        r3.y = min(r4.z, r4.w);
        r6.xyz = r3.yyy * r0.xzw + r1.xyz;
        r3.y = rcp(r6.z);
        r3.y = cmp(r1.w < r3.y);
        if (r3.y != 0) {
          r3.y = rcp(r1.w);
          r3.y = r3.y + -r1.z;
          r4.z = rcp(r0.w);
          r3.y = r4.z * r3.y;
          r6.xyz = r3.yyy * r0.xzw + r1.xyz;
          r2.zw = r2.zw + r2.zw;
          r4.zw = r6.xy * r2.zw;
          r5.xy = floor(r4.zw);
          r5.z = 3;
        } else {
          r5.z = 4;
        }
      }
      r3.y = 1;
    } else {
      r5.z = 4;
      r3.y = 0;
      r1.w = 0;
    }
    r4.zw = cmp(float2(0,0) >= r6.xy);
    r7.xy = cmp(r6.xy >= float2(1,1));
    r4.z = (int)r4.z | (int)r7.x;
    r4.z = (int)r4.w | (int)r4.z;
    r4.z = (int)r7.y | (int)r4.z;
    r4.w = rcp(r6.z);
    r5.w = cmp(r4.w >= cb0[0].w);
    r4.z = (int)r4.z | (int)r5.w;
    r4.z = ~(int)r4.z;
    r3.x = r3.x ? r4.z : 0;
    if (r3.x != 0) {
      r7.xyzw = (int4)r5.xyzz;
      r1.w = t0.Load(r7.xyz).x;
      r4.z = cmp(r1.w < r4.w);
      if (r4.z != 0) {
        r5.z = -1 + r5.z;
        r4.zw = float2(0.5,0.5) + r5.xy;
        r7.xy = rcp(r2.zw);
        r4.zw = -r4.zw * r7.xy + r6.xy;
        r4.zw = cmp(r4.zw >= float2(0,0));
        r4.zw = r4.zw ? float2(1,1) : 0;
        r5.xy = r5.xy * float2(2,2) + r4.zw;
        r2.zw = r2.zw + r2.zw;
      } else {
        r4.zw = r5.xy + r3.zw;
        r4.zw = r4.zw / r2.zw;
        r4.zw = r4.zw + -r1.xy;
        r4.zw = r4.zw / r0.xz;
        r5.w = cmp(r4.w >= r4.z);
        r7.y = r5.w ? 1.000000 : 0;
        r7.z = r5.w ? 0 : 1;
        r7.xw = r7.yz * r4.xy;
        r8.yz = r4.xy * r7.yz + r5.xy;
        r4.z = min(r4.z, r4.w);
        r6.xyz = r4.zzz * r0.xzw + r1.xyz;
        r4.z = rcp(r6.z);
        r4.z = cmp(r1.w < r4.z);
        if (r4.z != 0) {
          r4.z = rcp(r1.w);
          r4.z = r4.z + -r1.z;
          r4.w = rcp(r0.w);
          r4.z = r4.z * r4.w;
          r6.xyz = r4.zzz * r0.xzw + r1.xyz;
          r5.z = -1 + r5.z;
          r2.zw = r2.zw + r2.zw;
          r4.zw = r6.xy * r2.zw;
          r5.xy = floor(r4.zw);
        } else {
          r4.z = cmp(r5.z != 4.000000);
          r7.xy = cmp(r7.xw >= float2(0,0));
          r7.zw = float2(0.5,0.5) * r2.zw;
          r7.zw = floor(r7.zw);
          r9.x = 1 + r5.z;
          r7.xy = r7.xy ? float2(0,0) : float2(-0.5,-0.5);
          r9.yz = r8.yz * float2(0.5,0.5) + r7.xy;
          r2.zw = r4.zz ? r7.zw : r2.zw;
          r8.x = 4;
          r5.xyz = r4.zzz ? r9.yzx : r8.yzx;
        }
      }
      r3.y = 2;
    }
    r4.zw = cmp(float2(0,0) >= r6.xy);
    r7.xy = cmp(r6.xy >= float2(1,1));
    r4.z = (int)r4.z | (int)r7.x;
    r4.z = (int)r4.w | (int)r4.z;
    r4.z = (int)r7.y | (int)r4.z;
    r4.w = rcp(r6.z);
    r5.w = cmp(r4.w >= cb0[0].w);
    r4.z = (int)r4.z | (int)r5.w;
    r4.z = ~(int)r4.z;
    r3.x = r3.x ? r4.z : 0;
    if (r3.x != 0) {
      r7.xyzw = (int4)r5.xyzz;
      r1.w = t0.Load(r7.xyz).x;
      r4.z = cmp(r1.w < r4.w);
      if (r4.z != 0) {
        r5.z = -1 + r5.z;
        r4.zw = float2(0.5,0.5) + r5.xy;
        r7.xy = rcp(r2.zw);
        r4.zw = -r4.zw * r7.xy + r6.xy;
        r4.zw = cmp(r4.zw >= float2(0,0));
        r4.zw = r4.zw ? float2(1,1) : 0;
        r5.xy = r5.xy * float2(2,2) + r4.zw;
        r2.zw = r2.zw + r2.zw;
      } else {
        r4.zw = r5.xy + r3.zw;
        r4.zw = r4.zw / r2.zw;
        r4.zw = r4.zw + -r1.xy;
        r4.zw = r4.zw / r0.xz;
        r5.w = cmp(r4.w >= r4.z);
        r7.y = r5.w ? 1.000000 : 0;
        r7.z = r5.w ? 0 : 1;
        r7.xw = r7.yz * r4.xy;
        r8.yz = r4.xy * r7.yz + r5.xy;
        r4.z = min(r4.z, r4.w);
        r6.xyz = r4.zzz * r0.xzw + r1.xyz;
        r4.z = rcp(r6.z);
        r4.z = cmp(r1.w < r4.z);
        if (r4.z != 0) {
          r4.z = rcp(r1.w);
          r4.z = r4.z + -r1.z;
          r4.w = rcp(r0.w);
          r4.z = r4.z * r4.w;
          r6.xyz = r4.zzz * r0.xzw + r1.xyz;
          r5.z = -1 + r5.z;
          r2.zw = r2.zw + r2.zw;
          r4.zw = r6.xy * r2.zw;
          r5.xy = floor(r4.zw);
        } else {
          r4.z = cmp(r5.z != 4.000000);
          r7.xy = cmp(r7.xw >= float2(0,0));
          r7.zw = float2(0.5,0.5) * r2.zw;
          r7.zw = floor(r7.zw);
          r9.x = 1 + r5.z;
          r7.xy = r7.xy ? float2(0,0) : float2(-0.5,-0.5);
          r9.yz = r8.yz * float2(0.5,0.5) + r7.xy;
          r2.zw = r4.zz ? r7.zw : r2.zw;
          r8.x = 4;
          r5.xyz = r4.zzz ? r9.yzx : r8.yzx;
        }
      }
      r3.y = 3;
    }
    r4.zw = cmp(float2(0,0) >= r6.xy);
    r7.xy = cmp(r6.xy >= float2(1,1));
    r4.z = (int)r4.z | (int)r7.x;
    r4.z = (int)r4.w | (int)r4.z;
    r4.z = (int)r7.y | (int)r4.z;
    r4.w = rcp(r6.z);
    r5.w = cmp(r4.w >= cb0[0].w);
    r4.z = (int)r4.z | (int)r5.w;
    r4.z = ~(int)r4.z;
    r3.x = r3.x ? r4.z : 0;
    if (r3.x != 0) {
      r7.xyzw = (int4)r5.xyzz;
      r1.w = t0.Load(r7.xyz).x;
      r4.z = r4.w + -r1.w;
      r5.w = cmp(r5.z == 1.000000);
      r4.z = cmp(r4.z >= 50);
      r6.w = r4.z ? r5.w : 0;
      r6.w = ~(int)r6.w;
      r4.w = cmp(r1.w < r4.w);
      r4.w = r4.w ? r6.w : 0;
      if (r4.w != 0) {
        r5.z = -1 + r5.z;
        r7.xy = float2(0.5,0.5) + r5.xy;
        r7.zw = rcp(r2.zw);
        r7.xy = -r7.xy * r7.zw + r6.xy;
        r7.xy = cmp(r7.xy >= float2(0,0));
        r7.xy = r7.xy ? float2(1,1) : 0;
        r5.xy = r5.xy * float2(2,2) + r7.xy;
        r2.zw = r2.zw + r2.zw;
        r4.w = 0;
      } else {
        r7.xy = r5.xy + r3.zw;
        r7.xy = r7.xy / r2.zw;
        r7.xy = r7.xy + -r1.xy;
        r7.xy = r7.xy / r0.xz;
        r6.w = cmp(r7.y >= r7.x);
        r8.y = r6.w ? 1.000000 : 0;
        r8.z = r6.w ? 0 : 1;
        r7.zw = r8.yz * r4.xy;
        r5.xy = r4.xy * r8.yz + r5.xy;
        r6.w = min(r7.x, r7.y);
        r6.xyz = r6.www * r0.xzw + r1.xyz;
        r6.w = rcp(r6.z);
        r7.x = r6.w + -r1.w;
        r7.x = cmp(r7.x >= 50);
        r4.z = (int)r4.z | (int)r7.x;
        r4.w = r5.w ? r4.z : 0;
        if (r4.w == 0) {
          r4.z = cmp(r1.w < r6.w);
          if (r4.z != 0) {
            r4.z = rcp(r1.w);
            r4.z = r4.z + -r1.z;
            r5.w = rcp(r0.w);
            r4.z = r5.w * r4.z;
            r6.xyz = r4.zzz * r0.xzw + r1.xyz;
            r5.z = -1 + r5.z;
            r2.zw = r2.zw + r2.zw;
            r7.xy = r6.xy * r2.zw;
            r5.xy = floor(r7.xy);
          } else {
            r4.z = cmp(r5.z != 4.000000);
            r7.xy = cmp(r7.zw >= float2(0,0));
            r7.zw = float2(0.5,0.5) * r2.zw;
            r7.zw = floor(r7.zw);
            r8.x = 1 + r5.z;
            r7.xy = r7.xy ? float2(0,0) : float2(-0.5,-0.5);
            r8.yz = r5.xy * float2(0.5,0.5) + r7.xy;
            r2.zw = r4.zz ? r7.zw : r2.zw;
            r5.z = 4;
            r5.xyz = r4.zzz ? r8.yzx : r5.xyz;
          }
        } else {
          r5.z = 1;
        }
      }
      r3.y = 4;
    } else {
      r4.w = 0;
    }
    r4.z = cmp(r5.z >= 1);
    r7.xy = cmp(float2(0,0) >= r6.xy);
    r7.zw = cmp(r6.xy >= float2(1,1));
    r5.w = (int)r7.z | (int)r7.x;
    r5.w = (int)r7.y | (int)r5.w;
    r5.w = (int)r7.w | (int)r5.w;
    r6.w = rcp(r6.z);
    r7.x = cmp(r6.w >= cb0[0].w);
    r5.w = (int)r5.w | (int)r7.x;
    r5.w = ~(int)r5.w;
    r4.z = r4.z ? r5.w : 0;
    r3.x = r3.x ? r4.z : 0;
    if (r3.x != 0) {
      r7.xyzw = (int4)r5.xyzz;
      r1.w = t0.Load(r7.xyz).x;
      r4.z = r6.w + -r1.w;
      r5.w = cmp(r5.z == 1.000000);
      r4.z = cmp(r4.z >= 50);
      r4.z = r4.z ? r5.w : 0;
      r4.z = (int)r4.z | (int)r4.w;
      r7.x = ~(int)r4.z;
      r6.w = cmp(r1.w < r6.w);
      r6.w = r6.w ? r7.x : 0;
      if (r6.w != 0) {
        r5.z = -1 + r5.z;
        r7.xy = float2(0.5,0.5) + r5.xy;
        r7.zw = rcp(r2.zw);
        r7.xy = -r7.xy * r7.zw + r6.xy;
        r7.xy = cmp(r7.xy >= float2(0,0));
        r7.xy = r7.xy ? float2(1,1) : 0;
        r5.xy = r5.xy * float2(2,2) + r7.xy;
        r2.zw = r2.zw + r2.zw;
        r4.w = 0;
      } else {
        r7.xy = r5.xy + r3.zw;
        r7.xy = r7.xy / r2.zw;
        r7.xy = r7.xy + -r1.xy;
        r7.xy = r7.xy / r0.xz;
        r6.w = cmp(r7.y >= r7.x);
        r8.y = r6.w ? 1.000000 : 0;
        r8.z = r6.w ? 0 : 1;
        r7.zw = r8.yz * r4.xy;
        r5.xy = r4.xy * r8.yz + r5.xy;
        r6.w = min(r7.x, r7.y);
        r6.xyz = r6.www * r0.xzw + r1.xyz;
        r6.w = rcp(r6.z);
        r7.x = r6.w + -r1.w;
        r7.x = cmp(r7.x >= 50);
        r5.w = r5.w ? r7.x : 0;
        r4.w = (int)r4.z | (int)r5.w;
        if (r4.w == 0) {
          r4.z = cmp(r1.w < r6.w);
          if (r4.z != 0) {
            r4.z = rcp(r1.w);
            r4.z = r4.z + -r1.z;
            r5.w = rcp(r0.w);
            r4.z = r5.w * r4.z;
            r6.xyz = r4.zzz * r0.xzw + r1.xyz;
            r5.z = -1 + r5.z;
            r2.zw = r2.zw + r2.zw;
            r7.xy = r6.xy * r2.zw;
            r5.xy = floor(r7.xy);
          } else {
            r4.z = cmp(r5.z != 4.000000);
            r7.xy = cmp(r7.zw >= float2(0,0));
            r7.zw = float2(0.5,0.5) * r2.zw;
            r7.zw = floor(r7.zw);
            r8.x = 1 + r5.z;
            r7.xy = r7.xy ? float2(0,0) : float2(-0.5,-0.5);
            r8.yz = r5.xy * float2(0.5,0.5) + r7.xy;
            r2.zw = r4.zz ? r7.zw : r2.zw;
            r5.z = 4;
            r5.xyz = r4.zzz ? r8.yzx : r5.xyz;
          }
        }
      }
      r3.y = 5;
    }
    r4.z = cmp(r5.z >= 1);
    r7.xy = cmp(float2(0,0) >= r6.xy);
    r7.zw = cmp(r6.xy >= float2(1,1));
    r5.w = (int)r7.z | (int)r7.x;
    r5.w = (int)r7.y | (int)r5.w;
    r5.w = (int)r7.w | (int)r5.w;
    r6.w = rcp(r6.z);
    r7.x = cmp(r6.w >= cb0[0].w);
    r5.w = (int)r5.w | (int)r7.x;
    r5.w = ~(int)r5.w;
    r4.z = r4.z ? r5.w : 0;
    r3.x = r3.x ? r4.z : 0;
    if (r3.x != 0) {
      r7.xyzw = (int4)r5.xyzz;
      r1.w = t0.Load(r7.xyz).x;
      r4.z = r6.w + -r1.w;
      r5.w = cmp(r5.z == 1.000000);
      r4.z = cmp(r4.z >= 50);
      r4.z = r4.z ? r5.w : 0;
      r4.z = (int)r4.z | (int)r4.w;
      r7.x = ~(int)r4.z;
      r6.w = cmp(r1.w < r6.w);
      r6.w = r6.w ? r7.x : 0;
      if (r6.w != 0) {
        r5.z = -1 + r5.z;
        r7.xy = float2(0.5,0.5) + r5.xy;
        r7.zw = rcp(r2.zw);
        r7.xy = -r7.xy * r7.zw + r6.xy;
        r7.xy = cmp(r7.xy >= float2(0,0));
        r7.xy = r7.xy ? float2(1,1) : 0;
        r5.xy = r5.xy * float2(2,2) + r7.xy;
        r2.zw = r2.zw + r2.zw;
        r4.w = 0;
      } else {
        r7.xy = r5.xy + r3.zw;
        r7.xy = r7.xy / r2.zw;
        r7.xy = r7.xy + -r1.xy;
        r7.xy = r7.xy / r0.xz;
        r6.w = cmp(r7.y >= r7.x);
        r8.y = r6.w ? 1.000000 : 0;
        r8.z = r6.w ? 0 : 1;
        r7.zw = r8.yz * r4.xy;
        r5.xy = r4.xy * r8.yz + r5.xy;
        r6.w = min(r7.x, r7.y);
        r6.xyz = r6.www * r0.xzw + r1.xyz;
        r6.w = rcp(r6.z);
        r7.x = r6.w + -r1.w;
        r7.x = cmp(r7.x >= 50);
        r5.w = r5.w ? r7.x : 0;
        r4.w = (int)r4.z | (int)r5.w;
        if (r4.w == 0) {
          r4.z = cmp(r1.w < r6.w);
          if (r4.z != 0) {
            r4.z = rcp(r1.w);
            r4.z = r4.z + -r1.z;
            r5.w = rcp(r0.w);
            r4.z = r5.w * r4.z;
            r6.xyz = r4.zzz * r0.xzw + r1.xyz;
            r5.z = -1 + r5.z;
            r2.zw = r2.zw + r2.zw;
            r7.xy = r6.xy * r2.zw;
            r5.xy = floor(r7.xy);
          } else {
            r4.z = cmp(r5.z != 4.000000);
            r7.xy = cmp(r7.zw >= float2(0,0));
            r7.zw = float2(0.5,0.5) * r2.zw;
            r7.zw = floor(r7.zw);
            r8.x = 1 + r5.z;
            r7.xy = r7.xy ? float2(0,0) : float2(-0.5,-0.5);
            r8.yz = r5.xy * float2(0.5,0.5) + r7.xy;
            r2.zw = r4.zz ? r7.zw : r2.zw;
            r5.z = 4;
            r5.xyz = r4.zzz ? r8.yzx : r5.xyz;
          }
        }
      }
      r3.y = 6;
    }
    r4.z = cmp(r5.z >= 1);
    r7.xy = cmp(float2(0,0) >= r6.xy);
    r7.zw = cmp(r6.xy >= float2(1,1));
    r5.w = (int)r7.z | (int)r7.x;
    r5.w = (int)r7.y | (int)r5.w;
    r5.w = (int)r7.w | (int)r5.w;
    r6.w = rcp(r6.z);
    r7.x = cmp(r6.w >= cb0[0].w);
    r5.w = (int)r5.w | (int)r7.x;
    r5.w = ~(int)r5.w;
    r4.z = r4.z ? r5.w : 0;
    r3.x = r3.x ? r4.z : 0;
    if (r3.x != 0) {
      r7.xyzw = (int4)r5.xyzz;
      r1.w = t0.Load(r7.xyz).x;
      r4.z = r6.w + -r1.w;
      r5.w = cmp(r5.z == 1.000000);
      r4.z = cmp(r4.z >= 50);
      r4.z = r4.z ? r5.w : 0;
      r4.z = (int)r4.z | (int)r4.w;
      r7.x = ~(int)r4.z;
      r6.w = cmp(r1.w < r6.w);
      r6.w = r6.w ? r7.x : 0;
      if (r6.w != 0) {
        r5.z = -1 + r5.z;
        r7.xy = float2(0.5,0.5) + r5.xy;
        r7.zw = rcp(r2.zw);
        r7.xy = -r7.xy * r7.zw + r6.xy;
        r7.xy = cmp(r7.xy >= float2(0,0));
        r7.xy = r7.xy ? float2(1,1) : 0;
        r5.xy = r5.xy * float2(2,2) + r7.xy;
        r2.zw = r2.zw + r2.zw;
        r4.w = 0;
      } else {
        r7.xy = r5.xy + r3.zw;
        r7.xy = r7.xy / r2.zw;
        r7.xy = r7.xy + -r1.xy;
        r7.xy = r7.xy / r0.xz;
        r6.w = cmp(r7.y >= r7.x);
        r8.y = r6.w ? 1.000000 : 0;
        r8.z = r6.w ? 0 : 1;
        r7.zw = r8.yz * r4.xy;
        r5.xy = r4.xy * r8.yz + r5.xy;
        r6.w = min(r7.x, r7.y);
        r6.xyz = r6.www * r0.xzw + r1.xyz;
        r6.w = rcp(r6.z);
        r7.x = r6.w + -r1.w;
        r7.x = cmp(r7.x >= 50);
        r5.w = r5.w ? r7.x : 0;
        r4.w = (int)r4.z | (int)r5.w;
        if (r4.w == 0) {
          r4.z = cmp(r1.w < r6.w);
          if (r4.z != 0) {
            r4.z = rcp(r1.w);
            r4.z = r4.z + -r1.z;
            r5.w = rcp(r0.w);
            r4.z = r5.w * r4.z;
            r6.xyz = r4.zzz * r0.xzw + r1.xyz;
            r5.z = -1 + r5.z;
            r2.zw = r2.zw + r2.zw;
            r7.xy = r6.xy * r2.zw;
            r5.xy = floor(r7.xy);
          } else {
            r4.z = cmp(r5.z != 4.000000);
            r7.xy = cmp(r7.zw >= float2(0,0));
            r7.zw = float2(0.5,0.5) * r2.zw;
            r7.zw = floor(r7.zw);
            r8.x = 1 + r5.z;
            r7.xy = r7.xy ? float2(0,0) : float2(-0.5,-0.5);
            r8.yz = r5.xy * float2(0.5,0.5) + r7.xy;
            r2.zw = r4.zz ? r7.zw : r2.zw;
            r5.z = 4;
            r5.xyz = r4.zzz ? r8.yzx : r5.xyz;
          }
        }
      }
      r3.y = 7;
    }
    r4.z = cmp(r5.z >= 1);
    r7.xy = cmp(float2(0,0) >= r6.xy);
    r7.zw = cmp(r6.xy >= float2(1,1));
    r5.w = (int)r7.z | (int)r7.x;
    r5.w = (int)r7.y | (int)r5.w;
    r5.w = (int)r7.w | (int)r5.w;
    r6.w = rcp(r6.z);
    r7.x = cmp(r6.w >= cb0[0].w);
    r5.w = (int)r5.w | (int)r7.x;
    r5.w = ~(int)r5.w;
    r4.z = r4.z ? r5.w : 0;
    r3.x = r3.x ? r4.z : 0;
    if (r3.x != 0) {
      r7.xyzw = (int4)r5.xyzz;
      r1.w = t0.Load(r7.xyz).x;
      r4.z = r6.w + -r1.w;
      r5.w = cmp(r5.z == 1.000000);
      r4.z = cmp(r4.z >= 50);
      r4.z = r4.z ? r5.w : 0;
      r4.z = (int)r4.z | (int)r4.w;
      r7.x = ~(int)r4.z;
      r6.w = cmp(r1.w < r6.w);
      r6.w = r6.w ? r7.x : 0;
      if (r6.w != 0) {
        r5.z = -1 + r5.z;
        r7.xy = float2(0.5,0.5) + r5.xy;
        r7.zw = rcp(r2.zw);
        r7.xy = -r7.xy * r7.zw + r6.xy;
        r7.xy = cmp(r7.xy >= float2(0,0));
        r7.xy = r7.xy ? float2(1,1) : 0;
        r5.xy = r5.xy * float2(2,2) + r7.xy;
        r2.zw = r2.zw + r2.zw;
        r4.w = 0;
      } else {
        r7.xy = r5.xy + r3.zw;
        r7.xy = r7.xy / r2.zw;
        r7.xy = r7.xy + -r1.xy;
        r7.xy = r7.xy / r0.xz;
        r6.w = cmp(r7.y >= r7.x);
        r8.y = r6.w ? 1.000000 : 0;
        r8.z = r6.w ? 0 : 1;
        r7.zw = r8.yz * r4.xy;
        r5.xy = r4.xy * r8.yz + r5.xy;
        r6.w = min(r7.x, r7.y);
        r6.xyz = r6.www * r0.xzw + r1.xyz;
        r6.w = rcp(r6.z);
        r7.x = r6.w + -r1.w;
        r7.x = cmp(r7.x >= 50);
        r5.w = r5.w ? r7.x : 0;
        r4.w = (int)r4.z | (int)r5.w;
        if (r4.w == 0) {
          r4.z = cmp(r1.w < r6.w);
          if (r4.z != 0) {
            r4.z = rcp(r1.w);
            r4.z = r4.z + -r1.z;
            r5.w = rcp(r0.w);
            r4.z = r5.w * r4.z;
            r6.xyz = r4.zzz * r0.xzw + r1.xyz;
            r5.z = -1 + r5.z;
            r2.zw = r2.zw + r2.zw;
            r7.xy = r6.xy * r2.zw;
            r5.xy = floor(r7.xy);
          } else {
            r4.z = cmp(r5.z != 4.000000);
            r7.xy = cmp(r7.zw >= float2(0,0));
            r7.zw = float2(0.5,0.5) * r2.zw;
            r7.zw = floor(r7.zw);
            r8.x = 1 + r5.z;
            r7.xy = r7.xy ? float2(0,0) : float2(-0.5,-0.5);
            r8.yz = r5.xy * float2(0.5,0.5) + r7.xy;
            r2.zw = r4.zz ? r7.zw : r2.zw;
            r5.z = 4;
            r5.xyz = r4.zzz ? r8.yzx : r5.xyz;
          }
        }
      }
      r3.y = 8;
    }
    r4.z = cmp(r5.z >= 1);
    r7.xy = cmp(float2(0,0) >= r6.xy);
    r7.zw = cmp(r6.xy >= float2(1,1));
    r5.w = (int)r7.z | (int)r7.x;
    r5.w = (int)r7.y | (int)r5.w;
    r5.w = (int)r7.w | (int)r5.w;
    r6.w = rcp(r6.z);
    r7.x = cmp(r6.w >= cb0[0].w);
    r5.w = (int)r5.w | (int)r7.x;
    r5.w = ~(int)r5.w;
    r4.z = r4.z ? r5.w : 0;
    r3.x = r3.x ? r4.z : 0;
    if (r3.x != 0) {
      r7.xyzw = (int4)r5.xyzz;
      r1.w = t0.Load(r7.xyz).x;
      r4.z = r6.w + -r1.w;
      r5.w = cmp(r5.z == 1.000000);
      r4.z = cmp(r4.z >= 50);
      r4.z = r4.z ? r5.w : 0;
      r4.z = (int)r4.z | (int)r4.w;
      r7.x = ~(int)r4.z;
      r6.w = cmp(r1.w < r6.w);
      r6.w = r6.w ? r7.x : 0;
      if (r6.w != 0) {
        r5.z = -1 + r5.z;
        r7.xy = float2(0.5,0.5) + r5.xy;
        r7.zw = rcp(r2.zw);
        r7.xy = -r7.xy * r7.zw + r6.xy;
        r7.xy = cmp(r7.xy >= float2(0,0));
        r7.xy = r7.xy ? float2(1,1) : 0;
        r5.xy = r5.xy * float2(2,2) + r7.xy;
        r2.zw = r2.zw + r2.zw;
        r4.w = 0;
      } else {
        r7.xy = r5.xy + r3.zw;
        r7.xy = r7.xy / r2.zw;
        r7.xy = r7.xy + -r1.xy;
        r7.xy = r7.xy / r0.xz;
        r6.w = cmp(r7.y >= r7.x);
        r8.y = r6.w ? 1.000000 : 0;
        r8.z = r6.w ? 0 : 1;
        r7.zw = r8.yz * r4.xy;
        r5.xy = r4.xy * r8.yz + r5.xy;
        r6.w = min(r7.x, r7.y);
        r6.xyz = r6.www * r0.xzw + r1.xyz;
        r6.w = rcp(r6.z);
        r7.x = r6.w + -r1.w;
        r7.x = cmp(r7.x >= 50);
        r5.w = r5.w ? r7.x : 0;
        r4.w = (int)r4.z | (int)r5.w;
        if (r4.w == 0) {
          r4.z = cmp(r1.w < r6.w);
          if (r4.z != 0) {
            r4.z = rcp(r1.w);
            r4.z = r4.z + -r1.z;
            r5.w = rcp(r0.w);
            r4.z = r5.w * r4.z;
            r6.xyz = r4.zzz * r0.xzw + r1.xyz;
            r5.z = -1 + r5.z;
            r2.zw = r2.zw + r2.zw;
            r7.xy = r6.xy * r2.zw;
            r5.xy = floor(r7.xy);
          } else {
            r4.z = cmp(r5.z != 4.000000);
            r7.xy = cmp(r7.zw >= float2(0,0));
            r7.zw = float2(0.5,0.5) * r2.zw;
            r7.zw = floor(r7.zw);
            r8.x = 1 + r5.z;
            r7.xy = r7.xy ? float2(0,0) : float2(-0.5,-0.5);
            r8.yz = r5.xy * float2(0.5,0.5) + r7.xy;
            r2.zw = r4.zz ? r7.zw : r2.zw;
            r5.z = 4;
            r5.xyz = r4.zzz ? r8.yzx : r5.xyz;
          }
        }
      }
      r3.y = 9;
    }
    r4.z = cmp(r5.z >= 1);
    r7.xy = cmp(float2(0,0) >= r6.xy);
    r7.zw = cmp(r6.xy >= float2(1,1));
    r5.w = (int)r7.z | (int)r7.x;
    r5.w = (int)r7.y | (int)r5.w;
    r5.w = (int)r7.w | (int)r5.w;
    r6.w = rcp(r6.z);
    r7.x = cmp(r6.w >= cb0[0].w);
    r5.w = (int)r5.w | (int)r7.x;
    r5.w = ~(int)r5.w;
    r4.z = r4.z ? r5.w : 0;
    r3.x = r3.x ? r4.z : 0;
    if (r3.x != 0) {
      r7.xyzw = (int4)r5.xyzz;
      r1.w = t0.Load(r7.xyz).x;
      r4.z = r6.w + -r1.w;
      r5.w = cmp(r5.z == 1.000000);
      r4.z = cmp(r4.z >= 50);
      r4.z = r4.z ? r5.w : 0;
      r4.z = (int)r4.z | (int)r4.w;
      r7.x = ~(int)r4.z;
      r6.w = cmp(r1.w < r6.w);
      r6.w = r6.w ? r7.x : 0;
      if (r6.w != 0) {
        r5.z = -1 + r5.z;
        r7.xy = float2(0.5,0.5) + r5.xy;
        r7.zw = rcp(r2.zw);
        r7.xy = -r7.xy * r7.zw + r6.xy;
        r7.xy = cmp(r7.xy >= float2(0,0));
        r7.xy = r7.xy ? float2(1,1) : 0;
        r5.xy = r5.xy * float2(2,2) + r7.xy;
        r2.zw = r2.zw + r2.zw;
        r4.w = 0;
      } else {
        r7.xy = r5.xy + r3.zw;
        r7.xy = r7.xy / r2.zw;
        r7.xy = r7.xy + -r1.xy;
        r7.xy = r7.xy / r0.xz;
        r6.w = cmp(r7.y >= r7.x);
        r8.y = r6.w ? 1.000000 : 0;
        r8.z = r6.w ? 0 : 1;
        r7.zw = r8.yz * r4.xy;
        r5.xy = r4.xy * r8.yz + r5.xy;
        r6.w = min(r7.x, r7.y);
        r6.xyz = r6.www * r0.xzw + r1.xyz;
        r6.w = rcp(r6.z);
        r7.x = r6.w + -r1.w;
        r7.x = cmp(r7.x >= 50);
        r5.w = r5.w ? r7.x : 0;
        r4.w = (int)r4.z | (int)r5.w;
        if (r4.w == 0) {
          r4.z = cmp(r1.w < r6.w);
          if (r4.z != 0) {
            r4.z = rcp(r1.w);
            r4.z = r4.z + -r1.z;
            r5.w = rcp(r0.w);
            r4.z = r5.w * r4.z;
            r6.xyz = r4.zzz * r0.xzw + r1.xyz;
            r5.z = -1 + r5.z;
            r2.zw = r2.zw + r2.zw;
            r7.xy = r6.xy * r2.zw;
            r5.xy = floor(r7.xy);
          } else {
            r4.z = cmp(r5.z != 4.000000);
            r7.xy = cmp(r7.zw >= float2(0,0));
            r7.zw = float2(0.5,0.5) * r2.zw;
            r7.zw = floor(r7.zw);
            r8.x = 1 + r5.z;
            r7.xy = r7.xy ? float2(0,0) : float2(-0.5,-0.5);
            r8.yz = r5.xy * float2(0.5,0.5) + r7.xy;
            r2.zw = r4.zz ? r7.zw : r2.zw;
            r5.z = 4;
            r5.xyz = r4.zzz ? r8.yzx : r5.xyz;
          }
        }
      }
      r3.y = 10;
    }
    r4.z = cmp(r5.z >= 1);
    r7.xy = cmp(float2(0,0) >= r6.xy);
    r7.zw = cmp(r6.xy >= float2(1,1));
    r5.w = (int)r7.z | (int)r7.x;
    r5.w = (int)r7.y | (int)r5.w;
    r5.w = (int)r7.w | (int)r5.w;
    r6.w = rcp(r6.z);
    r7.x = cmp(r6.w >= cb0[0].w);
    r5.w = (int)r5.w | (int)r7.x;
    r5.w = ~(int)r5.w;
    r4.z = r4.z ? r5.w : 0;
    r3.x = r3.x ? r4.z : 0;
    if (r3.x != 0) {
      r7.xyzw = (int4)r5.xyzz;
      r1.w = t0.Load(r7.xyz).x;
      r4.z = r6.w + -r1.w;
      r5.w = cmp(r5.z == 1.000000);
      r4.z = cmp(r4.z >= 50);
      r4.z = r4.z ? r5.w : 0;
      r4.z = (int)r4.z | (int)r4.w;
      r7.x = ~(int)r4.z;
      r6.w = cmp(r1.w < r6.w);
      r6.w = r6.w ? r7.x : 0;
      if (r6.w != 0) {
        r5.z = -1 + r5.z;
        r7.xy = float2(0.5,0.5) + r5.xy;
        r7.zw = rcp(r2.zw);
        r7.xy = -r7.xy * r7.zw + r6.xy;
        r7.xy = cmp(r7.xy >= float2(0,0));
        r7.xy = r7.xy ? float2(1,1) : 0;
        r5.xy = r5.xy * float2(2,2) + r7.xy;
        r2.zw = r2.zw + r2.zw;
        r4.w = 0;
      } else {
        r7.xy = r5.xy + r3.zw;
        r7.xy = r7.xy / r2.zw;
        r7.xy = r7.xy + -r1.xy;
        r7.xy = r7.xy / r0.xz;
        r6.w = cmp(r7.y >= r7.x);
        r8.y = r6.w ? 1.000000 : 0;
        r8.z = r6.w ? 0 : 1;
        r7.zw = r8.yz * r4.xy;
        r5.xy = r4.xy * r8.yz + r5.xy;
        r6.w = min(r7.x, r7.y);
        r6.xyz = r6.www * r0.xzw + r1.xyz;
        r6.w = rcp(r6.z);
        r7.x = r6.w + -r1.w;
        r7.x = cmp(r7.x >= 50);
        r5.w = r5.w ? r7.x : 0;
        r4.w = (int)r4.z | (int)r5.w;
        if (r4.w == 0) {
          r4.z = cmp(r1.w < r6.w);
          if (r4.z != 0) {
            r4.z = rcp(r1.w);
            r4.z = r4.z + -r1.z;
            r5.w = rcp(r0.w);
            r4.z = r5.w * r4.z;
            r6.xyz = r4.zzz * r0.xzw + r1.xyz;
            r5.z = -1 + r5.z;
            r2.zw = r2.zw + r2.zw;
            r7.xy = r6.xy * r2.zw;
            r5.xy = floor(r7.xy);
          } else {
            r4.z = cmp(r5.z != 4.000000);
            r7.xy = cmp(r7.zw >= float2(0,0));
            r7.zw = float2(0.5,0.5) * r2.zw;
            r7.zw = floor(r7.zw);
            r8.x = 1 + r5.z;
            r7.xy = r7.xy ? float2(0,0) : float2(-0.5,-0.5);
            r8.yz = r5.xy * float2(0.5,0.5) + r7.xy;
            r2.zw = r4.zz ? r7.zw : r2.zw;
            r5.z = 4;
            r5.xyz = r4.zzz ? r8.yzx : r5.xyz;
          }
        }
      }
      r3.y = 11;
    }
    r4.z = cmp(r5.z >= 1);
    r7.xy = cmp(float2(0,0) >= r6.xy);
    r7.zw = cmp(r6.xy >= float2(1,1));
    r5.w = (int)r7.z | (int)r7.x;
    r5.w = (int)r7.y | (int)r5.w;
    r5.w = (int)r7.w | (int)r5.w;
    r6.w = rcp(r6.z);
    r7.x = cmp(r6.w >= cb0[0].w);
    r5.w = (int)r5.w | (int)r7.x;
    r5.w = ~(int)r5.w;
    r4.z = r4.z ? r5.w : 0;
    r3.x = r3.x ? r4.z : 0;
    if (r3.x != 0) {
      r7.xyzw = (int4)r5.xyzz;
      r1.w = t0.Load(r7.xyz).x;
      r4.z = r6.w + -r1.w;
      r5.w = cmp(r5.z == 1.000000);
      r4.z = cmp(r4.z >= 50);
      r4.z = r4.z ? r5.w : 0;
      r4.z = (int)r4.z | (int)r4.w;
      r7.x = ~(int)r4.z;
      r6.w = cmp(r1.w < r6.w);
      r6.w = r6.w ? r7.x : 0;
      if (r6.w != 0) {
        r5.z = -1 + r5.z;
        r7.xy = float2(0.5,0.5) + r5.xy;
        r7.zw = rcp(r2.zw);
        r7.xy = -r7.xy * r7.zw + r6.xy;
        r7.xy = cmp(r7.xy >= float2(0,0));
        r7.xy = r7.xy ? float2(1,1) : 0;
        r5.xy = r5.xy * float2(2,2) + r7.xy;
        r2.zw = r2.zw + r2.zw;
        r4.w = 0;
      } else {
        r7.xy = r5.xy + r3.zw;
        r7.xy = r7.xy / r2.zw;
        r7.xy = r7.xy + -r1.xy;
        r7.xy = r7.xy / r0.xz;
        r6.w = cmp(r7.y >= r7.x);
        r8.y = r6.w ? 1.000000 : 0;
        r8.z = r6.w ? 0 : 1;
        r7.zw = r8.yz * r4.xy;
        r5.xy = r4.xy * r8.yz + r5.xy;
        r6.w = min(r7.x, r7.y);
        r6.xyz = r6.www * r0.xzw + r1.xyz;
        r6.w = rcp(r6.z);
        r7.x = r6.w + -r1.w;
        r7.x = cmp(r7.x >= 50);
        r5.w = r5.w ? r7.x : 0;
        r4.w = (int)r4.z | (int)r5.w;
        if (r4.w == 0) {
          r4.z = cmp(r1.w < r6.w);
          if (r4.z != 0) {
            r4.z = rcp(r1.w);
            r4.z = r4.z + -r1.z;
            r5.w = rcp(r0.w);
            r4.z = r5.w * r4.z;
            r6.xyz = r4.zzz * r0.xzw + r1.xyz;
            r5.z = -1 + r5.z;
            r2.zw = r2.zw + r2.zw;
            r7.xy = r6.xy * r2.zw;
            r5.xy = floor(r7.xy);
          } else {
            r4.z = cmp(r5.z != 4.000000);
            r7.xy = cmp(r7.zw >= float2(0,0));
            r7.zw = float2(0.5,0.5) * r2.zw;
            r7.zw = floor(r7.zw);
            r8.x = 1 + r5.z;
            r7.xy = r7.xy ? float2(0,0) : float2(-0.5,-0.5);
            r8.yz = r5.xy * float2(0.5,0.5) + r7.xy;
            r2.zw = r4.zz ? r7.zw : r2.zw;
            r5.z = 4;
            r5.xyz = r4.zzz ? r8.yzx : r5.xyz;
          }
        }
      }
      r3.y = 12;
    }
    r4.z = cmp(r5.z >= 1);
    r7.xy = cmp(float2(0,0) >= r6.xy);
    r7.zw = cmp(r6.xy >= float2(1,1));
    r5.w = (int)r7.z | (int)r7.x;
    r5.w = (int)r7.y | (int)r5.w;
    r5.w = (int)r7.w | (int)r5.w;
    r6.w = rcp(r6.z);
    r7.x = cmp(r6.w >= cb0[0].w);
    r5.w = (int)r5.w | (int)r7.x;
    r5.w = ~(int)r5.w;
    r4.z = r4.z ? r5.w : 0;
    r3.x = r3.x ? r4.z : 0;
    if (r3.x != 0) {
      r7.xyzw = (int4)r5.xyzz;
      r1.w = t0.Load(r7.xyz).x;
      r4.z = r6.w + -r1.w;
      r5.w = cmp(r5.z == 1.000000);
      r4.z = cmp(r4.z >= 50);
      r4.z = r4.z ? r5.w : 0;
      r4.z = (int)r4.z | (int)r4.w;
      r7.x = ~(int)r4.z;
      r6.w = cmp(r1.w < r6.w);
      r6.w = r6.w ? r7.x : 0;
      if (r6.w != 0) {
        r5.z = -1 + r5.z;
        r7.xy = float2(0.5,0.5) + r5.xy;
        r7.zw = rcp(r2.zw);
        r7.xy = -r7.xy * r7.zw + r6.xy;
        r7.xy = cmp(r7.xy >= float2(0,0));
        r7.xy = r7.xy ? float2(1,1) : 0;
        r5.xy = r5.xy * float2(2,2) + r7.xy;
        r2.zw = r2.zw + r2.zw;
        r4.w = 0;
      } else {
        r7.xy = r5.xy + r3.zw;
        r7.xy = r7.xy / r2.zw;
        r7.xy = r7.xy + -r1.xy;
        r7.xy = r7.xy / r0.xz;
        r6.w = cmp(r7.y >= r7.x);
        r8.y = r6.w ? 1.000000 : 0;
        r8.z = r6.w ? 0 : 1;
        r7.zw = r8.yz * r4.xy;
        r5.xy = r4.xy * r8.yz + r5.xy;
        r6.w = min(r7.x, r7.y);
        r6.xyz = r6.www * r0.xzw + r1.xyz;
        r6.w = rcp(r6.z);
        r7.x = r6.w + -r1.w;
        r7.x = cmp(r7.x >= 50);
        r5.w = r5.w ? r7.x : 0;
        r4.w = (int)r4.z | (int)r5.w;
        if (r4.w == 0) {
          r4.z = cmp(r1.w < r6.w);
          if (r4.z != 0) {
            r4.z = rcp(r1.w);
            r4.z = r4.z + -r1.z;
            r5.w = rcp(r0.w);
            r4.z = r5.w * r4.z;
            r6.xyz = r4.zzz * r0.xzw + r1.xyz;
            r5.z = -1 + r5.z;
            r2.zw = r2.zw + r2.zw;
            r7.xy = r6.xy * r2.zw;
            r5.xy = floor(r7.xy);
          } else {
            r4.z = cmp(r5.z != 4.000000);
            r7.xy = cmp(r7.zw >= float2(0,0));
            r7.zw = float2(0.5,0.5) * r2.zw;
            r7.zw = floor(r7.zw);
            r8.x = 1 + r5.z;
            r7.xy = r7.xy ? float2(0,0) : float2(-0.5,-0.5);
            r8.yz = r5.xy * float2(0.5,0.5) + r7.xy;
            r2.zw = r4.zz ? r7.zw : r2.zw;
            r5.z = 4;
            r5.xyz = r4.zzz ? r8.yzx : r5.xyz;
          }
        }
      }
      r3.y = 13;
    }
    r4.z = cmp(r5.z >= 1);
    r7.xy = cmp(float2(0,0) >= r6.xy);
    r7.zw = cmp(r6.xy >= float2(1,1));
    r5.w = (int)r7.z | (int)r7.x;
    r5.w = (int)r7.y | (int)r5.w;
    r5.w = (int)r7.w | (int)r5.w;
    r6.w = rcp(r6.z);
    r7.x = cmp(r6.w >= cb0[0].w);
    r5.w = (int)r5.w | (int)r7.x;
    r5.w = ~(int)r5.w;
    r4.z = r4.z ? r5.w : 0;
    r3.x = r3.x ? r4.z : 0;
    if (r3.x != 0) {
      r7.xyzw = (int4)r5.xyzz;
      r1.w = t0.Load(r7.xyz).x;
      r4.z = r6.w + -r1.w;
      r5.w = cmp(r5.z == 1.000000);
      r4.z = cmp(r4.z >= 50);
      r4.z = r4.z ? r5.w : 0;
      r4.z = (int)r4.z | (int)r4.w;
      r7.x = ~(int)r4.z;
      r6.w = cmp(r1.w < r6.w);
      r6.w = r6.w ? r7.x : 0;
      if (r6.w != 0) {
        r5.z = -1 + r5.z;
        r7.xy = float2(0.5,0.5) + r5.xy;
        r7.zw = rcp(r2.zw);
        r7.xy = -r7.xy * r7.zw + r6.xy;
        r7.xy = cmp(r7.xy >= float2(0,0));
        r7.xy = r7.xy ? float2(1,1) : 0;
        r5.xy = r5.xy * float2(2,2) + r7.xy;
        r2.zw = r2.zw + r2.zw;
        r4.w = 0;
      } else {
        r7.xy = r5.xy + r3.zw;
        r7.xy = r7.xy / r2.zw;
        r7.xy = r7.xy + -r1.xy;
        r7.xy = r7.xy / r0.xz;
        r6.w = cmp(r7.y >= r7.x);
        r8.y = r6.w ? 1.000000 : 0;
        r8.z = r6.w ? 0 : 1;
        r7.zw = r8.yz * r4.xy;
        r5.xy = r4.xy * r8.yz + r5.xy;
        r6.w = min(r7.x, r7.y);
        r6.xyz = r6.www * r0.xzw + r1.xyz;
        r6.w = rcp(r6.z);
        r7.x = r6.w + -r1.w;
        r7.x = cmp(r7.x >= 50);
        r5.w = r5.w ? r7.x : 0;
        r4.w = (int)r4.z | (int)r5.w;
        if (r4.w == 0) {
          r4.z = cmp(r1.w < r6.w);
          if (r4.z != 0) {
            r4.z = rcp(r1.w);
            r4.z = r4.z + -r1.z;
            r5.w = rcp(r0.w);
            r4.z = r5.w * r4.z;
            r6.xyz = r4.zzz * r0.xzw + r1.xyz;
            r5.z = -1 + r5.z;
            r2.zw = r2.zw + r2.zw;
            r7.xy = r6.xy * r2.zw;
            r5.xy = floor(r7.xy);
          } else {
            r4.z = cmp(r5.z != 4.000000);
            r7.xy = cmp(r7.zw >= float2(0,0));
            r7.zw = float2(0.5,0.5) * r2.zw;
            r7.zw = floor(r7.zw);
            r8.x = 1 + r5.z;
            r7.xy = r7.xy ? float2(0,0) : float2(-0.5,-0.5);
            r8.yz = r5.xy * float2(0.5,0.5) + r7.xy;
            r2.zw = r4.zz ? r7.zw : r2.zw;
            r5.z = 4;
            r5.xyz = r4.zzz ? r8.yzx : r5.xyz;
          }
        }
      }
      r3.y = 14;
    }
    r4.z = cmp(r5.z >= 1);
    r7.xy = cmp(float2(0,0) >= r6.xy);
    r7.zw = cmp(r6.xy >= float2(1,1));
    r5.w = (int)r7.z | (int)r7.x;
    r5.w = (int)r7.y | (int)r5.w;
    r5.w = (int)r7.w | (int)r5.w;
    r6.w = rcp(r6.z);
    r7.x = cmp(r6.w >= cb0[0].w);
    r5.w = (int)r5.w | (int)r7.x;
    r5.w = ~(int)r5.w;
    r4.z = r4.z ? r5.w : 0;
    r3.x = r3.x ? r4.z : 0;
    if (r3.x != 0) {
      r7.xyzw = (int4)r5.xyzz;
      r1.w = t0.Load(r7.xyz).x;
      r4.z = r6.w + -r1.w;
      r5.w = cmp(r5.z == 1.000000);
      r4.z = cmp(r4.z >= 50);
      r4.z = r4.z ? r5.w : 0;
      r4.z = (int)r4.z | (int)r4.w;
      r7.x = ~(int)r4.z;
      r6.w = cmp(r1.w < r6.w);
      r6.w = r6.w ? r7.x : 0;
      if (r6.w != 0) {
        r5.z = -1 + r5.z;
        r7.xy = float2(0.5,0.5) + r5.xy;
        r7.zw = rcp(r2.zw);
        r7.xy = -r7.xy * r7.zw + r6.xy;
        r7.xy = cmp(r7.xy >= float2(0,0));
        r7.xy = r7.xy ? float2(1,1) : 0;
        r5.xy = r5.xy * float2(2,2) + r7.xy;
        r2.zw = r2.zw + r2.zw;
        r4.w = 0;
      } else {
        r7.xy = r5.xy + r3.zw;
        r7.xy = r7.xy / r2.zw;
        r7.xy = r7.xy + -r1.xy;
        r7.xy = r7.xy / r0.xz;
        r6.w = cmp(r7.y >= r7.x);
        r8.y = r6.w ? 1.000000 : 0;
        r8.z = r6.w ? 0 : 1;
        r7.zw = r8.yz * r4.xy;
        r5.xy = r4.xy * r8.yz + r5.xy;
        r6.w = min(r7.x, r7.y);
        r6.xyz = r6.www * r0.xzw + r1.xyz;
        r6.w = rcp(r6.z);
        r7.x = r6.w + -r1.w;
        r7.x = cmp(r7.x >= 50);
        r5.w = r5.w ? r7.x : 0;
        r4.w = (int)r4.z | (int)r5.w;
        if (r4.w == 0) {
          r4.z = cmp(r1.w < r6.w);
          if (r4.z != 0) {
            r4.z = rcp(r1.w);
            r4.z = r4.z + -r1.z;
            r5.w = rcp(r0.w);
            r4.z = r5.w * r4.z;
            r6.xyz = r4.zzz * r0.xzw + r1.xyz;
            r5.z = -1 + r5.z;
            r2.zw = r2.zw + r2.zw;
            r7.xy = r6.xy * r2.zw;
            r5.xy = floor(r7.xy);
          } else {
            r4.z = cmp(r5.z != 4.000000);
            r7.xy = cmp(r7.zw >= float2(0,0));
            r7.zw = float2(0.5,0.5) * r2.zw;
            r7.zw = floor(r7.zw);
            r8.x = 1 + r5.z;
            r7.xy = r7.xy ? float2(0,0) : float2(-0.5,-0.5);
            r8.yz = r5.xy * float2(0.5,0.5) + r7.xy;
            r2.zw = r4.zz ? r7.zw : r2.zw;
            r5.z = 4;
            r5.xyz = r4.zzz ? r8.yzx : r5.xyz;
          }
        }
      }
      r3.y = 15;
    }
    r4.z = cmp(r5.z >= 1);
    r7.xy = cmp(float2(0,0) >= r6.xy);
    r7.zw = cmp(r6.xy >= float2(1,1));
    r5.w = (int)r7.z | (int)r7.x;
    r5.w = (int)r7.y | (int)r5.w;
    r5.w = (int)r7.w | (int)r5.w;
    r6.w = rcp(r6.z);
    r7.x = cmp(r6.w >= cb0[0].w);
    r5.w = (int)r5.w | (int)r7.x;
    r5.w = ~(int)r5.w;
    r4.z = r4.z ? r5.w : 0;
    r3.x = r3.x ? r4.z : 0;
    if (r3.x != 0) {
      r7.xyzw = (int4)r5.xyzz;
      r1.w = t0.Load(r7.xyz).x;
      r4.z = r6.w + -r1.w;
      r5.w = cmp(r5.z == 1.000000);
      r4.z = cmp(r4.z >= 50);
      r4.z = r4.z ? r5.w : 0;
      r4.z = (int)r4.z | (int)r4.w;
      r7.x = ~(int)r4.z;
      r6.w = cmp(r1.w < r6.w);
      r6.w = r6.w ? r7.x : 0;
      if (r6.w != 0) {
        r5.z = -1 + r5.z;
        r7.xy = float2(0.5,0.5) + r5.xy;
        r7.zw = rcp(r2.zw);
        r7.xy = -r7.xy * r7.zw + r6.xy;
        r7.xy = cmp(r7.xy >= float2(0,0));
        r7.xy = r7.xy ? float2(1,1) : 0;
        r5.xy = r5.xy * float2(2,2) + r7.xy;
        r2.zw = r2.zw + r2.zw;
        r4.w = 0;
      } else {
        r7.xy = r5.xy + r3.zw;
        r7.xy = r7.xy / r2.zw;
        r7.xy = r7.xy + -r1.xy;
        r7.xy = r7.xy / r0.xz;
        r6.w = cmp(r7.y >= r7.x);
        r8.y = r6.w ? 1.000000 : 0;
        r8.z = r6.w ? 0 : 1;
        r7.zw = r8.yz * r4.xy;
        r5.xy = r4.xy * r8.yz + r5.xy;
        r6.w = min(r7.x, r7.y);
        r6.xyz = r6.www * r0.xzw + r1.xyz;
        r6.w = rcp(r6.z);
        r7.x = r6.w + -r1.w;
        r7.x = cmp(r7.x >= 50);
        r5.w = r5.w ? r7.x : 0;
        r4.w = (int)r4.z | (int)r5.w;
        if (r4.w == 0) {
          r4.z = cmp(r1.w < r6.w);
          if (r4.z != 0) {
            r4.z = rcp(r1.w);
            r4.z = r4.z + -r1.z;
            r5.w = rcp(r0.w);
            r4.z = r5.w * r4.z;
            r6.xyz = r4.zzz * r0.xzw + r1.xyz;
            r5.z = -1 + r5.z;
            r2.zw = r2.zw + r2.zw;
            r7.xy = r6.xy * r2.zw;
            r5.xy = floor(r7.xy);
          } else {
            r4.z = cmp(r5.z != 4.000000);
            r7.xy = cmp(r7.zw >= float2(0,0));
            r7.zw = float2(0.5,0.5) * r2.zw;
            r7.zw = floor(r7.zw);
            r8.x = 1 + r5.z;
            r7.xy = r7.xy ? float2(0,0) : float2(-0.5,-0.5);
            r8.yz = r5.xy * float2(0.5,0.5) + r7.xy;
            r2.zw = r4.zz ? r7.zw : r2.zw;
            r5.z = 4;
            r5.xyz = r4.zzz ? r8.yzx : r5.xyz;
          }
        }
      }
      r3.y = 16;
    }
    r4.z = cmp(r5.z >= 1);
    r7.xy = cmp(float2(0,0) >= r6.xy);
    r7.zw = cmp(r6.xy >= float2(1,1));
    r5.w = (int)r7.z | (int)r7.x;
    r5.w = (int)r7.y | (int)r5.w;
    r5.w = (int)r7.w | (int)r5.w;
    r6.w = rcp(r6.z);
    r7.x = cmp(r6.w >= cb0[0].w);
    r5.w = (int)r5.w | (int)r7.x;
    r5.w = ~(int)r5.w;
    r4.z = r4.z ? r5.w : 0;
    r3.x = r3.x ? r4.z : 0;
    if (r3.x != 0) {
      r7.xyzw = (int4)r5.xyzz;
      r1.w = t0.Load(r7.xyz).x;
      r4.z = r6.w + -r1.w;
      r5.w = cmp(r5.z == 1.000000);
      r4.z = cmp(r4.z >= 50);
      r4.z = r4.z ? r5.w : 0;
      r4.z = (int)r4.z | (int)r4.w;
      r7.x = ~(int)r4.z;
      r6.w = cmp(r1.w < r6.w);
      r6.w = r6.w ? r7.x : 0;
      if (r6.w != 0) {
        r5.z = -1 + r5.z;
        r7.xy = float2(0.5,0.5) + r5.xy;
        r7.zw = rcp(r2.zw);
        r7.xy = -r7.xy * r7.zw + r6.xy;
        r7.xy = cmp(r7.xy >= float2(0,0));
        r7.xy = r7.xy ? float2(1,1) : 0;
        r5.xy = r5.xy * float2(2,2) + r7.xy;
        r2.zw = r2.zw + r2.zw;
        r4.w = 0;
      } else {
        r7.xy = r5.xy + r3.zw;
        r7.xy = r7.xy / r2.zw;
        r7.xy = r7.xy + -r1.xy;
        r7.xy = r7.xy / r0.xz;
        r6.w = cmp(r7.y >= r7.x);
        r8.y = r6.w ? 1.000000 : 0;
        r8.z = r6.w ? 0 : 1;
        r7.zw = r8.yz * r4.xy;
        r5.xy = r4.xy * r8.yz + r5.xy;
        r6.w = min(r7.x, r7.y);
        r6.xyz = r6.www * r0.xzw + r1.xyz;
        r6.w = rcp(r6.z);
        r7.x = r6.w + -r1.w;
        r7.x = cmp(r7.x >= 50);
        r5.w = r5.w ? r7.x : 0;
        r4.w = (int)r4.z | (int)r5.w;
        if (r4.w == 0) {
          r4.z = cmp(r1.w < r6.w);
          if (r4.z != 0) {
            r4.z = rcp(r1.w);
            r4.z = r4.z + -r1.z;
            r5.w = rcp(r0.w);
            r4.z = r5.w * r4.z;
            r6.xyz = r4.zzz * r0.xzw + r1.xyz;
            r5.z = -1 + r5.z;
            r2.zw = r2.zw + r2.zw;
            r7.xy = r6.xy * r2.zw;
            r5.xy = floor(r7.xy);
          } else {
            r4.z = cmp(r5.z != 4.000000);
            r7.xy = cmp(r7.zw >= float2(0,0));
            r7.zw = float2(0.5,0.5) * r2.zw;
            r7.zw = floor(r7.zw);
            r8.x = 1 + r5.z;
            r7.xy = r7.xy ? float2(0,0) : float2(-0.5,-0.5);
            r8.yz = r5.xy * float2(0.5,0.5) + r7.xy;
            r2.zw = r4.zz ? r7.zw : r2.zw;
            r5.z = 4;
            r5.xyz = r4.zzz ? r8.yzx : r5.xyz;
          }
        }
      }
      r3.y = 17;
    }
    r4.z = cmp(r5.z >= 1);
    r7.xy = cmp(float2(0,0) >= r6.xy);
    r7.zw = cmp(r6.xy >= float2(1,1));
    r5.w = (int)r7.z | (int)r7.x;
    r5.w = (int)r7.y | (int)r5.w;
    r5.w = (int)r7.w | (int)r5.w;
    r6.w = rcp(r6.z);
    r7.x = cmp(r6.w >= cb0[0].w);
    r5.w = (int)r5.w | (int)r7.x;
    r5.w = ~(int)r5.w;
    r4.z = r4.z ? r5.w : 0;
    r3.x = r3.x ? r4.z : 0;
    if (r3.x != 0) {
      r7.xyzw = (int4)r5.xyzz;
      r1.w = t0.Load(r7.xyz).x;
      r4.z = r6.w + -r1.w;
      r5.w = cmp(r5.z == 1.000000);
      r4.z = cmp(r4.z >= 50);
      r4.z = r4.z ? r5.w : 0;
      r4.z = (int)r4.z | (int)r4.w;
      r7.x = ~(int)r4.z;
      r6.w = cmp(r1.w < r6.w);
      r6.w = r6.w ? r7.x : 0;
      if (r6.w != 0) {
        r5.z = -1 + r5.z;
        r7.xy = float2(0.5,0.5) + r5.xy;
        r7.zw = rcp(r2.zw);
        r7.xy = -r7.xy * r7.zw + r6.xy;
        r7.xy = cmp(r7.xy >= float2(0,0));
        r7.xy = r7.xy ? float2(1,1) : 0;
        r5.xy = r5.xy * float2(2,2) + r7.xy;
        r2.zw = r2.zw + r2.zw;
        r4.w = 0;
      } else {
        r7.xy = r5.xy + r3.zw;
        r7.xy = r7.xy / r2.zw;
        r7.xy = r7.xy + -r1.xy;
        r7.xy = r7.xy / r0.xz;
        r6.w = cmp(r7.y >= r7.x);
        r8.y = r6.w ? 1.000000 : 0;
        r8.z = r6.w ? 0 : 1;
        r7.zw = r8.yz * r4.xy;
        r5.xy = r4.xy * r8.yz + r5.xy;
        r6.w = min(r7.x, r7.y);
        r6.xyz = r6.www * r0.xzw + r1.xyz;
        r6.w = rcp(r6.z);
        r7.x = r6.w + -r1.w;
        r7.x = cmp(r7.x >= 50);
        r5.w = r5.w ? r7.x : 0;
        r4.w = (int)r4.z | (int)r5.w;
        if (r4.w == 0) {
          r4.z = cmp(r1.w < r6.w);
          if (r4.z != 0) {
            r4.z = rcp(r1.w);
            r4.z = r4.z + -r1.z;
            r5.w = rcp(r0.w);
            r4.z = r5.w * r4.z;
            r6.xyz = r4.zzz * r0.xzw + r1.xyz;
            r5.z = -1 + r5.z;
            r2.zw = r2.zw + r2.zw;
            r7.xy = r6.xy * r2.zw;
            r5.xy = floor(r7.xy);
          } else {
            r4.z = cmp(r5.z != 4.000000);
            r7.xy = cmp(r7.zw >= float2(0,0));
            r7.zw = float2(0.5,0.5) * r2.zw;
            r7.zw = floor(r7.zw);
            r8.x = 1 + r5.z;
            r7.xy = r7.xy ? float2(0,0) : float2(-0.5,-0.5);
            r8.yz = r5.xy * float2(0.5,0.5) + r7.xy;
            r2.zw = r4.zz ? r7.zw : r2.zw;
            r5.z = 4;
            r5.xyz = r4.zzz ? r8.yzx : r5.xyz;
          }
        }
      }
      r3.y = 18;
    }
    r4.z = cmp(r5.z >= 1);
    r7.xy = cmp(float2(0,0) >= r6.xy);
    r7.zw = cmp(r6.xy >= float2(1,1));
    r5.w = (int)r7.z | (int)r7.x;
    r5.w = (int)r7.y | (int)r5.w;
    r5.w = (int)r7.w | (int)r5.w;
    r6.w = rcp(r6.z);
    r7.x = cmp(r6.w >= cb0[0].w);
    r5.w = (int)r5.w | (int)r7.x;
    r5.w = ~(int)r5.w;
    r4.z = r4.z ? r5.w : 0;
    r3.x = r3.x ? r4.z : 0;
    if (r3.x != 0) {
      r7.xyzw = (int4)r5.xyzz;
      r1.w = t0.Load(r7.xyz).x;
      r4.z = r6.w + -r1.w;
      r5.w = cmp(r5.z == 1.000000);
      r4.z = cmp(r4.z >= 50);
      r4.z = r4.z ? r5.w : 0;
      r4.z = (int)r4.z | (int)r4.w;
      r7.x = ~(int)r4.z;
      r6.w = cmp(r1.w < r6.w);
      r6.w = r6.w ? r7.x : 0;
      if (r6.w != 0) {
        r5.z = -1 + r5.z;
        r7.xy = float2(0.5,0.5) + r5.xy;
        r7.zw = rcp(r2.zw);
        r7.xy = -r7.xy * r7.zw + r6.xy;
        r7.xy = cmp(r7.xy >= float2(0,0));
        r7.xy = r7.xy ? float2(1,1) : 0;
        r5.xy = r5.xy * float2(2,2) + r7.xy;
        r2.zw = r2.zw + r2.zw;
        r4.w = 0;
      } else {
        r7.xy = r5.xy + r3.zw;
        r7.xy = r7.xy / r2.zw;
        r7.xy = r7.xy + -r1.xy;
        r7.xy = r7.xy / r0.xz;
        r6.w = cmp(r7.y >= r7.x);
        r8.y = r6.w ? 1.000000 : 0;
        r8.z = r6.w ? 0 : 1;
        r7.zw = r8.yz * r4.xy;
        r5.xy = r4.xy * r8.yz + r5.xy;
        r6.w = min(r7.x, r7.y);
        r6.xyz = r6.www * r0.xzw + r1.xyz;
        r6.w = rcp(r6.z);
        r7.x = r6.w + -r1.w;
        r7.x = cmp(r7.x >= 50);
        r5.w = r5.w ? r7.x : 0;
        r4.w = (int)r4.z | (int)r5.w;
        if (r4.w == 0) {
          r4.z = cmp(r1.w < r6.w);
          if (r4.z != 0) {
            r4.z = rcp(r1.w);
            r4.z = r4.z + -r1.z;
            r5.w = rcp(r0.w);
            r4.z = r5.w * r4.z;
            r6.xyz = r4.zzz * r0.xzw + r1.xyz;
            r5.z = -1 + r5.z;
            r2.zw = r2.zw + r2.zw;
            r7.xy = r6.xy * r2.zw;
            r5.xy = floor(r7.xy);
          } else {
            r4.z = cmp(r5.z != 4.000000);
            r7.xy = cmp(r7.zw >= float2(0,0));
            r7.zw = float2(0.5,0.5) * r2.zw;
            r7.zw = floor(r7.zw);
            r8.x = 1 + r5.z;
            r7.xy = r7.xy ? float2(0,0) : float2(-0.5,-0.5);
            r8.yz = r5.xy * float2(0.5,0.5) + r7.xy;
            r2.zw = r4.zz ? r7.zw : r2.zw;
            r5.z = 4;
            r5.xyz = r4.zzz ? r8.yzx : r5.xyz;
          }
        }
      }
      r3.y = 19;
    }
    r4.z = cmp(r5.z >= 1);
    r7.xy = cmp(float2(0,0) >= r6.xy);
    r7.zw = cmp(r6.xy >= float2(1,1));
    r5.w = (int)r7.z | (int)r7.x;
    r5.w = (int)r7.y | (int)r5.w;
    r5.w = (int)r7.w | (int)r5.w;
    r6.w = rcp(r6.z);
    r7.x = cmp(r6.w >= cb0[0].w);
    r5.w = (int)r5.w | (int)r7.x;
    r5.w = ~(int)r5.w;
    r4.z = r4.z ? r5.w : 0;
    r3.x = r3.x ? r4.z : 0;
    if (r3.x != 0) {
      r7.xyzw = (int4)r5.xyzz;
      r1.w = t0.Load(r7.xyz).x;
      r4.z = r6.w + -r1.w;
      r5.w = cmp(r5.z == 1.000000);
      r4.z = cmp(r4.z >= 50);
      r4.z = r4.z ? r5.w : 0;
      r4.z = (int)r4.z | (int)r4.w;
      r7.x = ~(int)r4.z;
      r6.w = cmp(r1.w < r6.w);
      r6.w = r6.w ? r7.x : 0;
      if (r6.w != 0) {
        r5.z = -1 + r5.z;
        r7.xy = float2(0.5,0.5) + r5.xy;
        r7.zw = rcp(r2.zw);
        r7.xy = -r7.xy * r7.zw + r6.xy;
        r7.xy = cmp(r7.xy >= float2(0,0));
        r7.xy = r7.xy ? float2(1,1) : 0;
        r5.xy = r5.xy * float2(2,2) + r7.xy;
        r2.zw = r2.zw + r2.zw;
        r4.w = 0;
      } else {
        r7.xy = r5.xy + r3.zw;
        r7.xy = r7.xy / r2.zw;
        r7.xy = r7.xy + -r1.xy;
        r7.xy = r7.xy / r0.xz;
        r6.w = cmp(r7.y >= r7.x);
        r8.y = r6.w ? 1.000000 : 0;
        r8.z = r6.w ? 0 : 1;
        r7.zw = r8.yz * r4.xy;
        r5.xy = r4.xy * r8.yz + r5.xy;
        r6.w = min(r7.x, r7.y);
        r6.xyz = r6.www * r0.xzw + r1.xyz;
        r6.w = rcp(r6.z);
        r7.x = r6.w + -r1.w;
        r7.x = cmp(r7.x >= 50);
        r5.w = r5.w ? r7.x : 0;
        r4.w = (int)r4.z | (int)r5.w;
        if (r4.w == 0) {
          r4.z = cmp(r1.w < r6.w);
          if (r4.z != 0) {
            r4.z = rcp(r1.w);
            r4.z = r4.z + -r1.z;
            r5.w = rcp(r0.w);
            r4.z = r5.w * r4.z;
            r6.xyz = r4.zzz * r0.xzw + r1.xyz;
            r5.z = -1 + r5.z;
            r2.zw = r2.zw + r2.zw;
            r7.xy = r6.xy * r2.zw;
            r5.xy = floor(r7.xy);
          } else {
            r4.z = cmp(r5.z != 4.000000);
            r7.xy = cmp(r7.zw >= float2(0,0));
            r7.zw = float2(0.5,0.5) * r2.zw;
            r7.zw = floor(r7.zw);
            r8.x = 1 + r5.z;
            r7.xy = r7.xy ? float2(0,0) : float2(-0.5,-0.5);
            r8.yz = r5.xy * float2(0.5,0.5) + r7.xy;
            r2.zw = r4.zz ? r7.zw : r2.zw;
            r5.z = 4;
            r5.xyz = r4.zzz ? r8.yzx : r5.xyz;
          }
        }
      }
      r3.y = 20;
    }
    r4.z = cmp(r5.z >= 1);
    r7.xy = cmp(float2(0,0) >= r6.xy);
    r7.zw = cmp(r6.xy >= float2(1,1));
    r5.w = (int)r7.z | (int)r7.x;
    r5.w = (int)r7.y | (int)r5.w;
    r5.w = (int)r7.w | (int)r5.w;
    r6.w = rcp(r6.z);
    r7.x = cmp(r6.w >= cb0[0].w);
    r5.w = (int)r5.w | (int)r7.x;
    r5.w = ~(int)r5.w;
    r4.z = r4.z ? r5.w : 0;
    r3.x = r3.x ? r4.z : 0;
    if (r3.x != 0) {
      r7.xyzw = (int4)r5.xyzz;
      r1.w = t0.Load(r7.xyz).x;
      r4.z = r6.w + -r1.w;
      r5.w = cmp(r5.z == 1.000000);
      r4.z = cmp(r4.z >= 50);
      r4.z = r4.z ? r5.w : 0;
      r4.z = (int)r4.z | (int)r4.w;
      r7.x = ~(int)r4.z;
      r6.w = cmp(r1.w < r6.w);
      r6.w = r6.w ? r7.x : 0;
      if (r6.w != 0) {
        r5.z = -1 + r5.z;
        r7.xy = float2(0.5,0.5) + r5.xy;
        r7.zw = rcp(r2.zw);
        r7.xy = -r7.xy * r7.zw + r6.xy;
        r7.xy = cmp(r7.xy >= float2(0,0));
        r7.xy = r7.xy ? float2(1,1) : 0;
        r5.xy = r5.xy * float2(2,2) + r7.xy;
        r2.zw = r2.zw + r2.zw;
        r4.w = 0;
      } else {
        r7.xy = r5.xy + r3.zw;
        r7.xy = r7.xy / r2.zw;
        r7.xy = r7.xy + -r1.xy;
        r7.xy = r7.xy / r0.xz;
        r6.w = cmp(r7.y >= r7.x);
        r8.y = r6.w ? 1.000000 : 0;
        r8.z = r6.w ? 0 : 1;
        r7.zw = r8.yz * r4.xy;
        r5.xy = r4.xy * r8.yz + r5.xy;
        r6.w = min(r7.x, r7.y);
        r6.xyz = r6.www * r0.xzw + r1.xyz;
        r6.w = rcp(r6.z);
        r7.x = r6.w + -r1.w;
        r7.x = cmp(r7.x >= 50);
        r5.w = r5.w ? r7.x : 0;
        r4.w = (int)r4.z | (int)r5.w;
        if (r4.w == 0) {
          r4.z = cmp(r1.w < r6.w);
          if (r4.z != 0) {
            r4.z = rcp(r1.w);
            r4.z = r4.z + -r1.z;
            r5.w = rcp(r0.w);
            r4.z = r5.w * r4.z;
            r6.xyz = r4.zzz * r0.xzw + r1.xyz;
            r5.z = -1 + r5.z;
            r2.zw = r2.zw + r2.zw;
            r7.xy = r6.xy * r2.zw;
            r5.xy = floor(r7.xy);
          } else {
            r4.z = cmp(r5.z != 4.000000);
            r7.xy = cmp(r7.zw >= float2(0,0));
            r7.zw = float2(0.5,0.5) * r2.zw;
            r7.zw = floor(r7.zw);
            r8.x = 1 + r5.z;
            r7.xy = r7.xy ? float2(0,0) : float2(-0.5,-0.5);
            r8.yz = r5.xy * float2(0.5,0.5) + r7.xy;
            r2.zw = r4.zz ? r7.zw : r2.zw;
            r5.z = 4;
            r5.xyz = r4.zzz ? r8.yzx : r5.xyz;
          }
        }
      }
      r3.y = 21;
    }
    r4.z = cmp(r5.z >= 1);
    r7.xy = cmp(float2(0,0) >= r6.xy);
    r7.zw = cmp(r6.xy >= float2(1,1));
    r5.w = (int)r7.z | (int)r7.x;
    r5.w = (int)r7.y | (int)r5.w;
    r5.w = (int)r7.w | (int)r5.w;
    r6.w = rcp(r6.z);
    r7.x = cmp(r6.w >= cb0[0].w);
    r5.w = (int)r5.w | (int)r7.x;
    r5.w = ~(int)r5.w;
    r4.z = r4.z ? r5.w : 0;
    r3.x = r3.x ? r4.z : 0;
    if (r3.x != 0) {
      r7.xyzw = (int4)r5.xyzz;
      r1.w = t0.Load(r7.xyz).x;
      r4.z = r6.w + -r1.w;
      r5.w = cmp(r5.z == 1.000000);
      r4.z = cmp(r4.z >= 50);
      r4.z = r4.z ? r5.w : 0;
      r4.z = (int)r4.z | (int)r4.w;
      r7.x = ~(int)r4.z;
      r6.w = cmp(r1.w < r6.w);
      r6.w = r6.w ? r7.x : 0;
      if (r6.w != 0) {
        r5.z = -1 + r5.z;
        r7.xy = float2(0.5,0.5) + r5.xy;
        r7.zw = rcp(r2.zw);
        r7.xy = -r7.xy * r7.zw + r6.xy;
        r7.xy = cmp(r7.xy >= float2(0,0));
        r7.xy = r7.xy ? float2(1,1) : 0;
        r5.xy = r5.xy * float2(2,2) + r7.xy;
        r2.zw = r2.zw + r2.zw;
        r4.w = 0;
      } else {
        r7.xy = r5.xy + r3.zw;
        r7.xy = r7.xy / r2.zw;
        r7.xy = r7.xy + -r1.xy;
        r7.xy = r7.xy / r0.xz;
        r6.w = cmp(r7.y >= r7.x);
        r8.y = r6.w ? 1.000000 : 0;
        r8.z = r6.w ? 0 : 1;
        r7.zw = r8.yz * r4.xy;
        r5.xy = r4.xy * r8.yz + r5.xy;
        r6.w = min(r7.x, r7.y);
        r6.xyz = r6.www * r0.xzw + r1.xyz;
        r6.w = rcp(r6.z);
        r7.x = r6.w + -r1.w;
        r7.x = cmp(r7.x >= 50);
        r5.w = r5.w ? r7.x : 0;
        r4.w = (int)r4.z | (int)r5.w;
        if (r4.w == 0) {
          r4.z = cmp(r1.w < r6.w);
          if (r4.z != 0) {
            r4.z = rcp(r1.w);
            r4.z = r4.z + -r1.z;
            r5.w = rcp(r0.w);
            r4.z = r5.w * r4.z;
            r6.xyz = r4.zzz * r0.xzw + r1.xyz;
            r5.z = -1 + r5.z;
            r2.zw = r2.zw + r2.zw;
            r7.xy = r6.xy * r2.zw;
            r5.xy = floor(r7.xy);
          } else {
            r4.z = cmp(r5.z != 4.000000);
            r7.xy = cmp(r7.zw >= float2(0,0));
            r7.zw = float2(0.5,0.5) * r2.zw;
            r7.zw = floor(r7.zw);
            r8.x = 1 + r5.z;
            r7.xy = r7.xy ? float2(0,0) : float2(-0.5,-0.5);
            r8.yz = r5.xy * float2(0.5,0.5) + r7.xy;
            r2.zw = r4.zz ? r7.zw : r2.zw;
            r5.z = 4;
            r5.xyz = r4.zzz ? r8.yzx : r5.xyz;
          }
        }
      }
      r3.y = 22;
    }
    r4.z = cmp(r5.z >= 1);
    r7.xy = cmp(float2(0,0) >= r6.xy);
    r7.zw = cmp(r6.xy >= float2(1,1));
    r5.w = (int)r7.z | (int)r7.x;
    r5.w = (int)r7.y | (int)r5.w;
    r5.w = (int)r7.w | (int)r5.w;
    r6.w = rcp(r6.z);
    r7.x = cmp(r6.w >= cb0[0].w);
    r5.w = (int)r5.w | (int)r7.x;
    r5.w = ~(int)r5.w;
    r4.z = r4.z ? r5.w : 0;
    r3.x = r3.x ? r4.z : 0;
    if (r3.x != 0) {
      r7.xyzw = (int4)r5.xyzz;
      r1.w = t0.Load(r7.xyz).x;
      r4.z = r6.w + -r1.w;
      r5.w = cmp(r5.z == 1.000000);
      r4.z = cmp(r4.z >= 50);
      r4.z = r4.z ? r5.w : 0;
      r4.z = (int)r4.z | (int)r4.w;
      r7.x = ~(int)r4.z;
      r6.w = cmp(r1.w < r6.w);
      r6.w = r6.w ? r7.x : 0;
      if (r6.w != 0) {
        r5.z = -1 + r5.z;
        r7.xy = float2(0.5,0.5) + r5.xy;
        r7.zw = rcp(r2.zw);
        r7.xy = -r7.xy * r7.zw + r6.xy;
        r7.xy = cmp(r7.xy >= float2(0,0));
        r7.xy = r7.xy ? float2(1,1) : 0;
        r5.xy = r5.xy * float2(2,2) + r7.xy;
        r2.zw = r2.zw + r2.zw;
        r4.w = 0;
      } else {
        r7.xy = r5.xy + r3.zw;
        r7.xy = r7.xy / r2.zw;
        r7.xy = r7.xy + -r1.xy;
        r7.xy = r7.xy / r0.xz;
        r6.w = cmp(r7.y >= r7.x);
        r8.y = r6.w ? 1.000000 : 0;
        r8.z = r6.w ? 0 : 1;
        r7.zw = r8.yz * r4.xy;
        r5.xy = r4.xy * r8.yz + r5.xy;
        r6.w = min(r7.x, r7.y);
        r6.xyz = r6.www * r0.xzw + r1.xyz;
        r6.w = rcp(r6.z);
        r7.x = r6.w + -r1.w;
        r7.x = cmp(r7.x >= 50);
        r5.w = r5.w ? r7.x : 0;
        r4.w = (int)r4.z | (int)r5.w;
        if (r4.w == 0) {
          r4.z = cmp(r1.w < r6.w);
          if (r4.z != 0) {
            r4.z = rcp(r1.w);
            r4.z = r4.z + -r1.z;
            r5.w = rcp(r0.w);
            r4.z = r5.w * r4.z;
            r6.xyz = r4.zzz * r0.xzw + r1.xyz;
            r5.z = -1 + r5.z;
            r2.zw = r2.zw + r2.zw;
            r7.xy = r6.xy * r2.zw;
            r5.xy = floor(r7.xy);
          } else {
            r4.z = cmp(r5.z != 4.000000);
            r7.xy = cmp(r7.zw >= float2(0,0));
            r7.zw = float2(0.5,0.5) * r2.zw;
            r7.zw = floor(r7.zw);
            r8.x = 1 + r5.z;
            r7.xy = r7.xy ? float2(0,0) : float2(-0.5,-0.5);
            r8.yz = r5.xy * float2(0.5,0.5) + r7.xy;
            r2.zw = r4.zz ? r7.zw : r2.zw;
            r5.z = 4;
            r5.xyz = r4.zzz ? r8.yzx : r5.xyz;
          }
        }
      }
      r3.y = 23;
    }
    r4.z = cmp(r5.z >= 1);
    r7.xy = cmp(float2(0,0) >= r6.xy);
    r7.zw = cmp(r6.xy >= float2(1,1));
    r5.w = (int)r7.z | (int)r7.x;
    r5.w = (int)r7.y | (int)r5.w;
    r5.w = (int)r7.w | (int)r5.w;
    r6.w = rcp(r6.z);
    r7.x = cmp(r6.w >= cb0[0].w);
    r5.w = (int)r5.w | (int)r7.x;
    r5.w = ~(int)r5.w;
    r4.z = r4.z ? r5.w : 0;
    r3.x = r3.x ? r4.z : 0;
    if (r3.x != 0) {
      r7.xyzw = (int4)r5.xyzz;
      r1.w = t0.Load(r7.xyz).x;
      r4.z = r6.w + -r1.w;
      r5.w = cmp(r5.z == 1.000000);
      r4.z = cmp(r4.z >= 50);
      r4.z = r4.z ? r5.w : 0;
      r4.z = (int)r4.z | (int)r4.w;
      r7.x = ~(int)r4.z;
      r6.w = cmp(r1.w < r6.w);
      r6.w = r6.w ? r7.x : 0;
      if (r6.w != 0) {
        r5.z = -1 + r5.z;
        r7.xy = float2(0.5,0.5) + r5.xy;
        r7.zw = rcp(r2.zw);
        r7.xy = -r7.xy * r7.zw + r6.xy;
        r7.xy = cmp(r7.xy >= float2(0,0));
        r7.xy = r7.xy ? float2(1,1) : 0;
        r5.xy = r5.xy * float2(2,2) + r7.xy;
        r2.zw = r2.zw + r2.zw;
        r4.w = 0;
      } else {
        r7.xy = r5.xy + r3.zw;
        r7.xy = r7.xy / r2.zw;
        r7.xy = r7.xy + -r1.xy;
        r7.xy = r7.xy / r0.xz;
        r6.w = cmp(r7.y >= r7.x);
        r8.y = r6.w ? 1.000000 : 0;
        r8.z = r6.w ? 0 : 1;
        r7.zw = r8.yz * r4.xy;
        r5.xy = r4.xy * r8.yz + r5.xy;
        r6.w = min(r7.x, r7.y);
        r6.xyz = r6.www * r0.xzw + r1.xyz;
        r6.w = rcp(r6.z);
        r7.x = r6.w + -r1.w;
        r7.x = cmp(r7.x >= 50);
        r5.w = r5.w ? r7.x : 0;
        r4.w = (int)r4.z | (int)r5.w;
        if (r4.w == 0) {
          r4.z = cmp(r1.w < r6.w);
          if (r4.z != 0) {
            r4.z = rcp(r1.w);
            r4.z = r4.z + -r1.z;
            r5.w = rcp(r0.w);
            r4.z = r5.w * r4.z;
            r6.xyz = r4.zzz * r0.xzw + r1.xyz;
            r5.z = -1 + r5.z;
            r2.zw = r2.zw + r2.zw;
            r7.xy = r6.xy * r2.zw;
            r5.xy = floor(r7.xy);
          } else {
            r4.z = cmp(r5.z != 4.000000);
            r7.xy = cmp(r7.zw >= float2(0,0));
            r7.zw = float2(0.5,0.5) * r2.zw;
            r7.zw = floor(r7.zw);
            r8.x = 1 + r5.z;
            r7.xy = r7.xy ? float2(0,0) : float2(-0.5,-0.5);
            r8.yz = r5.xy * float2(0.5,0.5) + r7.xy;
            r2.zw = r4.zz ? r7.zw : r2.zw;
            r5.z = 4;
            r5.xyz = r4.zzz ? r8.yzx : r5.xyz;
          }
        }
      }
      r3.y = 24;
    }
    r4.z = cmp(r5.z >= 1);
    r7.xy = cmp(float2(0,0) >= r6.xy);
    r7.zw = cmp(r6.xy >= float2(1,1));
    r5.w = (int)r7.z | (int)r7.x;
    r5.w = (int)r7.y | (int)r5.w;
    r5.w = (int)r7.w | (int)r5.w;
    r6.w = rcp(r6.z);
    r7.x = cmp(r6.w >= cb0[0].w);
    r5.w = (int)r5.w | (int)r7.x;
    r5.w = ~(int)r5.w;
    r4.z = r4.z ? r5.w : 0;
    r3.x = r3.x ? r4.z : 0;
    if (r3.x != 0) {
      r7.xyzw = (int4)r5.xyzz;
      r1.w = t0.Load(r7.xyz).x;
      r4.z = r6.w + -r1.w;
      r5.w = cmp(r5.z == 1.000000);
      r4.z = cmp(r4.z >= 50);
      r4.z = r4.z ? r5.w : 0;
      r4.z = (int)r4.z | (int)r4.w;
      r7.x = ~(int)r4.z;
      r6.w = cmp(r1.w < r6.w);
      r6.w = r6.w ? r7.x : 0;
      if (r6.w != 0) {
        r5.z = -1 + r5.z;
        r7.xy = float2(0.5,0.5) + r5.xy;
        r7.zw = rcp(r2.zw);
        r7.xy = -r7.xy * r7.zw + r6.xy;
        r7.xy = cmp(r7.xy >= float2(0,0));
        r7.xy = r7.xy ? float2(1,1) : 0;
        r5.xy = r5.xy * float2(2,2) + r7.xy;
        r2.zw = r2.zw + r2.zw;
        r4.w = 0;
      } else {
        r7.xy = r5.xy + r3.zw;
        r7.xy = r7.xy / r2.zw;
        r7.xy = r7.xy + -r1.xy;
        r7.xy = r7.xy / r0.xz;
        r6.w = cmp(r7.y >= r7.x);
        r8.y = r6.w ? 1.000000 : 0;
        r8.z = r6.w ? 0 : 1;
        r7.zw = r8.yz * r4.xy;
        r5.xy = r4.xy * r8.yz + r5.xy;
        r6.w = min(r7.x, r7.y);
        r6.xyz = r6.www * r0.xzw + r1.xyz;
        r6.w = rcp(r6.z);
        r7.x = r6.w + -r1.w;
        r7.x = cmp(r7.x >= 50);
        r5.w = r5.w ? r7.x : 0;
        r4.w = (int)r4.z | (int)r5.w;
        if (r4.w == 0) {
          r4.z = cmp(r1.w < r6.w);
          if (r4.z != 0) {
            r4.z = rcp(r1.w);
            r4.z = r4.z + -r1.z;
            r5.w = rcp(r0.w);
            r4.z = r5.w * r4.z;
            r6.xyz = r4.zzz * r0.xzw + r1.xyz;
            r5.z = -1 + r5.z;
            r2.zw = r2.zw + r2.zw;
            r7.xy = r6.xy * r2.zw;
            r5.xy = floor(r7.xy);
          } else {
            r4.z = cmp(r5.z != 4.000000);
            r7.xy = cmp(r7.zw >= float2(0,0));
            r7.zw = float2(0.5,0.5) * r2.zw;
            r7.zw = floor(r7.zw);
            r8.x = 1 + r5.z;
            r7.xy = r7.xy ? float2(0,0) : float2(-0.5,-0.5);
            r8.yz = r5.xy * float2(0.5,0.5) + r7.xy;
            r2.zw = r4.zz ? r7.zw : r2.zw;
            r5.z = 4;
            r5.xyz = r4.zzz ? r8.yzx : r5.xyz;
          }
        }
      }
      r3.y = 25;
    }
    r4.z = cmp(r5.z >= 1);
    r7.xy = cmp(float2(0,0) >= r6.xy);
    r7.zw = cmp(r6.xy >= float2(1,1));
    r5.w = (int)r7.z | (int)r7.x;
    r5.w = (int)r7.y | (int)r5.w;
    r5.w = (int)r7.w | (int)r5.w;
    r6.w = rcp(r6.z);
    r7.x = cmp(r6.w >= cb0[0].w);
    r5.w = (int)r5.w | (int)r7.x;
    r5.w = ~(int)r5.w;
    r4.z = r4.z ? r5.w : 0;
    r3.x = r3.x ? r4.z : 0;
    if (r3.x != 0) {
      r7.xyzw = (int4)r5.xyzz;
      r1.w = t0.Load(r7.xyz).x;
      r4.z = r6.w + -r1.w;
      r5.w = cmp(r5.z == 1.000000);
      r4.z = cmp(r4.z >= 50);
      r4.z = r4.z ? r5.w : 0;
      r4.z = (int)r4.z | (int)r4.w;
      r7.x = ~(int)r4.z;
      r6.w = cmp(r1.w < r6.w);
      r6.w = r6.w ? r7.x : 0;
      if (r6.w != 0) {
        r5.z = -1 + r5.z;
        r7.xy = float2(0.5,0.5) + r5.xy;
        r7.zw = rcp(r2.zw);
        r7.xy = -r7.xy * r7.zw + r6.xy;
        r7.xy = cmp(r7.xy >= float2(0,0));
        r7.xy = r7.xy ? float2(1,1) : 0;
        r5.xy = r5.xy * float2(2,2) + r7.xy;
        r2.zw = r2.zw + r2.zw;
        r4.w = 0;
      } else {
        r7.xy = r5.xy + r3.zw;
        r7.xy = r7.xy / r2.zw;
        r7.xy = r7.xy + -r1.xy;
        r7.xy = r7.xy / r0.xz;
        r6.w = cmp(r7.y >= r7.x);
        r8.y = r6.w ? 1.000000 : 0;
        r8.z = r6.w ? 0 : 1;
        r7.zw = r8.yz * r4.xy;
        r5.xy = r4.xy * r8.yz + r5.xy;
        r6.w = min(r7.x, r7.y);
        r6.xyz = r6.www * r0.xzw + r1.xyz;
        r6.w = rcp(r6.z);
        r7.x = r6.w + -r1.w;
        r7.x = cmp(r7.x >= 50);
        r5.w = r5.w ? r7.x : 0;
        r4.w = (int)r4.z | (int)r5.w;
        if (r4.w == 0) {
          r4.z = cmp(r1.w < r6.w);
          if (r4.z != 0) {
            r4.z = rcp(r1.w);
            r4.z = r4.z + -r1.z;
            r5.w = rcp(r0.w);
            r4.z = r5.w * r4.z;
            r6.xyz = r4.zzz * r0.xzw + r1.xyz;
            r5.z = -1 + r5.z;
            r2.zw = r2.zw + r2.zw;
            r7.xy = r6.xy * r2.zw;
            r5.xy = floor(r7.xy);
          } else {
            r4.z = cmp(r5.z != 4.000000);
            r7.xy = cmp(r7.zw >= float2(0,0));
            r7.zw = float2(0.5,0.5) * r2.zw;
            r7.zw = floor(r7.zw);
            r8.x = 1 + r5.z;
            r7.xy = r7.xy ? float2(0,0) : float2(-0.5,-0.5);
            r8.yz = r5.xy * float2(0.5,0.5) + r7.xy;
            r2.zw = r4.zz ? r7.zw : r2.zw;
            r5.z = 4;
            r5.xyz = r4.zzz ? r8.yzx : r5.xyz;
          }
        }
      }
      r3.y = 26;
    }
    r4.z = cmp(r5.z >= 1);
    r7.xy = cmp(float2(0,0) >= r6.xy);
    r7.zw = cmp(r6.xy >= float2(1,1));
    r5.w = (int)r7.z | (int)r7.x;
    r5.w = (int)r7.y | (int)r5.w;
    r5.w = (int)r7.w | (int)r5.w;
    r6.w = rcp(r6.z);
    r7.x = cmp(r6.w >= cb0[0].w);
    r5.w = (int)r5.w | (int)r7.x;
    r5.w = ~(int)r5.w;
    r4.z = r4.z ? r5.w : 0;
    r3.x = r3.x ? r4.z : 0;
    if (r3.x != 0) {
      r7.xyzw = (int4)r5.xyzz;
      r1.w = t0.Load(r7.xyz).x;
      r4.z = r6.w + -r1.w;
      r5.w = cmp(r5.z == 1.000000);
      r4.z = cmp(r4.z >= 50);
      r4.z = r4.z ? r5.w : 0;
      r4.z = (int)r4.z | (int)r4.w;
      r7.x = ~(int)r4.z;
      r6.w = cmp(r1.w < r6.w);
      r6.w = r6.w ? r7.x : 0;
      if (r6.w != 0) {
        r5.z = -1 + r5.z;
        r7.xy = float2(0.5,0.5) + r5.xy;
        r7.zw = rcp(r2.zw);
        r7.xy = -r7.xy * r7.zw + r6.xy;
        r7.xy = cmp(r7.xy >= float2(0,0));
        r7.xy = r7.xy ? float2(1,1) : 0;
        r5.xy = r5.xy * float2(2,2) + r7.xy;
        r2.zw = r2.zw + r2.zw;
        r4.w = 0;
      } else {
        r7.xy = r5.xy + r3.zw;
        r7.xy = r7.xy / r2.zw;
        r7.xy = r7.xy + -r1.xy;
        r7.xy = r7.xy / r0.xz;
        r6.w = cmp(r7.y >= r7.x);
        r8.y = r6.w ? 1.000000 : 0;
        r8.z = r6.w ? 0 : 1;
        r7.zw = r8.yz * r4.xy;
        r5.xy = r4.xy * r8.yz + r5.xy;
        r6.w = min(r7.x, r7.y);
        r6.xyz = r6.www * r0.xzw + r1.xyz;
        r6.w = rcp(r6.z);
        r7.x = r6.w + -r1.w;
        r7.x = cmp(r7.x >= 50);
        r5.w = r5.w ? r7.x : 0;
        r4.w = (int)r4.z | (int)r5.w;
        if (r4.w == 0) {
          r4.z = cmp(r1.w < r6.w);
          if (r4.z != 0) {
            r4.z = rcp(r1.w);
            r4.z = r4.z + -r1.z;
            r5.w = rcp(r0.w);
            r4.z = r5.w * r4.z;
            r6.xyz = r4.zzz * r0.xzw + r1.xyz;
            r5.z = -1 + r5.z;
            r2.zw = r2.zw + r2.zw;
            r7.xy = r6.xy * r2.zw;
            r5.xy = floor(r7.xy);
          } else {
            r4.z = cmp(r5.z != 4.000000);
            r7.xy = cmp(r7.zw >= float2(0,0));
            r7.zw = float2(0.5,0.5) * r2.zw;
            r7.zw = floor(r7.zw);
            r8.x = 1 + r5.z;
            r7.xy = r7.xy ? float2(0,0) : float2(-0.5,-0.5);
            r8.yz = r5.xy * float2(0.5,0.5) + r7.xy;
            r2.zw = r4.zz ? r7.zw : r2.zw;
            r5.z = 4;
            r5.xyz = r4.zzz ? r8.yzx : r5.xyz;
          }
        }
      }
      r3.y = 27;
    }
    r4.z = cmp(r5.z >= 1);
    r7.xy = cmp(float2(0,0) >= r6.xy);
    r7.zw = cmp(r6.xy >= float2(1,1));
    r5.w = (int)r7.z | (int)r7.x;
    r5.w = (int)r7.y | (int)r5.w;
    r5.w = (int)r7.w | (int)r5.w;
    r6.w = rcp(r6.z);
    r7.x = cmp(r6.w >= cb0[0].w);
    r5.w = (int)r5.w | (int)r7.x;
    r5.w = ~(int)r5.w;
    r4.z = r4.z ? r5.w : 0;
    r3.x = r3.x ? r4.z : 0;
    if (r3.x != 0) {
      r7.xyzw = (int4)r5.xyzz;
      r1.w = t0.Load(r7.xyz).x;
      r4.z = r6.w + -r1.w;
      r5.w = cmp(r5.z == 1.000000);
      r4.z = cmp(r4.z >= 50);
      r4.z = r4.z ? r5.w : 0;
      r4.z = (int)r4.z | (int)r4.w;
      r7.x = ~(int)r4.z;
      r6.w = cmp(r1.w < r6.w);
      r6.w = r6.w ? r7.x : 0;
      if (r6.w != 0) {
        r5.z = -1 + r5.z;
        r7.xy = float2(0.5,0.5) + r5.xy;
        r7.zw = rcp(r2.zw);
        r7.xy = -r7.xy * r7.zw + r6.xy;
        r7.xy = cmp(r7.xy >= float2(0,0));
        r7.xy = r7.xy ? float2(1,1) : 0;
        r5.xy = r5.xy * float2(2,2) + r7.xy;
        r2.zw = r2.zw + r2.zw;
        r4.w = 0;
      } else {
        r7.xy = r5.xy + r3.zw;
        r7.xy = r7.xy / r2.zw;
        r7.xy = r7.xy + -r1.xy;
        r7.xy = r7.xy / r0.xz;
        r6.w = cmp(r7.y >= r7.x);
        r8.y = r6.w ? 1.000000 : 0;
        r8.z = r6.w ? 0 : 1;
        r7.zw = r8.yz * r4.xy;
        r5.xy = r4.xy * r8.yz + r5.xy;
        r6.w = min(r7.x, r7.y);
        r6.xyz = r6.www * r0.xzw + r1.xyz;
        r6.w = rcp(r6.z);
        r7.x = r6.w + -r1.w;
        r7.x = cmp(r7.x >= 50);
        r5.w = r5.w ? r7.x : 0;
        r4.w = (int)r4.z | (int)r5.w;
        if (r4.w == 0) {
          r4.z = cmp(r1.w < r6.w);
          if (r4.z != 0) {
            r4.z = rcp(r1.w);
            r4.z = r4.z + -r1.z;
            r5.w = rcp(r0.w);
            r4.z = r5.w * r4.z;
            r6.xyz = r4.zzz * r0.xzw + r1.xyz;
            r5.z = -1 + r5.z;
            r2.zw = r2.zw + r2.zw;
            r7.xy = r6.xy * r2.zw;
            r5.xy = floor(r7.xy);
          } else {
            r4.z = cmp(r5.z != 4.000000);
            r7.xy = cmp(r7.zw >= float2(0,0));
            r7.zw = float2(0.5,0.5) * r2.zw;
            r7.zw = floor(r7.zw);
            r8.x = 1 + r5.z;
            r7.xy = r7.xy ? float2(0,0) : float2(-0.5,-0.5);
            r8.yz = r5.xy * float2(0.5,0.5) + r7.xy;
            r2.zw = r4.zz ? r7.zw : r2.zw;
            r5.z = 4;
            r5.xyz = r4.zzz ? r8.yzx : r5.xyz;
          }
        }
      }
      r3.y = 28;
    }
    r4.z = cmp(r5.z >= 1);
    r7.xy = cmp(float2(0,0) >= r6.xy);
    r7.zw = cmp(r6.xy >= float2(1,1));
    r5.w = (int)r7.z | (int)r7.x;
    r5.w = (int)r7.y | (int)r5.w;
    r5.w = (int)r7.w | (int)r5.w;
    r6.w = rcp(r6.z);
    r7.x = cmp(r6.w >= cb0[0].w);
    r5.w = (int)r5.w | (int)r7.x;
    r5.w = ~(int)r5.w;
    r4.z = r4.z ? r5.w : 0;
    r3.x = r3.x ? r4.z : 0;
    if (r3.x != 0) {
      r7.xyzw = (int4)r5.xyzz;
      r1.w = t0.Load(r7.xyz).x;
      r4.z = r6.w + -r1.w;
      r5.w = cmp(r5.z == 1.000000);
      r4.z = cmp(r4.z >= 50);
      r4.z = r4.z ? r5.w : 0;
      r4.z = (int)r4.z | (int)r4.w;
      r7.x = ~(int)r4.z;
      r6.w = cmp(r1.w < r6.w);
      r6.w = r6.w ? r7.x : 0;
      if (r6.w != 0) {
        r5.z = -1 + r5.z;
        r7.xy = float2(0.5,0.5) + r5.xy;
        r7.zw = rcp(r2.zw);
        r7.xy = -r7.xy * r7.zw + r6.xy;
        r7.xy = cmp(r7.xy >= float2(0,0));
        r7.xy = r7.xy ? float2(1,1) : 0;
        r5.xy = r5.xy * float2(2,2) + r7.xy;
        r2.zw = r2.zw + r2.zw;
        r4.w = 0;
      } else {
        r7.xy = r5.xy + r3.zw;
        r7.xy = r7.xy / r2.zw;
        r7.xy = r7.xy + -r1.xy;
        r7.xy = r7.xy / r0.xz;
        r6.w = cmp(r7.y >= r7.x);
        r8.y = r6.w ? 1.000000 : 0;
        r8.z = r6.w ? 0 : 1;
        r7.zw = r8.yz * r4.xy;
        r5.xy = r4.xy * r8.yz + r5.xy;
        r6.w = min(r7.x, r7.y);
        r6.xyz = r6.www * r0.xzw + r1.xyz;
        r6.w = rcp(r6.z);
        r7.x = r6.w + -r1.w;
        r7.x = cmp(r7.x >= 50);
        r5.w = r5.w ? r7.x : 0;
        r4.w = (int)r4.z | (int)r5.w;
        if (r4.w == 0) {
          r4.z = cmp(r1.w < r6.w);
          if (r4.z != 0) {
            r4.z = rcp(r1.w);
            r4.z = r4.z + -r1.z;
            r5.w = rcp(r0.w);
            r4.z = r5.w * r4.z;
            r6.xyz = r4.zzz * r0.xzw + r1.xyz;
            r5.z = -1 + r5.z;
            r2.zw = r2.zw + r2.zw;
            r7.xy = r6.xy * r2.zw;
            r5.xy = floor(r7.xy);
          } else {
            r4.z = cmp(r5.z != 4.000000);
            r7.xy = cmp(r7.zw >= float2(0,0));
            r7.zw = float2(0.5,0.5) * r2.zw;
            r7.zw = floor(r7.zw);
            r8.x = 1 + r5.z;
            r7.xy = r7.xy ? float2(0,0) : float2(-0.5,-0.5);
            r8.yz = r5.xy * float2(0.5,0.5) + r7.xy;
            r2.zw = r4.zz ? r7.zw : r2.zw;
            r5.z = 4;
            r5.xyz = r4.zzz ? r8.yzx : r5.xyz;
          }
        }
      }
      r3.y = 29;
    }
    r4.z = cmp(r5.z >= 1);
    r7.xy = cmp(float2(0,0) >= r6.xy);
    r7.zw = cmp(r6.xy >= float2(1,1));
    r5.w = (int)r7.z | (int)r7.x;
    r5.w = (int)r7.y | (int)r5.w;
    r5.w = (int)r7.w | (int)r5.w;
    r6.w = rcp(r6.z);
    r7.x = cmp(r6.w >= cb0[0].w);
    r5.w = (int)r5.w | (int)r7.x;
    r5.w = ~(int)r5.w;
    r4.z = r4.z ? r5.w : 0;
    r3.x = r3.x ? r4.z : 0;
    if (r3.x != 0) {
      r7.xyzw = (int4)r5.xyzz;
      r1.w = t0.Load(r7.xyz).x;
      r4.z = r6.w + -r1.w;
      r5.w = cmp(r5.z == 1.000000);
      r4.z = cmp(r4.z >= 50);
      r4.z = r4.z ? r5.w : 0;
      r4.z = (int)r4.z | (int)r4.w;
      r7.x = ~(int)r4.z;
      r6.w = cmp(r1.w < r6.w);
      r6.w = r6.w ? r7.x : 0;
      if (r6.w != 0) {
        r5.z = -1 + r5.z;
        r7.xy = float2(0.5,0.5) + r5.xy;
        r7.zw = rcp(r2.zw);
        r7.xy = -r7.xy * r7.zw + r6.xy;
        r7.xy = cmp(r7.xy >= float2(0,0));
        r7.xy = r7.xy ? float2(1,1) : 0;
        r5.xy = r5.xy * float2(2,2) + r7.xy;
        r2.zw = r2.zw + r2.zw;
        r4.w = 0;
      } else {
        r7.xy = r5.xy + r3.zw;
        r7.xy = r7.xy / r2.zw;
        r7.xy = r7.xy + -r1.xy;
        r7.xy = r7.xy / r0.xz;
        r6.w = cmp(r7.y >= r7.x);
        r8.y = r6.w ? 1.000000 : 0;
        r8.z = r6.w ? 0 : 1;
        r7.zw = r8.yz * r4.xy;
        r5.xy = r4.xy * r8.yz + r5.xy;
        r6.w = min(r7.x, r7.y);
        r6.xyz = r6.www * r0.xzw + r1.xyz;
        r6.w = rcp(r6.z);
        r7.x = r6.w + -r1.w;
        r7.x = cmp(r7.x >= 50);
        r5.w = r5.w ? r7.x : 0;
        r4.w = (int)r4.z | (int)r5.w;
        if (r4.w == 0) {
          r4.z = cmp(r1.w < r6.w);
          if (r4.z != 0) {
            r4.z = rcp(r1.w);
            r4.z = r4.z + -r1.z;
            r5.w = rcp(r0.w);
            r4.z = r5.w * r4.z;
            r6.xyz = r4.zzz * r0.xzw + r1.xyz;
            r5.z = -1 + r5.z;
            r2.zw = r2.zw + r2.zw;
            r7.xy = r6.xy * r2.zw;
            r5.xy = floor(r7.xy);
          } else {
            r4.z = cmp(r5.z != 4.000000);
            r7.xy = cmp(r7.zw >= float2(0,0));
            r7.zw = float2(0.5,0.5) * r2.zw;
            r7.zw = floor(r7.zw);
            r8.x = 1 + r5.z;
            r7.xy = r7.xy ? float2(0,0) : float2(-0.5,-0.5);
            r8.yz = r5.xy * float2(0.5,0.5) + r7.xy;
            r2.zw = r4.zz ? r7.zw : r2.zw;
            r5.z = 4;
            r5.xyz = r4.zzz ? r8.yzx : r5.xyz;
          }
        }
      }
      r3.y = 30;
    }
    r4.z = cmp(r5.z >= 1);
    r7.xy = cmp(float2(0,0) >= r6.xy);
    r7.zw = cmp(r6.xy >= float2(1,1));
    r5.w = (int)r7.z | (int)r7.x;
    r5.w = (int)r7.y | (int)r5.w;
    r5.w = (int)r7.w | (int)r5.w;
    r6.w = rcp(r6.z);
    r7.x = cmp(r6.w >= cb0[0].w);
    r5.w = (int)r5.w | (int)r7.x;
    r5.w = ~(int)r5.w;
    r4.z = r4.z ? r5.w : 0;
    r3.x = r3.x ? r4.z : 0;
    if (r3.x != 0) {
      r7.xyzw = (int4)r5.xyzz;
      r1.w = t0.Load(r7.xyz).x;
      r4.z = r6.w + -r1.w;
      r5.w = cmp(r5.z == 1.000000);
      r4.z = cmp(r4.z >= 50);
      r4.z = r4.z ? r5.w : 0;
      r4.z = (int)r4.z | (int)r4.w;
      r7.x = ~(int)r4.z;
      r6.w = cmp(r1.w < r6.w);
      r6.w = r6.w ? r7.x : 0;
      if (r6.w != 0) {
        r5.z = -1 + r5.z;
        r7.xy = float2(0.5,0.5) + r5.xy;
        r7.zw = rcp(r2.zw);
        r7.xy = -r7.xy * r7.zw + r6.xy;
        r7.xy = cmp(r7.xy >= float2(0,0));
        r7.xy = r7.xy ? float2(1,1) : 0;
        r5.xy = r5.xy * float2(2,2) + r7.xy;
        r2.zw = r2.zw + r2.zw;
        r4.w = 0;
      } else {
        r7.xy = r5.xy + r3.zw;
        r7.xy = r7.xy / r2.zw;
        r7.xy = r7.xy + -r1.xy;
        r7.xy = r7.xy / r0.xz;
        r6.w = cmp(r7.y >= r7.x);
        r8.y = r6.w ? 1.000000 : 0;
        r8.z = r6.w ? 0 : 1;
        r7.zw = r8.yz * r4.xy;
        r5.xy = r4.xy * r8.yz + r5.xy;
        r4.x = min(r7.x, r7.y);
        r6.xyz = r4.xxx * r0.xzw + r1.xyz;
        r4.x = rcp(r6.z);
        r4.y = r4.x + -r1.w;
        r4.y = cmp(r4.y >= 50);
        r4.y = r4.y ? r5.w : 0;
        r4.w = (int)r4.y | (int)r4.z;
        if (r4.w == 0) {
          r4.x = cmp(r1.w < r4.x);
          if (r4.x != 0) {
            r4.x = rcp(r1.w);
            r4.x = r4.x + -r1.z;
            r4.y = rcp(r0.w);
            r4.x = r4.x * r4.y;
            r6.xyz = r4.xxx * r0.xzw + r1.xyz;
            r5.z = -1 + r5.z;
            r2.zw = r2.zw + r2.zw;
            r4.xy = r6.xy * r2.zw;
            r5.xy = floor(r4.xy);
          } else {
            r4.x = cmp(r5.z != 4.000000);
            r4.yz = cmp(r7.zw >= float2(0,0));
            r7.xy = float2(0.5,0.5) * r2.zw;
            r7.xy = floor(r7.xy);
            r8.x = 1 + r5.z;
            r4.yz = r4.yz ? float2(0,0) : float2(-0.5,-0.5);
            r8.yz = r5.xy * float2(0.5,0.5) + r4.yz;
            r2.zw = r4.xx ? r7.xy : r2.zw;
            r5.z = 4;
            r5.xyz = r4.xxx ? r8.yzx : r5.xyz;
          }
        }
      }
      r3.y = 31;
    }
    r4.x = cmp(r5.z >= 1);
    r4.yz = cmp(float2(0,0) >= r6.xy);
    r7.xy = cmp(r6.xy >= float2(1,1));
    r4.y = (int)r4.y | (int)r7.x;
    r4.y = (int)r4.z | (int)r4.y;
    r4.y = (int)r7.y | (int)r4.y;
    r4.z = rcp(r6.z);
    r5.w = cmp(r4.z >= cb0[0].w);
    r4.y = (int)r4.y | (int)r5.w;
    r4.y = ~(int)r4.y;
    r4.x = r4.y ? r4.x : 0;
    r3.x = r3.x ? r4.x : 0;
    if (r3.x != 0) {
      r7.xyzw = (int4)r5.xyzz;
      r1.w = t0.Load(r7.xyz).x;
      r3.x = r4.z + -r1.w;
      r4.x = cmp(r5.z == 1.000000);
      r3.x = cmp(r3.x >= 50);
      r3.x = r3.x ? r4.x : 0;
      r3.x = (int)r3.x | (int)r4.w;
      r4.y = ~(int)r3.x;
      r4.z = cmp(r1.w < r4.z);
      r4.y = r4.z ? r4.y : 0;
      if (r4.y != 0) {
        r4.w = 0;
      } else {
        r3.zw = r5.xy + r3.zw;
        r2.zw = r3.zw / r2.zw;
        r2.zw = r2.zw + -r1.xy;
        r2.zw = r2.zw / r0.xz;
        r2.z = min(r2.z, r2.w);
        r6.xyz = r2.zzz * r0.xzw + r1.xyz;
        r2.z = rcp(r6.z);
        r2.w = r2.z + -r1.w;
        r2.w = cmp(r2.w >= 50);
        r2.w = r2.w ? r4.x : 0;
        r4.w = (int)r2.w | (int)r3.x;
        if (r4.w == 0) {
          r2.z = cmp(r1.w < r2.z);
          if (r2.z != 0) {
            r2.z = rcp(r1.w);
            r2.z = r2.z + -r1.z;
            r2.w = rcp(r0.w);
            r2.z = r2.z * r2.w;
            r6.xyz = r2.zzz * r0.xzw + r1.xyz;
          }
        }
      }
      r3.y = 32;
    }
    r0.xz = float2(-0.5,-0.5) + r6.xy;
    r0.xyz = 0.0; // Fix: Do not fade on screen edges
    r0.x = dot(r0.xz, r0.xz);
    r0.x = sqrt(r0.x);
    r0.x = r0.x + r0.x;
    r0.x = min(1, r0.x);
    r0.x = -r0.x * r0.x + 1;
    r0.zw = -r6.xy + r2.xy;
    r0.z = dot(r0.zw, r0.zw);
    r0.z = sqrt(r0.z);
    r0.z = r0.z * -2 + 1;
    r0.z = max(0, r0.z);
    r0.x = r0.x * r0.z;
    r0.z = cb0[0].w + -cb0[0].z;
    r0.z = rcp(r0.z);
    r0.z = -25 * r0.z;
    r0.y = r1.w + -r0.y;
    r0.y = saturate(r0.z * r0.y + 1);
    r0.x = r0.x * r0.y;
    r0.x = r0.x * r0.x;
    r0.yz = cmp(float2(0,0) >= r6.xy);
    r1.xy = cmp(r6.xy >= float2(1,1));
    r0.y = (int)r0.y | (int)r1.x;
    r0.y = (int)r0.z | (int)r0.y;
    r0.y = (int)r1.y | (int)r0.y;
    r0.z = rcp(r6.z);
    r0.z = cmp(r0.z >= cb0[0].w);
    r0.y = (int)r0.z | (int)r0.y;
    r0.z = cmp((int)r3.y == 32);
    r0.y = (int)r0.z | (int)r0.y;
    r0.y = (int)r4.w | (int)r0.y;
    if (r0.y == 0) {
      r0.yzw = t3.SampleLevel(s3_s, r6.xy, 0).xyz;
      o0.xyz = r0.yzw;
      o0.w = r0.x;
    } else {
      o0.xyzw = float4(0,0,0,0);
    }
  } else {
    o0.xyzw = float4(0,0,0,0);
  }
  return;
}