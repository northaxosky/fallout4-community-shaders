#ifndef VISIBILITY
#error "LensFlareVis requires VISIBILITY"
#endif

struct PS_INPUT
{
	noperspective float4 Position : SV_POSITION;
};

cbuffer LensFlareVisibilityParameters : register(b2)
{
	float4 Parameters[128];
};

Texture2D<float4> Image : register(t0);
SamplerState ImageSampler : register(s0);

float4 main(PS_INPUT input) : SV_Target
{
	uint index = (uint)input.Position.x;
	float count = 0;

	{
		float4 coordinate = Parameters[index].xyxy + float4(
			asfloat(0xbc9a5706), asfloat(0xbc02c3c3),
			asfloat(0x3c9aecc1), asfloat(0xbc7bf49d));
		float first = Image.Sample(ImageSampler, coordinate.xy).x;
		float second = Image.Sample(ImageSampler, coordinate.zw).x;
		count += first >= Parameters[index].z;
		count += second >= Parameters[index].z;
	}
	{
		float4 coordinate = Parameters[index].xyxy + float4(
			asfloat(0xbaf6e5e1), asfloat(0xbc984563),
			asfloat(0x3be21295), asfloat(0x3bc0987b));
		float first = Image.Sample(ImageSampler, coordinate.xy).x;
		float second = Image.Sample(ImageSampler, coordinate.zw).x;
		count += first >= Parameters[index].z;
		count += second >= Parameters[index].z;
	}
	{
		float4 coordinate = Parameters[index].xyxy + float4(
			asfloat(0xbc960f09), asfloat(0x3c15fbdc),
			asfloat(0xbc859a21), asfloat(0xbc900929));
		float first = Image.Sample(ImageSampler, coordinate.xy).x;
		float second = Image.Sample(ImageSampler, coordinate.zw).x;
		count += first >= Parameters[index].z;
		count += second >= Parameters[index].z;
	}
	{
		float4 coordinate = Parameters[index].xyxy + float4(
			asfloat(0xbbfadb10), asfloat(0x3bb5620a),
			asfloat(0x3c9fb7ec), asfloat(0x3c77e276));
		float first = Image.Sample(ImageSampler, coordinate.xy).x;
		float second = Image.Sample(ImageSampler, coordinate.zw).x;
		count += first >= Parameters[index].z;
		count += second >= Parameters[index].z;
	}
	{
		float4 coordinate = Parameters[index].xyxy + float4(
			asfloat(0x3c113d1a), asfloat(0xbc9fc34f),
			asfloat(0x3c301ae1), asfloat(0xbc1b3bb4));
		float first = Image.Sample(ImageSampler, coordinate.xy).x;
		float second = Image.Sample(ImageSampler, coordinate.zw).x;
		count += first >= Parameters[index].z;
		count += second >= Parameters[index].z;
	}
	{
		float4 coordinate = Parameters[index].xyxy + float4(
			asfloat(0xbbada671), asfloat(0xbc09466a),
			asfloat(0x3c81c1d8), asfloat(0x3b7a380b));
		float first = Image.Sample(ImageSampler, coordinate.xy).x;
		float second = Image.Sample(ImageSampler, coordinate.zw).x;
		count += first >= Parameters[index].z;
		count += second >= Parameters[index].z;
	}
	{
		float4 coordinate = Parameters[index].xyxy + float4(
			asfloat(0xbb9e8624), asfloat(0x3ca35bf1),
			asfloat(0xbc8561cf), asfloat(0x3c95cfb4));
		float first = Image.Sample(ImageSampler, coordinate.xy).x;
		float second = Image.Sample(ImageSampler, coordinate.zw).x;
		count += first >= Parameters[index].z;
		count += second >= Parameters[index].z;
	}
	{
		float4 coordinate = Parameters[index].xyxy + float4(
			asfloat(0x3b82f7cd), asfloat(0x3c80d894),
			asfloat(0x3b3c85e1), asfloat(0xbb38d266));
		float first = Image.Sample(ImageSampler, coordinate.xy).x;
		float second = Image.Sample(ImageSampler, coordinate.zw).x;
		count += first >= Parameters[index].z;
		count += second >= Parameters[index].z;
	}

	return count * 0.0625;
}
