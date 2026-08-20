void AppendBsdfFamilyShaderReplacementVariants(
	std::vector<ShaderReplacementVariantRegistration>& a_variants)
{
	using Target = ShaderInjectionTarget;
	a_variants.reserve(a_variants.size() + 241);
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfComposite,
		"bsdfcomposite_ps3401",
		{},
		"c89cf7676d1258cc746565db58d0854b8742126e",
		{
			{ "BSDFCOMPOSITE_PS_CUBE_IBL", "1" },
			{ "COMPOSITE_CB2_COUNT", "3" },
			{ "COMPOSITE_MODULATION", "0" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfComposite,
		"bsdfcomposite_ps3402",
		{},
		"3c1355737e77d36cdbc37d6b76015b8eb2a15b53",
		{
			{ "BSDFCOMPOSITE_PS_2D_FOG", "1" },
			{ "COMPOSITE_ALPHA_ONE", "1" },
			{ "COMPOSITE_CB2_COUNT", "3" },
			{ "COMPOSITE_HAS_LIGHT", "1" },
			{ "COMPOSITE_HAS_TYPE", "1" },
			{ "COMPOSITE_MATERIAL_EXCLUSION", "1" },
			{ "COMPOSITE_SCENE_BLEND", "1" },
			{ "COMPOSITE_TILE_AMBIENT", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfComposite,
		"bsdfcomposite_ps3403",
		{},
		"e877e50839ee388ed88dcf674aaed8087628b88e",
		{
			{ "BSDFCOMPOSITE_PS_CUBE_IBL", "1" },
			{ "COMPOSITE_ALPHA_ONE", "1" },
			{ "COMPOSITE_CB2_COUNT", "3" },
			{ "COMPOSITE_MODULATION", "0" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfComposite,
		"bsdfcomposite_ps3404",
		{},
		"448668db96a9677c730d604f00363350cc7f9d59",
		{
			{ "BSDFCOMPOSITE_PS_CUBE_IBL", "1" },
			{ "COMPOSITE_CB12_COUNT", "31" },
			{ "COMPOSITE_CB2_COUNT", "1" },
			{ "COMPOSITE_FOG_STACK", "0" },
			{ "COMPOSITE_MATERIAL_EXCLUSION", "0" },
			{ "COMPOSITE_MODULATION", "0" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfComposite,
		"bsdfcomposite_ps3405",
		{},
		"4a002a759b10d9cd46789db96346a5ed689ef676",
		{
			{ "BSDFCOMPOSITE_PS_2D_FOG", "1" },
			{ "COMPOSITE_ALPHA_ONE", "1" },
			{ "COMPOSITE_CB2_COUNT", "3" },
			{ "COMPOSITE_HAS_LIGHT", "1" },
			{ "COMPOSITE_HAS_TYPE", "1" },
			{ "COMPOSITE_MATERIAL_EXCLUSION", "1" },
			{ "COMPOSITE_SCENE_BLEND", "1" },
			{ "COMPOSITE_TILE_AMBIENT", "1" },
			{ "TILED_LIGHTS", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfComposite,
		"bsdfcomposite_ps3406",
		{},
		"7e5c2241c8f374d4ec01a75425d98f31a0db3814",
		{
			{ "BSDFCOMPOSITE_PS_CUBE_IBL", "1" },
			{ "COMPOSITE_ALPHA_ONE", "1" },
			{ "COMPOSITE_CB2_COUNT", "3" },
			{ "COMPOSITE_MODULATION", "0" },
			{ "TILED_LIGHTS", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfComposite,
		"bsdfcomposite_ps3412",
		{},
		"900f62348ec133fd6384624e68fffb28b3939117",
		{
			{ "BSDFCOMPOSITE_PS_2D_ACCUMULATOR", "1" },
			{ "COMPOSITE_CB2_COUNT", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfComposite,
		"bsdfcomposite_ps3414",
		{},
		"177d9b5d6abe37bf28aceed02d17f93dd4c355b0",
		{
			{ "BSDFCOMPOSITE_PS_CUBE_IBL", "1" },
			{ "COMPOSITE_CB12_COUNT", "31" },
			{ "COMPOSITE_CB2_COUNT", "1" },
			{ "COMPOSITE_FOG_STACK", "0" },
			{ "COMPOSITE_MATERIAL_EXCLUSION", "0" },
			{ "COMPOSITE_MODULATION", "0" },
			{ "COMPOSITE_UNUSED_TEXCOORD", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfComposite,
		"bsdfcomposite_ps3416",
		{},
		"e87d2c4403d4fa5f6a995f10b151f0e1c582125a",
		{
			{ "BSDFCOMPOSITE_PS_2D_FOG", "1" },
			{ "COMPOSITE_CB2_COUNT", "3" },
			{ "COMPOSITE_HAS_LIGHT", "1" },
			{ "COMPOSITE_HAS_TYPE", "1" },
			{ "COMPOSITE_MATERIAL_EXCLUSION", "1" },
			{ "COMPOSITE_SCENE_BLEND", "1" },
			{ "COMPOSITE_TILE_AMBIENT", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfComposite,
		"bsdfcomposite_ps3418",
		{},
		"260a8c3f7ea20eccdd55a9d9784b599f7f758e8c",
		{
			{ "BSDFCOMPOSITE_PS_2D_ACCUMULATOR", "1" },
			{ "COMPOSITE_CB2_COUNT", "1" },
			{ "COMPOSITE_MATERIAL_5", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfComposite,
		"bsdfcomposite_ps3419",
		{},
		"d5fd792b8784e49e8dbe0a6252925d941c489d90",
		{
			{ "BSDFCOMPOSITE_PS_2D_FOG", "1" },
			{ "COMPOSITE_CB2_COUNT", "3" },
			{ "COMPOSITE_HAS_LIGHT", "1" },
			{ "COMPOSITE_HAS_TYPE", "1" },
			{ "COMPOSITE_MATERIAL_5", "1" },
			{ "COMPOSITE_MATERIAL_EXCLUSION", "1" },
			{ "COMPOSITE_SCENE_BLEND", "1" },
			{ "COMPOSITE_TILE_AMBIENT", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfComposite,
		"bsdfcomposite_ps3420",
		{},
		"a41a0a5eccee70a001d208804535094b75a83600",
		{
			{ "BSDFCOMPOSITE_PS_2D_ACCUMULATOR", "1" },
			{ "COMPOSITE_CB2_COUNT", "6" },
			{ "COMPOSITE_MODULATION", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfComposite,
		"bsdfcomposite_ps3421",
		{},
		"62c99bb7e5e697f0f727b88082f36417c05b6815",
		{
			{ "BSDFCOMPOSITE_PS_CUBE_IBL", "1" },
			{ "COMPOSITE_CB12_COUNT", "31" },
			{ "COMPOSITE_FOG_STACK", "0" },
			{ "COMPOSITE_MATERIAL_EXCLUSION", "0" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfComposite,
		"bsdfcomposite_ps3422",
		{},
		"ed85e813230faa1cf2f3f0706c150250a68a4acc",
		{
			{ "BSDFCOMPOSITE_PS_2D_FOG", "1" },
			{ "COMPOSITE_CB2_COUNT", "6" },
			{ "COMPOSITE_HAS_LIGHT", "1" },
			{ "COMPOSITE_HAS_TYPE", "1" },
			{ "COMPOSITE_MATERIAL_EXCLUSION", "1" },
			{ "COMPOSITE_MODULATION", "1" },
			{ "COMPOSITE_SCENE_BLEND", "1" },
			{ "COMPOSITE_TILE_AMBIENT", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfComposite,
		"bsdfcomposite_ps3423",
		{},
		"7cdee7bed35fe8fb7ea308d5b35490b3eb0870b5",
		{
			{ "BSDFCOMPOSITE_PS_CUBE_IBL", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfComposite,
		"bsdfcomposite_ps3424",
		{},
		"94c8634be6709fdba723d668852a2deadfa48ddd",
		{
			{ "BSDFCOMPOSITE_PS_NO_SRV_POSITION", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfComposite,
		"bsdfcomposite_ps3425",
		{},
		"5c4bf49dced74855109669b344bfeb208ff4b2b4",
		{
			{ "BSDFCOMPOSITE_PS_NO_SRV_POSITION_TEXCOORD", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfComposite,
		"bsdfcomposite_ps3427",
		{},
		"612bd572ea516f35a0809cbd3e9f286f20435ecc",
		{
			{ "BSDFCOMPOSITE_PS_AMBIENT_IBL_CB31_FAMILY", "1" },
			{ "AMBIENT_DIFFUSE_SET_B", "0" },
			{ "AMBIENT_SSAO", "0" },
			{ "AMBIENT_SUBSURFACE_BLUR", "0" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfComposite,
		"bsdfcomposite_ps3428",
		{},
		"2fdc647abbc86064601cff2bee9e8ace354295ac",
		{
			{ "BSDFCOMPOSITE_PS_AMBIENT_IBL_CB31_FAMILY", "1" },
			{ "AMBIENT_DIFFUSE_SET_B", "0" },
			{ "AMBIENT_SSAO", "0" },
			{ "AMBIENT_SUBSURFACE_BLUR", "0" },
			{ "AMBIENT_UNUSED_TEXCOORD", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfComposite,
		"bsdfcomposite_ps3434",
		{},
		"1f3e25fe78b759d1278ca3608c0f0d30eb46434d",
		{
			{ "BSDFCOMPOSITE_PS_NO_T0_ACCUMULATOR", "1" },
			{ "WAVE5A_ACCUMULATOR_SHAPE", "2" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfComposite,
		"bsdfcomposite_ps3439",
		{},
		"410b2ce030c171b16306c891d57f669a33806652",
		{
			{ "BSDFCOMPOSITE_PS_NO_T0_FOG", "1" },
			{ "WAVE5A_FOG_SHAPE", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfComposite,
		"bsdfcomposite_ps3440",
		{},
		"b3730e0e88879ddc09161398b32841492205eba8",
		{
			{ "BSDFCOMPOSITE_PS_AMBIENT_IBL_MINIMAL_FAMILY", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfComposite,
		"bsdfcomposite_ps3442",
		{},
		"6112e6a7e39c67df24a1c42e45b8247780f1b6f9",
		{
			{ "BSDFCOMPOSITE_PS_NO_T0_FOG", "1" },
			{ "WAVE5A_FOG_SHAPE", "2" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfComposite,
		"bsdfcomposite_ps3443",
		{},
		"1a6e42e6f2cbd101536af7dd12df80e01492eadd",
		{
			{ "BSDFCOMPOSITE_PS_NO_T0_ACCUMULATOR", "1" },
			{ "WAVE5A_ACCUMULATOR_SHAPE", "4" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfComposite,
		"bsdfcomposite_ps3444",
		{},
		"ceda4c15480eddc1ac629710384df204b0358cfc",
		{
			{ "BSDFCOMPOSITE_PS_NO_T0_FOG", "1" },
			{ "WAVE5A_FOG_SHAPE", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfComposite,
		"bsdfcomposite_ps3445",
		{},
		"fbdcf44d5190e73ee43bb39576f89a6ea05581cf",
		{
			{ "BSDFCOMPOSITE_PS_AMBIENT_IBL_MINIMAL_FAMILY", "1" },
			{ "OUTPUTMASK", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfComposite,
		"bsdfcomposite_ps3448",
		{},
		"19092d8fd082f11292cb166cb0c5661138701eff",
		{
			{ "BSDFCOMPOSITE_PS_2D_ACCUMULATOR", "1" },
			{ "COMPOSITE_CB2_COUNT", "1" },
			{ "TILED_LIGHTS", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfComposite,
		"bsdfcomposite_ps3449",
		{},
		"c4bfb3394e50918481086a827e5bca07d7085f73",
		{
			{ "BSDFCOMPOSITE_PS_CUBE_IBL", "1" },
			{ "COMPOSITE_CB12_COUNT", "31" },
			{ "COMPOSITE_CB2_COUNT", "1" },
			{ "COMPOSITE_FOG_STACK", "0" },
			{ "COMPOSITE_MATERIAL_EXCLUSION", "0" },
			{ "COMPOSITE_MODULATION", "0" },
			{ "TILED_LIGHTS", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfComposite,
		"bsdfcomposite_ps3450",
		{},
		"95b8547b41fe9f7d83e7d5e314369f88245f4a25",
		{
			{ "BSDFCOMPOSITE_PS_CUBE_IBL", "1" },
			{ "COMPOSITE_CB12_COUNT", "31" },
			{ "COMPOSITE_CB2_COUNT", "1" },
			{ "COMPOSITE_FOG_STACK", "0" },
			{ "COMPOSITE_MATERIAL_EXCLUSION", "0" },
			{ "COMPOSITE_MODULATION", "0" },
			{ "COMPOSITE_UNUSED_TEXCOORD", "1" },
			{ "TILED_LIGHTS", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfComposite,
		"bsdfcomposite_ps3452",
		{},
		"56a8e3aee5e66287ab9d793052e839ea919c85a1",
		{
			{ "BSDFCOMPOSITE_PS_2D_FOG", "1" },
			{ "COMPOSITE_CB2_COUNT", "3" },
			{ "COMPOSITE_HAS_LIGHT", "1" },
			{ "COMPOSITE_HAS_TYPE", "1" },
			{ "COMPOSITE_MATERIAL_EXCLUSION", "1" },
			{ "COMPOSITE_SCENE_BLEND", "1" },
			{ "COMPOSITE_TILE_AMBIENT", "1" },
			{ "TILED_LIGHTS", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfComposite,
		"bsdfcomposite_ps3453",
		{},
		"7008b83e40f8eecae03722e93682bf5ffbf09f3b",
		{
			{ "BSDFCOMPOSITE_PS_CUBE_IBL", "1" },
			{ "COMPOSITE_CB2_COUNT", "3" },
			{ "COMPOSITE_MODULATION", "0" },
			{ "TILED_LIGHTS", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfComposite,
		"bsdfcomposite_ps3454",
		{},
		"fa2b30bf9531a821ab04d464de7e2c053b221677",
		{
			{ "BSDFCOMPOSITE_PS_2D_ACCUMULATOR", "1" },
			{ "COMPOSITE_CB2_COUNT", "1" },
			{ "COMPOSITE_MATERIAL_5", "1" },
			{ "TILED_LIGHTS", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfComposite,
		"bsdfcomposite_ps3455",
		{},
		"eb5a28317f427b5796e9adcd670a2a6202d55658",
		{
			{ "BSDFCOMPOSITE_PS_2D_FOG", "1" },
			{ "COMPOSITE_CB2_COUNT", "3" },
			{ "COMPOSITE_HAS_LIGHT", "1" },
			{ "COMPOSITE_HAS_TYPE", "1" },
			{ "COMPOSITE_MATERIAL_5", "1" },
			{ "COMPOSITE_MATERIAL_EXCLUSION", "1" },
			{ "COMPOSITE_SCENE_BLEND", "1" },
			{ "COMPOSITE_TILE_AMBIENT", "1" },
			{ "TILED_LIGHTS", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfComposite,
		"bsdfcomposite_ps3456",
		{},
		"88bdc88ec22f75ca40e3464febe330aeb6d2d001",
		{
			{ "BSDFCOMPOSITE_PS_2D_ACCUMULATOR", "1" },
			{ "COMPOSITE_CB2_COUNT", "6" },
			{ "COMPOSITE_MODULATION", "1" },
			{ "TILED_LIGHTS", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfComposite,
		"bsdfcomposite_ps3457",
		{},
		"56ac4e077a36b220a39aae1cf8229ab281b7b7b1",
		{
			{ "BSDFCOMPOSITE_PS_CUBE_IBL", "1" },
			{ "COMPOSITE_CB12_COUNT", "31" },
			{ "COMPOSITE_FOG_STACK", "0" },
			{ "COMPOSITE_MATERIAL_EXCLUSION", "0" },
			{ "TILED_LIGHTS", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfComposite,
		"bsdfcomposite_ps3458",
		{},
		"a8aa0b979cbb0e59d948c01e5707ffebbd612aa4",
		{
			{ "BSDFCOMPOSITE_PS_2D_FOG", "1" },
			{ "COMPOSITE_CB2_COUNT", "6" },
			{ "COMPOSITE_HAS_LIGHT", "1" },
			{ "COMPOSITE_HAS_TYPE", "1" },
			{ "COMPOSITE_MATERIAL_EXCLUSION", "1" },
			{ "COMPOSITE_MODULATION", "1" },
			{ "COMPOSITE_SCENE_BLEND", "1" },
			{ "COMPOSITE_TILE_AMBIENT", "1" },
			{ "TILED_LIGHTS", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfComposite,
		"bsdfcomposite_ps3459",
		{},
		"0dad380a8395611f62e9aa5c30d7082462ef366a",
		{
			{ "BSDFCOMPOSITE_PS_CUBE_IBL", "1" },
			{ "TILED_LIGHTS", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfComposite,
		"bsdfcomposite_ps3476",
		{},
		"b77db624fe08bc9167e30f3888f99d212fab2882",
		{
			{ "BSDFCOMPOSITE_PS_NO_T0_ACCUMULATOR", "1" },
			{ "WAVE5A_ACCUMULATOR_SHAPE", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfComposite,
		"bsdfcomposite_ps3477",
		{},
		"569116875ea779752e2a5267a6dac08b360d9b4c",
		{
			{ "BSDFCOMPOSITE_PS_AMBIENT_IBL_CB47_FAMILY", "1" },
			{ "FO4_AMBIENT_OCCLUSION", "0" },
			{ "FO4_SKIN_BLUR", "0" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfComposite,
		"bsdfcomposite_ps3481",
		{},
		"b9859418f05f0bc5f1e6b0aaf1c11efb0b4c802f",
		{
			{ "BSDFCOMPOSITE_PS_AMBIENT_IBL_CB31_FAMILY", "1" },
			{ "AMBIENT_DIFFUSE_SET_B", "0" },
			{ "AMBIENT_SSAO", "0" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfComposite,
		"bsdfcomposite_ps3482",
		{},
		"10c81a631d4bcd8a0f599b2700e3c58492b67764",
		{
			{ "BSDFCOMPOSITE_PS_AMBIENT_IBL_CB47_FAMILY", "1" },
			{ "FO4_AMBIENT_OCCLUSION", "0" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfComposite,
		"bsdfcomposite_ps3483",
		{},
		"e31b3e0a4237b0af7b486322d892b76c48f5f3e5",
		{
			{ "BSDFCOMPOSITE_PS_AMBIENT_IBL_CB31_FAMILY", "1" },
			{ "AMBIENT_DIFFUSE_SET_B", "0" },
			{ "AMBIENT_SUBSURFACE_BLUR", "0" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfComposite,
		"bsdfcomposite_ps3490",
		{},
		"d765ed1e754d8bcc4766cfb97e55d4654b813754",
		{
			{ "BSDFCOMPOSITE_PS_AMBIENT_IBL_CB47_FAMILY", "1" },
			{ "FO4_SKIN_BLUR", "0" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfComposite,
		"bsdfcomposite_ps3493",
		{},
		"4154d2a108bc009e6e80b5fb7cc433e5c9552ae1",
		{
			{ "BSDFCOMPOSITE_PS_AMBIENT_IBL_CB31_FAMILY", "1" },
			{ "AMBIENT_DIFFUSE_SET_B", "0" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfComposite,
		"bsdfcomposite_ps3494",
		{},
		"8fbcce648ee85cb46f9f62a4c18c2ccbb0787d42",
		{
			{ "BSDFCOMPOSITE_PS_AMBIENT_IBL_COMPACT_FAMILY", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfComposite,
		"bsdfcomposite_ps3495",
		{},
		"6d726d0fe6b6c474da30edbffcecfa067c795873",
		{
			{ "BSDFCOMPOSITE_PS_AMBIENT_IBL_CB47_FAMILY", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfComposite,
		"bsdfcomposite_ps3499",
		{},
		"c81e02577d1beda28d31ee7e93790ea96c7f4529",
		{
			{ "BSDFCOMPOSITE_PS_2D_FOG", "1" },
			{ "COMPOSITE_CB2_COUNT", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfComposite,
		"bsdfcomposite_ps3501",
		{},
		"6870ab131fb3c2c42903c0b1255c173c59fd4cf3",
		{
			{ "BSDFCOMPOSITE_PS_AMBIENT_IBL_CB31_FAMILY", "1" },
			{ "AMBIENT_SSAO", "0" },
			{ "AMBIENT_SUBSURFACE_BLUR", "0" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfComposite,
		"bsdfcomposite_ps3505",
		{},
		"3c45963927e7275be549fce65eb9d70ce360cf40",
		{
			{ "BSDFCOMPOSITE_PS_AMBIENT_IBL_COMPACT_FAMILY", "1" },
			{ "FOGSTACK", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfComposite,
		"bsdfcomposite_ps3522",
		{},
		"66012006862ae5dacad707c95c3b23477c3a84d8",
		{
			{ "BSDFCOMPOSITE_PS_AMBIENT_IBL_CB31_FAMILY", "1" },
			{ "AMBIENT_SSAO", "0" },
			{ "AMBIENT_SUBSURFACE_BLUR", "0" },
			{ "AMBIENT_UNUSED_TEXCOORD", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfComposite,
		"bsdfcomposite_ps3532",
		{},
		"b9740350ab31a835261751e9b4d73d5c679e2427",
		{
			{ "BSDFCOMPOSITE_PS_2D_FOG", "1" },
			{ "COMPOSITE_CB2_COUNT", "3" },
			{ "COMPOSITE_HAS_TYPE", "1" },
			{ "COMPOSITE_MATERIAL_EXCLUSION", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfComposite,
		"bsdfcomposite_ps3539",
		{},
		"861504f6dcbe6214ff41ae550fe7adc3078e6a8c",
		{
			{ "BSDFCOMPOSITE_PS_2D_FOG", "1" },
			{ "COMPOSITE_CB2_COUNT", "3" },
			{ "COMPOSITE_HAS_TYPE", "1" },
			{ "COMPOSITE_MATERIAL_EXCLUSION", "1" },
			{ "TILED_LIGHTS", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfComposite,
		"bsdfcomposite_ps3544",
		{},
		"79ea987724e3781070cc712668a655968684aa00",
		{
			{ "BSDFCOMPOSITE_PS_AMBIENT_IBL_COMPACT_FAMILY", "1" },
			{ "TILELIGHT", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfComposite,
		"bsdfcomposite_ps3545",
		{},
		"39637a98310ce6a865d2c4209f3ae3b76550c777",
		{
			{ "BSDFCOMPOSITE_PS_AMBIENT_IBL_COMPACT_FAMILY", "1" },
			{ "FOGSTACK", "1" },
			{ "TILELIGHT", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfComposite,
		"bsdfcomposite_ps3551",
		{},
		"3cf8ea09b2980f9c20ddfe93141290bc6894c3c1",
		{
			{ "BSDFCOMPOSITE_PS_AMBIENT_IBL_COMPACT_FAMILY", "1" },
			{ "OUTPUTMASK", "1" },
			{ "TILELIGHT", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfComposite,
		"bsdfcomposite_ps3552",
		{},
		"da65c284045c202d26675830d14548d9f104e2a7",
		{
			{ "BSDFCOMPOSITE_PS_AMBIENT_IBL_COMPACT_FAMILY", "1" },
			{ "FOGSTACK", "1" },
			{ "OUTPUTMASK", "1" },
			{ "TILELIGHT", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfComposite,
		"bsdfcomposite_ps3554",
		{},
		"5c2c8f2867634d092f23b1b23e86ddb31965c639",
		{
			{ "BSDFCOMPOSITE_PS_AMBIENT_IBL_CB47_FAMILY", "1" },
			{ "FO4_AMBIENT_OCCLUSION", "0" },
			{ "FO4_SKIN_BLUR", "0" },
			{ "TILELIGHT", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfComposite,
		"bsdfcomposite_ps3555",
		{},
		"4cfb1778be6a96f3213bbf02b7595b6a02aa7674",
		{
			{ "BSDFCOMPOSITE_PS_AMBIENT_IBL_CB31_FAMILY", "1" },
			{ "AMBIENT_SSAO", "0" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfComposite,
		"bsdfcomposite_ps3556",
		{},
		"69affe3872ed614604f72220ae21a71852bb96ec",
		{
			{ "BSDFCOMPOSITE_PS_AMBIENT_IBL_CB47_FAMILY", "1" },
			{ "FO4_AMBIENT_OCCLUSION", "0" },
			{ "TILELIGHT", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfComposite,
		"bsdfcomposite_ps3557",
		{},
		"a6225a984b9900db6726a2b44795249bf49c1e29",
		{
			{ "BSDFCOMPOSITE_PS_AMBIENT_IBL_CB31_FAMILY", "1" },
			{ "AMBIENT_SUBSURFACE_BLUR", "0" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfComposite,
		"bsdfcomposite_ps3558",
		{},
		"ff9c774d9bbe2e77ced5a0f41ec7a6f0d618ae4f",
		{
			{ "BSDFCOMPOSITE_PS_AMBIENT_IBL_CB47_FAMILY", "1" },
			{ "FO4_SKIN_BLUR", "0" },
			{ "TILELIGHT", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfComposite,
		"bsdfcomposite_ps3559",
		{},
		"7460585eaf763b27ea14ee7bf607c9dfc3837a55",
		{
			{ "BSDFCOMPOSITE_PS_AMBIENT_IBL_CB31_FAMILY", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfComposite,
		"bsdfcomposite_ps3560",
		{},
		"2b6e36c08aca7ff0a3bd10da326e00b3b0367383",
		{
			{ "BSDFCOMPOSITE_PS_AMBIENT_IBL_CB47_FAMILY", "1" },
			{ "TILELIGHT", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfComposite,
		"bsdfcomposite_ps3568",
		{},
		"97335e28d575dcfbad08d1690efb7d5abedf8597",
		{
			{ "BSDFCOMPOSITE_PS_AMBIENT_IBL_COMPACT_FAMILY", "1" },
			{ "OUTPUTMASK", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfComposite,
		"bsdfcomposite_ps3569",
		{},
		"212cc8d4ca118d90826fcc1b5a20d6005fc2a122",
		{
			{ "BSDFCOMPOSITE_PS_NO_T0_FOG", "1" },
			{ "WAVE5A_FOG_SHAPE", "4" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfComposite,
		"bsdfcomposite_ps3571",
		{},
		"4887d3c62889e699a3daadc483844e9522d697c9",
		{
			{ "BSDFCOMPOSITE_PS_NO_T0_FOG", "1" },
			{ "WAVE5A_FOG_SHAPE", "5" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfComposite,
		"bsdfcomposite_ps3572",
		{},
		"df89353bebcfa292277146c1dad752ba3309426e",
		{
			{ "BSDFCOMPOSITE_PS_NO_T0_ACCUMULATOR", "1" },
			{ "WAVE5A_ACCUMULATOR_SHAPE", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfComposite,
		"bsdfcomposite_ps3573",
		{},
		"c6fed40e6fe7054140a6dce5d25ad49f2a4ddd3c",
		{
			{ "BSDFCOMPOSITE_PS_NO_T0_FOG", "1" },
			{ "WAVE5A_FOG_SHAPE", "6" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfComposite,
		"bsdfcomposite_ps3575",
		{},
		"ad139695d8e1874eccc1a9f539dcc070b3357549",
		{
			{ "BSDFCOMPOSITE_PS_AMBIENT_IBL_COMPACT_FAMILY", "1" },
			{ "FOGSTACK", "1" },
			{ "OUTPUTMASK", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfComposite,
		"bsdfcomposite_ps3579",
		{},
		"d74a32c9a7be4bccd45b58da69a6b2eff5225b31",
		{
			{ "BSDFCOMPOSITE_PS_2D_FOG", "1" },
			{ "COMPOSITE_CB2_COUNT", "3" },
			{ "TILED_LIGHTS", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfComposite,
		"bsdfcomposite_vs3378",
		{},
		"b311c1f25593cfac39b6ec55f4e694ce6d88d34e",
		{
			{ "BSDFCOMPOSITE_VS", "1" }
		},
		ShaderStage::kVertex));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfComposite,
		"bsdfcomposite_vs3379",
		{},
		"65a3ca08add4b7c7654e14687f421c1265c94173",
		{
			{ "BSDFCOMPOSITE_VS", "1" },
			{ "GEOMETRY", "1" }
		},
		ShaderStage::kVertex));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfComposite,
		"bsdfcomposite_vs3380",
		{},
		"fa6dc5623d337701ee618af230c55f762f6511ee",
		{
			{ "BSDFCOMPOSITE_VS", "1" },
			{ "DECAL", "1" }
		},
		ShaderStage::kVertex));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfComposite,
		"bsdfcomposite_vs3383",
		{},
		"95792bed248c9292f30666fe494fd92054f19d78",
		{
			{ "BSDFCOMPOSITE_VS", "1" },
			{ "TEXTURE", "1" },
			{ "GEOMETRY", "1" }
		},
		ShaderStage::kVertex));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3072",
		{},
		"a9435eca0f0be238c3660cd8a7cac1340247bf8b",
		{
			{ "BSDFLIGHT_PS_UNSHADOWED", "1" },
			{ "DIRECTIONAL", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3073",
		{},
		"ed0dd942f9cb6b227cff74ca15503572b7577bfb",
		{
			{ "BSDFLIGHT_PS_DEFERRED", "1" },
			{ "SPOT", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3074",
		{},
		"ffb33088fb5bb1dc211e1c52c5ee855930fd50a1",
		{
			{ "BSDFLIGHT_PS_DEFERRED", "1" },
			{ "POINTOMNI", "1" },
			{ "SHADOW", "1" },
			{ "FILTER_PCF9", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3075",
		{},
		"c55f6f0d777e6f6a55d5a359eb4b370da2ff6a6a",
		{
			{ "BSDFLIGHT_PS_DEFERRED", "1" },
			{ "POINTOMNI", "1" },
			{ "SHADOW", "1" },
			{ "HALFOMNI", "1" },
			{ "FILTER_PCF9", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3076",
		{},
		"d63b2b1807b386557d5244474dfd6fdfa51f2f10",
		{
			{ "BSDFLIGHT_PS_DEFERRED", "1" },
			{ "POINTSPOT", "1" },
			{ "SHADOW", "1" },
			{ "FILTER_PCF9", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3077",
		{},
		"6816c3885f7e99ee072ec107a52a49a35780731b",
		{
			{ "BSDFLIGHT_PS_DIRSPLITS2", "1" },
			{ "DIRECTIONAL", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "FILTER_PCF9", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3078",
		{},
		"2fe442f15c3bebd51f17d6bdfdc2e2524376af7e",
		{
			{ "BSDFLIGHT_PS_DEFERRED", "1" },
			{ "POINTOMNI", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "FILTER_PCF9", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3079",
		{},
		"fa90ccc0141d32ec70dd823d032a437a41c7108f",
		{
			{ "BSDFLIGHT_PS_DEFERRED", "1" },
			{ "POINTOMNI", "1" },
			{ "SHADOW", "1" },
			{ "HALFOMNI", "1" },
			{ "SPECULAR", "1" },
			{ "FILTER_PCF9", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3080",
		{},
		"d5351ff2cf2158fa8a3e53babf3779b7321d1ce4",
		{
			{ "BSDFLIGHT_PS_DEFERRED", "1" },
			{ "POINTSPOT", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "FILTER_PCF9", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3082",
		{},
		"bbbb5ddde11db5d418dd21724ba1f3d152cf73df",
		{
			{ "BSDFLIGHT_PS_DEFERRED", "1" },
			{ "POINTSPOT", "1" },
			{ "SHADOW", "1" },
			{ "ATTENUATION_ONLY", "1" },
			{ "FILTER_PCF9", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3083",
		{},
		"8eb91cb7565e5dee63226ed0ba7f98579d2b4570",
		{
			{ "BSDFLIGHT_PS_DEFERRED", "1" },
			{ "POINTOMNI", "1" },
			{ "SHADOW", "1" },
			{ "GOBOPROJECTION", "1" },
			{ "FILTER_PCF9", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3084",
		{},
		"f6e4aed9485cea7e27ae29c5160eceeebbc2d141",
		{
			{ "BSDFLIGHT_PS_DEFERRED", "1" },
			{ "POINTSPOT", "1" },
			{ "SHADOW", "1" },
			{ "GOBOPROJECTION", "1" },
			{ "FILTER_PCF9", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3085",
		{},
		"9ab3d9cfeaf9f1cfffccd182aa26d3eb5f389461",
		{
			{ "BSDFLIGHT_PS_DEFERRED", "1" },
			{ "POINTOMNI", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "GOBOPROJECTION", "1" },
			{ "FILTER_PCF9", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3086",
		{},
		"ffbeb95eea97c2c7e4ff05bba255494cb702a85b",
		{
			{ "BSDFLIGHT_PS_DEFERRED", "1" },
			{ "POINTOMNI", "1" },
			{ "SHADOW", "1" },
			{ "HALFOMNI", "1" },
			{ "SPECULAR", "1" },
			{ "GOBOPROJECTION", "1" },
			{ "FILTER_PCF9", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3087",
		{},
		"180372319d5ff04806dc727c83e77a1ca737b75e",
		{
			{ "BSDFLIGHT_PS_DEFERRED", "1" },
			{ "POINTSPOT", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "GOBOPROJECTION", "1" },
			{ "FILTER_PCF9", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3088",
		{},
		"2018f7d9fd227bb90520585e7c2f8d451f135738",
		{
			{ "BSDFLIGHT_PS_DEFERRED", "1" },
			{ "POINTOMNI", "1" },
			{ "SHADOW", "1" },
			{ "IGNOREROUGHNESS", "1" },
			{ "FILTER_PCF9", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3089",
		{},
		"f37b236a1e3f36b2f836b2e8a251ca7658923024",
		{
			{ "BSDFLIGHT_PS_DEFERRED", "1" },
			{ "POINTOMNI", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "IGNOREROUGHNESS", "1" },
			{ "FILTER_PCF9", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3090",
		{},
		"cb87d220e14bdd5470a75aa9b1ddf4e92153cb75",
		{
			{ "BSDFLIGHT_PS_DEFERRED", "1" },
			{ "POINTOMNI", "1" },
			{ "SHADOW", "1" },
			{ "HALFOMNI", "1" },
			{ "SPECULAR", "1" },
			{ "IGNOREROUGHNESS", "1" },
			{ "FILTER_PCF9", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3091",
		{},
		"1cb3b8adcaef66f61f408874c03bed9914ec7f67",
		{
			{ "BSDFLIGHT_PS_DEFERRED", "1" },
			{ "POINTSPOT", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "IGNOREROUGHNESS", "1" },
			{ "FILTER_PCF9", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3092",
		{},
		"6a62a10b141bc872998797d9e8079a8459f2e676",
		{
			{ "BSDFLIGHT_PS_DEFERRED", "1" },
			{ "POINTSPOT", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "GOBOPROJECTION", "1" },
			{ "IGNOREROUGHNESS", "1" },
			{ "FILTER_PCF9", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3093",
		{},
		"27ca11039e1e098bf4c1d9ea384917897e4434f3",
		{
			{ "BSDFLIGHT_PS_OVERDRAW", "1" },
			{ "OVERDRAW", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3095",
		{},
		"8cce3fb7eb9abdad984423c0bb61af92f710694d",
		{
			{ "BSDFLIGHT_PS_DEFERRED", "1" },
			{ "POINTSPOT", "1" },
			{ "SHADOW", "1" },
			{ "IGNOREROUGHNESS", "1" },
			{ "IGNORERIM", "1" },
			{ "FILTER_PCF9", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3097",
		{},
		"570b99b6fe4f7aff0f08a61d55c4e27e3785740c",
		{
			{ "BSDFLIGHT_PS_DEFERRED", "1" },
			{ "POINTOMNI", "1" },
			{ "SHADOW", "1" },
			{ "GOBOPROJECTION", "1" },
			{ "IGNOREROUGHNESS", "1" },
			{ "IGNORERIM", "1" },
			{ "FILTER_PCF9", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3098",
		{},
		"f8d37f115027ef5f681f03662342ae4d4412830d",
		{
			{ "BSDFLIGHT_PS_DEFERRED", "1" },
			{ "POINTSPOT", "1" },
			{ "SHADOW", "1" },
			{ "GOBOPROJECTION", "1" },
			{ "IGNOREROUGHNESS", "1" },
			{ "IGNORERIM", "1" },
			{ "FILTER_PCF9", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3099",
		{},
		"aa721295cd3b1ff82646b52dded82d88566224cd",
		{
			{ "BSDFLIGHT_PS_DIRSPLITS1", "1" },
			{ "DIRECTIONAL", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "FILTER_PCF9", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3101",
		{},
		"dd1ff1577fef2ac3d6233924fd9a64dd90cef13c",
		{
			{ "BSDFLIGHT_PS_DIRSPLITS2", "1" },
			{ "DIRECTIONAL", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "AMBIENT", "1" },
			{ "FILTER_PCF9", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "1" },
			{ "AMBIENT_IBL_IN_LIGHT", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3103",
		{},
		"84e5c748d45be8eeb3cc39844aeb44bd1ed7c7e6",
		{
			{ "BSDFLIGHT_PS_DIRSPLITS2", "1" },
			{ "DIRECTIONAL", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "IGNOREROUGHNESS", "1" },
			{ "AMBIENT", "1" },
			{ "FILTER_PCF9", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "1" },
			{ "AMBIENT_IBL_IN_LIGHT", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3104",
		{},
		"b732fcfa4b24e58f1876af206d794876d1cb962a",
		{
			{ "BSDFLIGHT_PS_DIRSPLITS1", "1" },
			{ "DIRECTIONAL", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "AMBIENT", "1" },
			{ "FILTER_PCF9", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3107",
		{},
		"f6578f4eadd1bdc8c5e30e0113fbe592b380c83c",
		{
			{ "BSDFLIGHT_PS_AMBIENT", "1" },
			{ "AMBIENT", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3109",
		{},
		"477c3e1ea7dab5028415c79725971a8a6ac55a09",
		{
			{ "BSDFLIGHT_PS_UNSHADOWED", "1" },
			{ "DIRECTIONAL", "1" },
			{ "SPECULAR", "1" },
			{ "AMBIENT", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3110",
		{},
		"f40250516b3aa42d3644cf07a0d97c40f0af0762",
		{
			{ "BSDFLIGHT_PS_DIRSPLITS2", "1" },
			{ "DIRECTIONAL", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "AMBIENT", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "1" },
			{ "AMBIENT_IBL_IN_LIGHT", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3113",
		{},
		"ab98af9145c081d80af7b0cd444b72bb108119bc",
		{
			{ "BSDFLIGHT_PS_DIRSPLITS3", "1" },
			{ "DIRECTIONAL", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "AMBIENT", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3114",
		{},
		"964e9b82cde7aecea38bc999d1c925127da0bc01",
		{
			{ "BSDFLIGHT_PS_SHADOW_ONLY", "1" },
			{ "DIRECTIONAL", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "SHADOW_ONLY", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "1" },
			{ "LIGHT_TYPE", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3115",
		{},
		"815117736d04057d09c827a9663d0298ab9ed113",
		{
			{ "BSDFLIGHT_PS_DIRSPLITS3", "1" },
			{ "DIRECTIONAL", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "FILTER_PCF1", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3117",
		{},
		"9fc11553c6068eaccff5a603cf038a9b4cc65546",
		{
			{ "BSDFLIGHT_PS_DIRSPLITS1", "1" },
			{ "DIRECTIONAL", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "FILTER_PCF1", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3119",
		{},
		"fa81943b56a8cac6bb20e0ac36ed7b1bb8adb366",
		{
			{ "BSDFLIGHT_PS_DIRSPLITS3", "1" },
			{ "DIRECTIONAL", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "AMBIENT", "1" },
			{ "FILTER_PCF1", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3121",
		{},
		"cf3f9141478b244932e875909255d1ebde3373e5",
		{
			{ "BSDFLIGHT_PS_DIRSPLITS1", "1" },
			{ "DIRECTIONAL", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "AMBIENT", "1" },
			{ "FILTER_PCF1", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3123",
		{},
		"0bff5e0ecdf732495be001a37160d8aa11a9660c",
		{
			{ "BSDFLIGHT_PS_SHADOW_ONLY", "1" },
			{ "DIRECTIONAL", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "SHADOW_ONLY", "1" },
			{ "FILTER_PCF1", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "1" },
			{ "LIGHT_TYPE", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3124",
		{},
		"67dd9b9c145fc4297c316c1da8536bcd2c87146a",
		{
			{ "BSDFLIGHT_PS_DIRSPLITS3", "1" },
			{ "DIRECTIONAL", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "FILTER_PCF9", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3128",
		{},
		"c10b80bec8e012691d020cdeb2e732ebdc0676e7",
		{
			{ "BSDFLIGHT_PS_DIRSPLITS3", "1" },
			{ "DIRECTIONAL", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "AMBIENT", "1" },
			{ "FILTER_PCF9", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3132",
		{},
		"ba94fedf2ed412c7dd0f770a990f735c92768b44",
		{
			{ "BSDFLIGHT_PS_SHADOW_ONLY", "1" },
			{ "DIRECTIONAL", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "SHADOW_ONLY", "1" },
			{ "FILTER_PCF9", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "1" },
			{ "LIGHT_TYPE", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3133",
		{},
		"423798e1b7099cba22ff0588ec80fb81338c9679",
		{
			{ "BSDFLIGHT_PS_DIRSPLITS3", "1" },
			{ "DIRECTIONAL", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "FILTER_POISSON", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3135",
		{},
		"02427236dcf3dc126e41ad38aaf2c07aedd43b5f",
		{
			{ "BSDFLIGHT_PS_DIRSPLITS1", "1" },
			{ "DIRECTIONAL", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "FILTER_POISSON", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3137",
		{},
		"4cf1e9e3cff2038a6ff7c8db38d234b13bda47fa",
		{
			{ "BSDFLIGHT_PS_DIRSPLITS3", "1" },
			{ "DIRECTIONAL", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "AMBIENT", "1" },
			{ "FILTER_POISSON", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3139",
		{},
		"a1d88864cd30485d84f98e36eaf281d15bd15c47",
		{
			{ "BSDFLIGHT_PS_DIRSPLITS1", "1" },
			{ "DIRECTIONAL", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "AMBIENT", "1" },
			{ "FILTER_POISSON", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3141",
		{},
		"99a112e7bc7fc4fbcefac716864d6e6a9cdcac68",
		{
			{ "BSDFLIGHT_PS_SHADOW_ONLY", "1" },
			{ "DIRECTIONAL", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "SHADOW_ONLY", "1" },
			{ "FILTER_POISSON", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "1" },
			{ "LIGHT_TYPE", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3143",
		{},
		"d32da64922f862a7916957551f343c640ac59652",
		{
			{ "BSDFLIGHT_PS_DIRSPLITS3", "1" },
			{ "DIRECTIONAL", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "FILTER_PCSS", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3145",
		{},
		"4fa0caff68964bd2d0d358b3a02560c3d01c4efe",
		{
			{ "BSDFLIGHT_PS_DIRSPLITS3", "1" },
			{ "DIRECTIONAL", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "IGNOREROUGHNESS", "1" },
			{ "FILTER_PCSS", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3146",
		{},
		"42b270dd2f5aa8f9238314a376a1f0ceec580d5d",
		{
			{ "BSDFLIGHT_PS_DIRSPLITS1", "1" },
			{ "DIRECTIONAL", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "FILTER_PCSS", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3147",
		{},
		"a5e2f8a0985e36da3362b2f707de2d8557d9cd5d",
		{
			{ "BSDFLIGHT_PS_DIRSPLITS3", "1" },
			{ "DIRECTIONAL", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "AMBIENT", "1" },
			{ "FILTER_PCSS", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3149",
		{},
		"90eddd0477bf2edc537037df9d9575888c92334c",
		{
			{ "BSDFLIGHT_PS_DIRSPLITS3", "1" },
			{ "DIRECTIONAL", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "IGNOREROUGHNESS", "1" },
			{ "AMBIENT", "1" },
			{ "FILTER_PCSS", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3150",
		{},
		"8ccc1b02d0efde734ae4d0d5f58cbe986c5e8b0c",
		{
			{ "BSDFLIGHT_PS_DIRSPLITS1", "1" },
			{ "DIRECTIONAL", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "AMBIENT", "1" },
			{ "FILTER_PCSS", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3151",
		{},
		"53e2fcf10e89547e9d02969173839a2768b22e22",
		{
			{ "BSDFLIGHT_PS_SHADOW_ONLY", "1" },
			{ "DIRECTIONAL", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "SHADOW_ONLY", "1" },
			{ "FILTER_PCSS", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "1" },
			{ "LIGHT_TYPE", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3152",
		{},
		"e5ef2e946298f7eea6702473dbeef4202c7ca821",
		{
			{ "BSDFLIGHT_PS_SHADOW_ONLY", "1" },
			{ "DIRECTIONAL", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "SHADOW_ONLY", "1" },
			{ "FILTER_PCSSPOISSON", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "1" },
			{ "LIGHT_TYPE", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3153",
		{},
		"987c4e79e0e7a466da1156a35ade792df6939072",
		{
			{ "BSDFLIGHT_PS_UNSHADOWED", "1" },
			{ "DIRECTIONAL", "1" },
			{ "SPECULAR", "1" },
			{ "IGNOREROUGHNESS", "1" },
			{ "AMBIENT", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3154",
		{},
		"b21b8eb34c0ef5c307d268ee450e92eba7ea588f",
		{
			{ "BSDFLIGHT_PS_DIRSPLITS2", "1" },
			{ "DIRECTIONAL", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "IGNOREROUGHNESS", "1" },
			{ "AMBIENT", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "1" },
			{ "AMBIENT_IBL_IN_LIGHT", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3156",
		{},
		"5681c96c44b1f0f6d64de2b374ce59e5542795c4",
		{
			{ "BSDFLIGHT_PS_DIRSPLITS3", "1" },
			{ "DIRECTIONAL", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "BLENDSPLIT", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3157",
		{},
		"43b435e991198a5e8a4e69ecc2400edf3b279171",
		{
			{ "BSDFLIGHT_PS_DIRSPLITS3", "1" },
			{ "DIRECTIONAL", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "AMBIENT", "1" },
			{ "BLENDSPLIT", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3158",
		{},
		"d88e74c94cbb8fea6dfd5d35a0c194c659cc0821",
		{
			{ "BSDFLIGHT_PS_DIRSPLITS3", "1" },
			{ "DIRECTIONAL", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "BLENDSPLIT", "1" },
			{ "FILTER_PCF1", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3162",
		{},
		"a56bb4a01ef2f085ecf8e96b7d53b603bc288649",
		{
			{ "BSDFLIGHT_PS_DIRSPLITS3", "1" },
			{ "DIRECTIONAL", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "AMBIENT", "1" },
			{ "BLENDSPLIT", "1" },
			{ "FILTER_PCF1", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3165",
		{},
		"4e89dd81f40121cadb5f4b322a7323331576f05c",
		{
			{ "BSDFLIGHT_PS_SHADOW_ONLY_BLEND_SPLIT", "1" },
			{ "DIRECTIONAL", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "SHADOW_ONLY", "1" },
			{ "BLENDSPLIT", "1" },
			{ "FILTER_PCF1", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3167",
		{},
		"2b04bed67a5641536ecedceebedf8916d3130a97",
		{
			{ "BSDFLIGHT_PS_SHADOW_ONLY_BLEND_SPLIT", "1" },
			{ "DIRECTIONAL", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "SHADOW_ONLY", "1" },
			{ "BLENDSPLIT", "1" },
			{ "FILTER_PCF1", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "1" },
			{ "AMBIENT", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3168",
		{},
		"631d032081292ee72fb2a2b145865ab0a250a918",
		{
			{ "BSDFLIGHT_PS_DIRSPLITS3", "1" },
			{ "DIRECTIONAL", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "BLENDSPLIT", "1" },
			{ "FILTER_PCF9", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3170",
		{},
		"e8e275a22c6146fdb7936db718172d268218c705",
		{
			{ "BSDFLIGHT_PS_DIRSPLITS3", "1" },
			{ "DIRECTIONAL", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "IGNOREROUGHNESS", "1" },
			{ "BLENDSPLIT", "1" },
			{ "FILTER_PCF9", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3173",
		{},
		"66bdcba1d4faf2b2a928d4cf7fe812fb7ae44740",
		{
			{ "BSDFLIGHT_PS_DIRSPLITS3", "1" },
			{ "DIRECTIONAL", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "AMBIENT", "1" },
			{ "BLENDSPLIT", "1" },
			{ "FILTER_PCF9", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3175",
		{},
		"247b7a714386c80cc9adc99f7529937e1e4e475d",
		{
			{ "BSDFLIGHT_PS_DIRSPLITS3", "1" },
			{ "DIRECTIONAL", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "IGNOREROUGHNESS", "1" },
			{ "AMBIENT", "1" },
			{ "BLENDSPLIT", "1" },
			{ "FILTER_PCF9", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3177",
		{},
		"1eb732dd3f746afb73b3c3f785eb6ec5af93f1a7",
		{
			{ "BSDFLIGHT_PS_SHADOW_ONLY_BLEND_SPLIT", "1" },
			{ "DIRECTIONAL", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "SHADOW_ONLY", "1" },
			{ "BLENDSPLIT", "1" },
			{ "FILTER_PCF9", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3179",
		{},
		"e3b027b8c5549dc429abf8356c0a1e300616027d",
		{
			{ "BSDFLIGHT_PS_SHADOW_ONLY_BLEND_SPLIT", "1" },
			{ "DIRECTIONAL", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "SHADOW_ONLY", "1" },
			{ "BLENDSPLIT", "1" },
			{ "FILTER_PCF9", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "1" },
			{ "AMBIENT", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3180",
		{},
		"f9c1d441ee71275b4459e911f308a456e70c81bc",
		{
			{ "BSDFLIGHT_PS_DIRSPLITS3", "1" },
			{ "DIRECTIONAL", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "BLENDSPLIT", "1" },
			{ "FILTER_POISSON", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3182",
		{},
		"d54e97b02ea33c40ba8f68455732fb04f49422d8",
		{
			{ "BSDFLIGHT_PS_DIRSPLITS3", "1" },
			{ "DIRECTIONAL", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "IGNOREROUGHNESS", "1" },
			{ "BLENDSPLIT", "1" },
			{ "FILTER_POISSON", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3185",
		{},
		"7c8a4acf51da2311f93784fa8da190d8431db767",
		{
			{ "BSDFLIGHT_PS_DIRSPLITS3", "1" },
			{ "DIRECTIONAL", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "AMBIENT", "1" },
			{ "BLENDSPLIT", "1" },
			{ "FILTER_POISSON", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3187",
		{},
		"3de16ba02f3aa37f56fc64048c0b759e7a6aa1e3",
		{
			{ "BSDFLIGHT_PS_DIRSPLITS3", "1" },
			{ "DIRECTIONAL", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "IGNOREROUGHNESS", "1" },
			{ "AMBIENT", "1" },
			{ "BLENDSPLIT", "1" },
			{ "FILTER_POISSON", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3189",
		{},
		"e928ab5f44eede452d26262c9cd09ad0ca3839b7",
		{
			{ "BSDFLIGHT_PS_SHADOW_ONLY_BLEND_SPLIT", "1" },
			{ "DIRECTIONAL", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "SHADOW_ONLY", "1" },
			{ "BLENDSPLIT", "1" },
			{ "FILTER_POISSON", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3191",
		{},
		"c56b2b7862b68c7a7753e9c543a48378cb0c607a",
		{
			{ "BSDFLIGHT_PS_SHADOW_ONLY_BLEND_SPLIT", "1" },
			{ "DIRECTIONAL", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "SHADOW_ONLY", "1" },
			{ "BLENDSPLIT", "1" },
			{ "FILTER_POISSON", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "1" },
			{ "AMBIENT", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3192",
		{},
		"418df8b0febb3e8dd639d7a226978b9bbedd1506",
		{
			{ "BSDFLIGHT_PS_DEFERRED", "1" },
			{ "SPECULAR", "1" },
			{ "SPOT", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3193",
		{},
		"42efec596e98da27936beadb5f133dc442559673",
		{
			{ "BSDFLIGHT_PS_DIRSPLITS3", "1" },
			{ "DIRECTIONAL", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "BLENDSPLIT", "1" },
			{ "FILTER_PCSS", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3195",
		{},
		"5ac2a9512c9da8896cb058c6a01b6e3f672c84c2",
		{
			{ "BSDFLIGHT_PS_DIRSPLITS3", "1" },
			{ "DIRECTIONAL", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "AMBIENT", "1" },
			{ "BLENDSPLIT", "1" },
			{ "FILTER_PCSS", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3196",
		{},
		"98d5291ab3059f9e6427ee6f5b8d11331f7017ad",
		{
			{ "BSDFLIGHT_PS_DIRSPLITS3", "1" },
			{ "DIRECTIONAL", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "BLENDSPLIT", "1" },
			{ "FILTER_PCSSPOISSON", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3197",
		{},
		"45ca225fda631d2d4874465bd15092a63edf7db7",
		{
			{ "BSDFLIGHT_PS_DIRSPLITS3", "1" },
			{ "DIRECTIONAL", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "AMBIENT", "1" },
			{ "BLENDSPLIT", "1" },
			{ "FILTER_PCSSPOISSON", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3198",
		{},
		"1b2eca6d889c56a51f1900a61786256024bf0195",
		{
			{ "BSDFLIGHT_PS_DEFERRED", "1" },
			{ "POINTOMNI", "1" },
			{ "SHADOW", "1" },
			{ "HALFOMNI", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3199",
		{},
		"ea2537f5040162605525c6c31621901a25d91a07",
		{
			{ "BSDFLIGHT_PS_UNSHADOWED", "1" },
			{ "POINTOMNI", "1" },
			{ "IGNOREROUGHNESS", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3200",
		{},
		"28858d7b79c4fa76bf95afaf13ba42d9127eab66",
		{
			{ "BSDFLIGHT_PS_UNSHADOWED", "1" },
			{ "DIRECTIONAL", "1" },
			{ "SPECULAR", "1" },
			{ "IGNOREROUGHNESS", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3201",
		{},
		"8765cebef76b887728d46489e6c3ffa852c2cdcb",
		{
			{ "BSDFLIGHT_PS_UNSHADOWED", "1" },
			{ "POINTOMNI", "1" },
			{ "SPECULAR", "1" },
			{ "IGNOREROUGHNESS", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3202",
		{},
		"aaec045678aacc0349b1599289dce144e0f1f571",
		{
			{ "BSDFLIGHT_PS_DIRSPLITS2", "1" },
			{ "DIRECTIONAL", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "BLENDSPLIT", "1" },
			{ "FILTER_PCF1", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3206",
		{},
		"b07e0954bf4b261a8ef5814a026ee860f623d666",
		{
			{ "BSDFLIGHT_PS_DEFERRED", "1" },
			{ "SPOT", "1" },
			{ "IGNOREROUGHNESS", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3207",
		{},
		"c5145c962a2c189e3778352b2aa027f6368b992b",
		{
			{ "BSDFLIGHT_PS_DIRSPLITS2", "1" },
			{ "DIRECTIONAL", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "AMBIENT", "1" },
			{ "BLENDSPLIT", "1" },
			{ "FILTER_PCF1", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "1" },
			{ "AMBIENT_IBL_IN_LIGHT", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3213",
		{},
		"e283ea89bad28ce4310ad19a26774c2519506e34",
		{
			{ "BSDFLIGHT_PS_DIRSPLITS2", "1" },
			{ "DIRECTIONAL", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "BLENDSPLIT", "1" },
			{ "FILTER_PCF9", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3215",
		{},
		"f4a8c9d256f1cd8d209379053a41c9dcd6350175",
		{
			{ "BSDFLIGHT_PS_DIRSPLITS2", "1" },
			{ "DIRECTIONAL", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "IGNOREROUGHNESS", "1" },
			{ "BLENDSPLIT", "1" },
			{ "FILTER_PCF9", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3218",
		{},
		"ca5492afed87f95baa258687394794de9548e531",
		{
			{ "BSDFLIGHT_PS_DEFERRED", "1" },
			{ "SPECULAR", "1" },
			{ "SPOT", "1" },
			{ "IGNOREROUGHNESS", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3219",
		{},
		"afb65b699e5a18204afc3c37457f5afcd54d5734",
		{
			{ "BSDFLIGHT_PS_DIRSPLITS2", "1" },
			{ "DIRECTIONAL", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "AMBIENT", "1" },
			{ "BLENDSPLIT", "1" },
			{ "FILTER_PCF9", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "1" },
			{ "AMBIENT_IBL_IN_LIGHT", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3221",
		{},
		"0af02fad1e862e517ade458d0a2fa4916db45e9d",
		{
			{ "BSDFLIGHT_PS_DIRSPLITS2", "1" },
			{ "DIRECTIONAL", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "IGNOREROUGHNESS", "1" },
			{ "AMBIENT", "1" },
			{ "BLENDSPLIT", "1" },
			{ "FILTER_PCF9", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "1" },
			{ "AMBIENT_IBL_IN_LIGHT", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3226",
		{},
		"aa5cd5f492d921546a2b9cf66d34eae9baedf63f",
		{
			{ "BSDFLIGHT_PS_ATTENUATION_ONLY", "1" },
			{ "POINTOMNI", "1" },
			{ "ATTENUATION_ONLY", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3227",
		{},
		"50e2618e8d1a8c3400c2bdb0129e510fe395d19a",
		{
			{ "BSDFLIGHT_PS_DIRSPLITS2", "1" },
			{ "DIRECTIONAL", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "BLENDSPLIT", "1" },
			{ "FILTER_POISSON", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3229",
		{},
		"1cb2449c07870b64a05c696199a0dcf2108f73f0",
		{
			{ "BSDFLIGHT_PS_DIRSPLITS2", "1" },
			{ "DIRECTIONAL", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "IGNOREROUGHNESS", "1" },
			{ "BLENDSPLIT", "1" },
			{ "FILTER_POISSON", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3232",
		{},
		"94f8385edd1b4eb232b1de269e1ad7b21122a293",
		{
			{ "BSDFLIGHT_PS_DIRSPLITS2", "1" },
			{ "DIRECTIONAL", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "AMBIENT", "1" },
			{ "BLENDSPLIT", "1" },
			{ "FILTER_POISSON", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "1" },
			{ "AMBIENT_IBL_IN_LIGHT", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3234",
		{},
		"7300db73a915e0b73974d50ed72f5a5090a58a3e",
		{
			{ "BSDFLIGHT_PS_DIRSPLITS2", "1" },
			{ "DIRECTIONAL", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "IGNOREROUGHNESS", "1" },
			{ "AMBIENT", "1" },
			{ "BLENDSPLIT", "1" },
			{ "FILTER_POISSON", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "1" },
			{ "AMBIENT_IBL_IN_LIGHT", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3239",
		{},
		"09c2bd0951da6b3ce9ccf877c90b092a4b9f7a21",
		{
			{ "BSDFLIGHT_PS_GOBO", "1" },
			{ "POINTOMNI", "1" },
			{ "GOBOPROJECTION", "1" },
			{ "IGNOREROUGHNESS", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3241",
		{},
		"3292d3f70ffc2a9265eb393531ad30620da67a09",
		{
			{ "BSDFLIGHT_PS_CHARACTER_LIGHT_C26", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3242",
		{},
		"da9c1cd4400516c4cd1edb0d6b6e8e0b2e717926",
		{
			{ "BSDFLIGHT_PS_DEFERRED", "1" },
			{ "POINTOMNI", "1" },
			{ "SHADOW", "1" },
			{ "FILTER_POISSON", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3243",
		{},
		"f09956bfb0ab9fee058b86702953b2db62626d94",
		{
			{ "BSDFLIGHT_PS_DEFERRED", "1" },
			{ "POINTOMNI", "1" },
			{ "SHADOW", "1" },
			{ "HALFOMNI", "1" },
			{ "FILTER_POISSON", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3244",
		{},
		"d8dd889b57c46b3d670e6bca8737681714670b07",
		{
			{ "BSDFLIGHT_PS_DEFERRED", "1" },
			{ "POINTSPOT", "1" },
			{ "SHADOW", "1" },
			{ "FILTER_POISSON", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3245",
		{},
		"dae1235b15fb8b9266bd3166ded958c19f9192d3",
		{
			{ "BSDFLIGHT_PS_DIRSPLITS2", "1" },
			{ "DIRECTIONAL", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "BLENDSPLIT", "1" },
			{ "FILTER_PCSS", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3246",
		{},
		"f4ece8123e2a49a2777079fd1284732cfbfa9a14",
		{
			{ "BSDFLIGHT_PS_DIRSPLITS2", "1" },
			{ "DIRECTIONAL", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "FILTER_POISSON", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3247",
		{},
		"77b57f6ca04021e14f6fcc895103a9fc4aabba0d",
		{
			{ "BSDFLIGHT_PS_DEFERRED", "1" },
			{ "POINTOMNI", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "FILTER_POISSON", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3248",
		{},
		"c1519068b7928fbf7d881a264df5614acba70d17",
		{
			{ "BSDFLIGHT_PS_DEFERRED", "1" },
			{ "POINTOMNI", "1" },
			{ "SHADOW", "1" },
			{ "HALFOMNI", "1" },
			{ "SPECULAR", "1" },
			{ "FILTER_POISSON", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3249",
		{},
		"b4f6f4d636946d6ba34114a0803222bff0889914",
		{
			{ "BSDFLIGHT_PS_DEFERRED", "1" },
			{ "POINTSPOT", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "FILTER_POISSON", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3251",
		{},
		"9d216a947e713d67f24829cc94294ca57dc1f943",
		{
			{ "BSDFLIGHT_PS_DEFERRED", "1" },
			{ "POINTSPOT", "1" },
			{ "SHADOW", "1" },
			{ "ATTENUATION_ONLY", "1" },
			{ "FILTER_POISSON", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3252",
		{},
		"a65b5952a6484b6a3adc9b4a14bea3d2eeff5059",
		{
			{ "BSDFLIGHT_PS_GOBO", "1" },
			{ "POINTOMNI", "1" },
			{ "SPECULAR", "1" },
			{ "GOBOPROJECTION", "1" },
			{ "IGNOREROUGHNESS", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3253",
		{},
		"54b92a2288a96aa827219a682f59303338a127e1",
		{
			{ "BSDFLIGHT_PS_DEFERRED", "1" },
			{ "POINTSPOT", "1" },
			{ "SHADOW", "1" },
			{ "GOBOPROJECTION", "1" },
			{ "FILTER_POISSON", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3254",
		{},
		"7c03ee05df657890397683357d4da56584aeae04",
		{
			{ "BSDFLIGHT_PS_DEFERRED", "1" },
			{ "POINTOMNI", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "GOBOPROJECTION", "1" },
			{ "FILTER_POISSON", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3255",
		{},
		"b74181bcda79e8a737b2ae7cab2b81cc549f4ea0",
		{
			{ "BSDFLIGHT_PS_DEFERRED", "1" },
			{ "POINTOMNI", "1" },
			{ "SHADOW", "1" },
			{ "HALFOMNI", "1" },
			{ "SPECULAR", "1" },
			{ "GOBOPROJECTION", "1" },
			{ "FILTER_POISSON", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3256",
		{},
		"204b670e795f921520bc587a546b1ac3eed0d3ce",
		{
			{ "BSDFLIGHT_PS_DEFERRED", "1" },
			{ "POINTSPOT", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "GOBOPROJECTION", "1" },
			{ "FILTER_POISSON", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3257",
		{},
		"cea19eb5dcf4f7316171f245720d24248e894498",
		{
			{ "BSDFLIGHT_PS_DIRSPLITS2", "1" },
			{ "DIRECTIONAL", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "AMBIENT", "1" },
			{ "BLENDSPLIT", "1" },
			{ "FILTER_PCSS", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "1" },
			{ "AMBIENT_IBL_IN_LIGHT", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3258",
		{},
		"3ac8bbf905fe5c2ea7f3c3b233c658f9c4551cf8",
		{
			{ "BSDFLIGHT_PS_DEFERRED", "1" },
			{ "POINTOMNI", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "IGNOREROUGHNESS", "1" },
			{ "FILTER_POISSON", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3259",
		{},
		"4946991311da678f2cbf2321fb9561b15501ddd1",
		{
			{ "BSDFLIGHT_PS_DEFERRED", "1" },
			{ "POINTSPOT", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "IGNOREROUGHNESS", "1" },
			{ "FILTER_POISSON", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3260",
		{},
		"78fe3860f261f25390216b93b6a522ea037e2946",
		{
			{ "BSDFLIGHT_PS_DEFERRED", "1" },
			{ "POINTSPOT", "1" },
			{ "SHADOW", "1" },
			{ "IGNOREROUGHNESS", "1" },
			{ "IGNORERIM", "1" },
			{ "FILTER_POISSON", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3261",
		{},
		"f80904f47b79f125ea1daa3ac603a69bde864d77",
		{
			{ "BSDFLIGHT_PS_DEFERRED", "1" },
			{ "POINTOMNI", "1" },
			{ "SHADOW", "1" },
			{ "GOBOPROJECTION", "1" },
			{ "IGNOREROUGHNESS", "1" },
			{ "IGNORERIM", "1" },
			{ "FILTER_POISSON", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3262",
		{},
		"80ddfd20ef7a48beaa3a84dd3697ac698a84e528",
		{
			{ "BSDFLIGHT_PS_DEFERRED", "1" },
			{ "POINTSPOT", "1" },
			{ "SHADOW", "1" },
			{ "GOBOPROJECTION", "1" },
			{ "IGNOREROUGHNESS", "1" },
			{ "IGNORERIM", "1" },
			{ "FILTER_POISSON", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3265",
		{},
		"0218832a2876f66aa7e4ed72a13f64c797704e63",
		{
			{ "BSDFLIGHT_PS_DEFERRED", "1" },
			{ "SPECULAR", "1" },
			{ "SPOT", "1" },
			{ "GOBOPROJECTION", "1" },
			{ "IGNOREROUGHNESS", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3266",
		{},
		"0c319651badcd58e3987596d9ebeb562f9387ad9",
		{
			{ "BSDFLIGHT_PS_DIRSPLITS2", "1" },
			{ "DIRECTIONAL", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "AMBIENT", "1" },
			{ "FILTER_POISSON", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "1" },
			{ "AMBIENT_IBL_IN_LIGHT", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3268",
		{},
		"7d2f0902eea714faff38b932402d96c368d76794",
		{
			{ "BSDFLIGHT_PS_DIRSPLITS2", "1" },
			{ "DIRECTIONAL", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "IGNOREROUGHNESS", "1" },
			{ "AMBIENT", "1" },
			{ "FILTER_POISSON", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "1" },
			{ "AMBIENT_IBL_IN_LIGHT", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3276",
		{},
		"12d92cd37efa9722bd27657f54fcddf1879170cd",
		{
			{ "BSDFLIGHT_PS_UNSHADOWED", "1" },
			{ "POINTOMNI", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3282",
		{},
		"039c893525975f0dcfbe3d1f175fed77bf36330a",
		{
			{ "BSDFLIGHT_PS_UNSHADOWED", "1" },
			{ "DIRECTIONAL", "1" },
			{ "SPECULAR", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3283",
		{},
		"9f44ba67a6ec8e3b9bf8b979e79bc563249fd0d7",
		{
			{ "BSDFLIGHT_PS_UNSHADOWED", "1" },
			{ "POINTOMNI", "1" },
			{ "SPECULAR", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3286",
		{},
		"fa6948bafbdfb46c8af31caff6690ac2161ce8d5",
		{
			{ "BSDFLIGHT_PS_GOBO", "1" },
			{ "POINTOMNI", "1" },
			{ "GOBOPROJECTION", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3287",
		{},
		"9969e800683c8a7c8afc25f41582415d79cbe47e",
		{
			{ "BSDFLIGHT_PS_GOBO", "1" },
			{ "POINTOMNI", "1" },
			{ "SPECULAR", "1" },
			{ "GOBOPROJECTION", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3289",
		{},
		"b4337a894d9335e2d96487eb0c4faeffebdf8c3d",
		{
			{ "BSDFLIGHT_PS_UNSHADOWED", "1" },
			{ "POINTOMNI", "1" },
			{ "IGNORERIM", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3298",
		{},
		"388c13087069397fcc3f4b2a8e3f96e59c003d34",
		{
			{ "BSDFLIGHT_PS_DEFERRED", "1" },
			{ "POINTSPOT", "1" },
			{ "SHADOW", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3301",
		{},
		"fcabd74908d7044bf0cbf0303679775501a0f235",
		{
			{ "BSDFLIGHT_PS_UNSHADOWED", "1" },
			{ "POINTOMNI", "1" },
			{ "SPECULAR", "1" },
			{ "IGNORERIM", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3302",
		{},
		"306ba03a7778a6b79c9303dbdf2db49dc1afaa68",
		{
			{ "BSDFLIGHT_PS_DEFERRED", "1" },
			{ "SPOT", "1" },
			{ "IGNORERIM", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3303",
		{},
		"d3331d19dfc93b63d3e078d5fa1db51689a206aa",
		{
			{ "BSDFLIGHT_PS_GOBO", "1" },
			{ "POINTOMNI", "1" },
			{ "GOBOPROJECTION", "1" },
			{ "IGNORERIM", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3304",
		{},
		"f33e32f9c8022c1739991fdc8d543f0d94a7c4a0",
		{
			{ "BSDFLIGHT_PS_GOBO", "1" },
			{ "POINTOMNI", "1" },
			{ "SPECULAR", "1" },
			{ "GOBOPROJECTION", "1" },
			{ "IGNORERIM", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3307",
		{},
		"23234de9cff52ae49d0a14ca131e33e8b2338f0c",
		{
			{ "BSDFLIGHT_PS_DIRSPLITS2", "1" },
			{ "DIRECTIONAL", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "FILTER_PCSS", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3309",
		{},
		"626fabfa6cb838e7eaff0495b8bad0bf2376778f",
		{
			{ "BSDFLIGHT_PS_DIRSPLITS2", "1" },
			{ "DIRECTIONAL", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "IGNOREROUGHNESS", "1" },
			{ "FILTER_PCSS", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3311",
		{},
		"d13839fca17e56c395a5b3d371bf93128b097309",
		{
			{ "BSDFLIGHT_PS_DIRSPLITS2", "1" },
			{ "DIRECTIONAL", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "AMBIENT", "1" },
			{ "FILTER_PCSS", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "1" },
			{ "AMBIENT_IBL_IN_LIGHT", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3313",
		{},
		"be0ba5322102a0771d4575394d34fe5e0c8be1f3",
		{
			{ "BSDFLIGHT_PS_DIRSPLITS2", "1" },
			{ "DIRECTIONAL", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "IGNOREROUGHNESS", "1" },
			{ "AMBIENT", "1" },
			{ "FILTER_PCSS", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "1" },
			{ "AMBIENT_IBL_IN_LIGHT", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3319",
		{},
		"934eccbe8072ec6cea5bab45a7b3c49e9ff8eecc",
		{
			{ "BSDFLIGHT_PS_DEFERRED", "1" },
			{ "SPOT", "1" },
			{ "GOBOPROJECTION", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3322",
		{},
		"0fd35e4a13c9ce8197188b6e4883e51f734745f1",
		{
			{ "BSDFLIGHT_PS_DIRSPLITS2", "1" },
			{ "DIRECTIONAL", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3324",
		{},
		"a8ef826c26fd950c6a491e9e0528fe4b23d2a773",
		{
			{ "BSDFLIGHT_PS_DEFERRED", "1" },
			{ "POINTOMNI", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3325",
		{},
		"343061244ac85f5bf6b304454332ec009d04e2f4",
		{
			{ "BSDFLIGHT_PS_DEFERRED", "1" },
			{ "POINTOMNI", "1" },
			{ "SHADOW", "1" },
			{ "FILTER_PCF1", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3326",
		{},
		"32528d53fb1239d33db643d03932dfa3d308dab2",
		{
			{ "BSDFLIGHT_PS_DEFERRED", "1" },
			{ "POINTOMNI", "1" },
			{ "SHADOW", "1" },
			{ "HALFOMNI", "1" },
			{ "FILTER_PCF1", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3327",
		{},
		"89432623839a50dcb915ad3285917fcf5d99df75",
		{
			{ "BSDFLIGHT_PS_DEFERRED", "1" },
			{ "POINTSPOT", "1" },
			{ "SHADOW", "1" },
			{ "FILTER_PCF1", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3328",
		{},
		"3d1bcaf539fd34620773ae30fc7a3002f19fb426",
		{
			{ "BSDFLIGHT_PS_DIRSPLITS2", "1" },
			{ "DIRECTIONAL", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "FILTER_PCF1", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3329",
		{},
		"50c9d4c3ef7dd9f41fc93b23891fcd9cfd9d9c9c",
		{
			{ "BSDFLIGHT_PS_DEFERRED", "1" },
			{ "POINTOMNI", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "FILTER_PCF1", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3330",
		{},
		"14d23504eb8160a638ea97d2edabeffa6b3e4b0a",
		{
			{ "BSDFLIGHT_PS_DEFERRED", "1" },
			{ "POINTOMNI", "1" },
			{ "SHADOW", "1" },
			{ "HALFOMNI", "1" },
			{ "SPECULAR", "1" },
			{ "FILTER_PCF1", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3331",
		{},
		"bffd9e9e324654d21a6032350f7083af22bbdff8",
		{
			{ "BSDFLIGHT_PS_DEFERRED", "1" },
			{ "POINTSPOT", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "FILTER_PCF1", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3333",
		{},
		"dab3065cd56d9d8772dc78d3ffd7d4c048c81f3e",
		{
			{ "BSDFLIGHT_PS_DEFERRED", "1" },
			{ "POINTSPOT", "1" },
			{ "SHADOW", "1" },
			{ "ATTENUATION_ONLY", "1" },
			{ "FILTER_PCF1", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3334",
		{},
		"c0024122a3d253b5df2eafe1f736e907d16952aa",
		{
			{ "BSDFLIGHT_PS_DEFERRED", "1" },
			{ "POINTSPOT", "1" },
			{ "SHADOW", "1" },
			{ "GOBOPROJECTION", "1" },
			{ "FILTER_PCF1", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3335",
		{},
		"236ca6a0bd709759b4dd946935809a5f466ea375",
		{
			{ "BSDFLIGHT_PS_DEFERRED", "1" },
			{ "POINTOMNI", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "GOBOPROJECTION", "1" },
			{ "FILTER_PCF1", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3336",
		{},
		"b8b4935974c95f686652a3621eebd02dad40035c",
		{
			{ "BSDFLIGHT_PS_DEFERRED", "1" },
			{ "POINTOMNI", "1" },
			{ "SHADOW", "1" },
			{ "HALFOMNI", "1" },
			{ "SPECULAR", "1" },
			{ "GOBOPROJECTION", "1" },
			{ "FILTER_PCF1", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3337",
		{},
		"3e1d0db60d011efcfbafe8c6a2fb139a245c5ce3",
		{
			{ "BSDFLIGHT_PS_DEFERRED", "1" },
			{ "POINTSPOT", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "GOBOPROJECTION", "1" },
			{ "FILTER_PCF1", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3339",
		{},
		"7dbeaf79423a8e0c526f6d60ae01af0dc347d87b",
		{
			{ "BSDFLIGHT_PS_DIRSPLITS2", "1" },
			{ "DIRECTIONAL", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "IGNOREROUGHNESS", "1" },
			{ "FILTER_PCF1", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3340",
		{},
		"086430f042849184b1088a2b73c2a6a29453d535",
		{
			{ "BSDFLIGHT_PS_DEFERRED", "1" },
			{ "POINTOMNI", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "IGNOREROUGHNESS", "1" },
			{ "FILTER_PCF1", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3341",
		{},
		"bf12ddedcfac07bbd39a22e0278337106757bc37",
		{
			{ "BSDFLIGHT_PS_DEFERRED", "1" },
			{ "POINTOMNI", "1" },
			{ "SHADOW", "1" },
			{ "HALFOMNI", "1" },
			{ "SPECULAR", "1" },
			{ "IGNOREROUGHNESS", "1" },
			{ "FILTER_PCF1", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3342",
		{},
		"880720a9de4c3f9fb65b5f3984079d7cbb4d0b49",
		{
			{ "BSDFLIGHT_PS_DEFERRED", "1" },
			{ "POINTSPOT", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "IGNOREROUGHNESS", "1" },
			{ "FILTER_PCF1", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3343",
		{},
		"9b402cbfd7c7937314daf22058d023ece785af4e",
		{
			{ "BSDFLIGHT_PS_DEFERRED", "1" },
			{ "SPOT", "1" },
			{ "GOBOPROJECTION", "1" },
			{ "IGNOREROUGHNESS", "1" },
			{ "IGNORERIM", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3344",
		{},
		"99cf75b7d82b5c0af0e35650568a2a8a708a578d",
		{
			{ "BSDFLIGHT_PS_DEFERRED", "1" },
			{ "POINTSPOT", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3345",
		{},
		"83f27df1981c24585903831bb3b943b8062e9959",
		{
			{ "BSDFLIGHT_PS_DEFERRED", "1" },
			{ "POINTSPOT", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "GOBOPROJECTION", "1" },
			{ "IGNOREROUGHNESS", "1" },
			{ "FILTER_PCF1", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3346",
		{},
		"7251c19ad356e00ebcb433082bf3bffc2d08c509",
		{
			{ "BSDFLIGHT_PS_DEFERRED", "1" },
			{ "SPECULAR", "1" },
			{ "SPOT", "1" },
			{ "GOBOPROJECTION", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3347",
		{},
		"4521fc37900213f8a4befb7de7465dfa36d2efbd",
		{
			{ "BSDFLIGHT_PS_DEFERRED", "1" },
			{ "POINTSPOT", "1" },
			{ "SHADOW", "1" },
			{ "IGNOREROUGHNESS", "1" },
			{ "IGNORERIM", "1" },
			{ "FILTER_PCF1", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3348",
		{},
		"7d125da03c61a6615aa9f563ab3a721a6c9c6d14",
		{
			{ "BSDFLIGHT_PS_DEFERRED", "1" },
			{ "POINTOMNI", "1" },
			{ "SHADOW", "1" },
			{ "GOBOPROJECTION", "1" },
			{ "IGNOREROUGHNESS", "1" },
			{ "IGNORERIM", "1" },
			{ "FILTER_PCF1", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3349",
		{},
		"83a04487ca35da8c4afab93a6d3fd50326fbb63b",
		{
			{ "BSDFLIGHT_PS_DEFERRED", "1" },
			{ "POINTSPOT", "1" },
			{ "SHADOW", "1" },
			{ "GOBOPROJECTION", "1" },
			{ "IGNOREROUGHNESS", "1" },
			{ "IGNORERIM", "1" },
			{ "FILTER_PCF1", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3355",
		{},
		"c1099fef518fe9a1d5a5b26afc4a72aa27d6ff7c",
		{
			{ "BSDFLIGHT_PS_DIRSPLITS2", "1" },
			{ "DIRECTIONAL", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "AMBIENT", "1" },
			{ "FILTER_PCF1", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "1" },
			{ "AMBIENT_IBL_IN_LIGHT", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3358",
		{},
		"b0b309448219221651a25fa055ce2bfbf6a1a7a3",
		{
			{ "BSDFLIGHT_PS_CHARACTER_LIGHT", "1" },
			{ "CHARACTER_LIGHT", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3365",
		{},
		"1d64f145176575f18fab7c7c9c4f09e11274845b",
		{
			{ "BSDFLIGHT_PS_DIRSPLITS2", "1" },
			{ "DIRECTIONAL", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" },
			{ "IGNOREROUGHNESS", "1" },
			{ "AMBIENT", "1" },
			{ "FILTER_PCF1", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "1" },
			{ "AMBIENT_IBL_IN_LIGHT", "1" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_ps3374",
		{},
		"1a1c410f653ee5c5730b47e206126806ab92100c",
		{
			{ "BSDFLIGHT_PS_DEFERRED", "1" },
			{ "POINTOMNI", "1" },
			{ "SHADOW", "1" },
			{ "RGBSPEC", "1" },
			{ "DIRSPLITS", "2" },
			{ "LIGHT_TYPE", "3" }
		}));
	a_variants.push_back(MakeDefaultVariantRegistration(
		Target::kBsdfLight,
		"bsdflight_vs3071",
		{},
		"43f60dcc3daf4bb9df38cc8d9d6ef2ed9bc8de13",
		{
			{ "BSDFLIGHT_VS", "1" },
			{ "DIRSPLITS", "2" }
		},
		ShaderStage::kVertex));
}
