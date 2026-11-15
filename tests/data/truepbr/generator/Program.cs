// Regenerate: dotnet run --project tests\data\truepbr\generator -- tests\data\truepbr
using System.Text;
using MaterialLib;

var root = Path.GetFullPath(args.Length == 1 ? args[0] : throw new ArgumentException("usage: <output directory>"));
var rows = new List<string>();
var failures = 0;

const string Rock = @"pbr\rock\rock_";

// Valid files: every field of the reference reader's view lands in expected.tsv.
Valid("minimal", s => Textures(s, Rock + "color.dds", Rock + "n.dds", rmaos: Rock + "rmaos.dds"));

Valid("nondefault", s =>
{
    Textures(s, Rock + "color.dds", Rock + "n.dds", rmaos: Rock + "rmaos.dds");
    var c = s.Common;
    c.TileV = false;
    c.UvOffset = [0.25f, -0.5f];
    c.UvScale = [2f, 4f];
    c.Alpha = 0.75f;
    c.AlphaBlendMode = BaseMaterialFile.AlphaBlendModeType.Standard;
    c.AlphaTest = true;
    c.AlphaTestRef = 64;
    c.Refraction = true;
    c.RefractionFalloff = true;
    c.RefractionPower = 0.3f;
    c.ModelSpaceNormals = true;
    c.Emissive = true;
    c.EmissiveColor = [2.5f, 0.5f, 0.125f];
    c.EmissiveScale = 3f;
    c.ExternalEmittance = true;
    float[] values =
    [
        1.5f, 0.08f, 2.5f, 0.9f, 0.8f, 0.7f, 0.6f, 0.6f, 0.5f, 0.4f, 0.9f, 0.8f, 0.06f,
        0.3f, 0.2f, 0.1f, 0.7f, 2.5f, 20f, 0.1f, 3f, 0.4f, 0.3f, 0.2f, 2f,
    ];
    values.CopyTo(s.Parameters, 0);
}, part1: b => b.EmitEnabled = false); // Part 1 omits the emittance color; CSPB still emits.

Valid("coat_all_slots", s =>
{
    Textures(s, Rock + "color.dds", Rock + "n.dds", Rock + "glow.dds", Rock + "height.dds", Rock + "rmaos.dds", Rock + "coat.dds", Rock + "coatn.dds");
    s.FeatureFlags = Flag("twoLayer") | Flag("coloredCoat") | Flag("interlayerParallax") | Flag("coatNormal");
    Param(s, "coatStrength", 0.8f);
    Param(s, "coatRoughness", 0.2f);
});

Valid("subsurface_glint", s =>
{
    Textures(s, Rock + "color.dds", Rock + "n.dds", rmaos: Rock + "rmaos.dds");
    s.FeatureFlags = Flag("subsurface") | Flag("glint");
    Param(s, "subsurfaceOpacity", 0.5f);
    Param(s, "glintMicrofacetRoughness", 0.2f);
});

Valid("hair", s =>
{
    Textures(s, Rock + "color.dds", Rock + "n.dds", rmaos: Rock + "rmaos.dds");
    s.FeatureFlags = Flag("hairMarschner");
});

Valid("fuzz_csfz", s =>
{
    Textures(s, Rock + "color.dds", Rock + "n.dds", rmaos: Rock + "rmaos.dds");
    s.FeatureFlags = Flag("fuzz");
    Param(s, "fuzzWeight", 0.6f);
    s.FuzzOverride = new TruePbrFuzzOverride { Roughness = 0.35f, RoughnessFromDisplacementGreen = true };
});

Valid("foliage_csts", s =>
{
    Textures(s, Rock + "color.dds", Rock + "n.dds", rmaos: Rock + "rmaos.dds");
    s.FeatureFlags = Flag("foliage") | Flag("thinSurface");
    s.ThinSurfaceOverride = new TruePbrThinSurfaceOverride
    {
        SubsurfaceBalance = 0.6f,
        FrontSpecularWeight = 0.4f,
        BackSpecularWeight = 0.3f,
        BackRoughnessScale = 2f,
    };
});

// Block order and unknown tags: CSPB comes last, after a zero-size unknown block.
Valid("unknown_blocks", s => Textures(s, Rock + "color.dds", Rock + "n.dds", rmaos: Rock + "rmaos.dds"),
    blocks: s => [
        new MaterialBlock("XXXX", [1, 2, 3, 4, 5]),
        Block("CSTS", s.ThinSurfaceOverride!.Write),
        Block("CSFZ", s.FuzzOverride!.Write),
        new MaterialBlock("YYYY", []),
        Block("CSPB", s.Write),
    ],
    configure: s =>
    {
        s.ThinSurfaceOverride = new TruePbrThinSurfaceOverride();
        s.FuzzOverride = new TruePbrFuzzOverride();
    });

// Warns only: a texture outside pbr\ may replace a vanilla one.
Valid("outside_pbr", s => Textures(s, @"architecture\rock_d.dds", Rock + "n.dds", rmaos: Rock + "rmaos.dds"));

// Each invalid file breaks exactly one rule; the reference validator names it.
Invalid("layout_2", s => { s.Layout = 2; });
Invalid("payload_version_3", s => { s.PayloadVersion = 3; });
Invalid("flag_bit_10", s => s.FeatureFlags = 1u << 10);
Invalid("hair_with_glint", s => s.FeatureFlags = Flag("hairMarschner") | Flag("glint"));
Invalid("colored_coat_without_two_layer", s => s.FeatureFlags = Flag("coloredCoat"));
Invalid("two_layer_with_subsurface", s => s.FeatureFlags = Flag("twoLayer") | Flag("subsurface"));
Invalid("fuzz_with_glint", s => s.FeatureFlags = Flag("fuzz") | Flag("glint"));
Invalid("foliage_without_thin_surface", s => s.FeatureFlags = Flag("foliage"));
Invalid("parameter_above_range", s => Param(s, "coatStrength", 1.5f));
Invalid("parameter_below_range", s => Param(s, "glintScreenSpaceScale", 0.5f));
Invalid("parameter_nan", s => Param(s, "specularLevel", float.NaN));
Invalid("parameter_negative_color", s => s.Parameters[Index("fuzzColor") + 1] = -0.1f);
Invalid("emissive_scale_negative", s => s.Common.EmissiveScale = -1f);
Invalid("missing_base_color", s => s.Textures[0] = "");
Invalid("missing_rmaos", s => s.Textures[4] = "");
Invalid("interlayer_without_displacement", s => s.FeatureFlags = Flag("twoLayer") | Flag("interlayerParallax"));
Invalid("coat_normal_without_features1", s => s.FeatureFlags = Flag("twoLayer") | Flag("coatNormal"));

// Common-field rules the reference validator does not check; the expected text is ours.
Unreferenced("uv_offset_nan", s => s.Common.UvOffset = [float.NaN, 0f], "UV Offset must be finite.");
Unreferenced("alpha_nan", s => s.Common.Alpha = float.NaN, "Alpha must be finite.");
Unreferenced("emittance_color_negative", s => s.Common.EmissiveColor = [1f, -0.5f, 1f], "Emissive Color must be finite and at least 0.");

// Structural and override failures the reference reader or validator does not name.
Raw("duplicate_cspb", (s, p1) => Concat(p1, Block("CSPB", s.Write), Block("CSPB", s.Write)));
Raw("missing_cspb", (s, p1) => Concat(p1, Block("CSTS", new TruePbrThinSurfaceOverride().Write)));
Raw("trailing_bytes", (s, p1) => Concat(p1, new MaterialBlock("CSPB", [.. Payload(s.Write), 0]).ToBytes()));
Raw("truncated_block", (s, p1) => Concat(p1, Header("CSPB", 100000), new byte[16]));
Raw("truncated_header", (s, p1) => Concat(p1, Block("CSPB", s.Write), Encoding.ASCII.GetBytes("CSTS"), new byte[] { 1, 0 }));
Raw("cspb_short_payload", (s, p1) => Concat(p1, new MaterialBlock("CSPB", Payload(s.Write)[..^1]).ToBytes()));
Raw("csts_short_payload", (s, p1) => Concat(p1, Block("CSPB", s.Write), new MaterialBlock("CSTS", new byte[12]).ToBytes()));
Raw("csfz_bad_flag", (s, p1) => Concat(p1, Block("CSPB", s.Write), new MaterialBlock("CSFZ", [.. BitConverter.GetBytes(0.5f), 2, 0, 0, 0]).ToBytes()));
Raw("csts_out_of_range", (s, p1) => Concat(p1, Block("CSPB", s.Write),
    Block("CSTS", new TruePbrThinSurfaceOverride { SubsurfaceBalance = 2f }.Write)), reference: false);

File.WriteAllLines(Path.Combine(root, "expected.tsv"), rows.OrderBy(r => r, StringComparer.Ordinal));
Console.WriteLine($"wrote {rows.Count} fixtures, {failures} generator failures");
return failures == 0 ? 0 : 1;

static uint Flag(string name) => TruePbrSchema.Features.First(f => f.Name == name).Mask;

static int Index(string name) => TruePbrSchema.Parameters.First(p => p.Name == name).Index;

static void Param(TruePbrSection s, string name, float value) => s.Parameters[Index(name)] = value;

static void Textures(TruePbrSection s, string baseColor, string normal, string emissive = "", string displacement = "",
    string rmaos = "", string features0 = "", string features1 = "")
{
    string[] paths = [baseColor, normal, emissive, displacement, rmaos, features0, features1];
    paths.CopyTo(s.Textures, 0);
}

static byte[] Payload(Action<BinaryWriter> write)
{
    using var stream = new MemoryStream();
    using (var writer = new BinaryWriter(stream, Encoding.UTF8, true))
        write(writer);
    return stream.ToArray();
}

static MaterialBlock Block(string tag, Action<BinaryWriter> write) => new(tag, Payload(write));

static byte[] Header(string tag, uint size) => [.. Encoding.ASCII.GetBytes(tag), .. BitConverter.GetBytes(size)];

static byte[] Concat(params object[] parts)
{
    var bytes = new List<byte>();
    foreach (var part in parts)
    {
        switch (part)
        {
            case MaterialBlock block: bytes.AddRange(block.ToBytes()); break;
            case byte[] raw: bytes.AddRange(raw); break;
        }
    }
    return bytes.ToArray();
}

static string Hex(float value) => BitConverter.SingleToUInt32Bits(value).ToString("x8");

static string Floats(IEnumerable<float> values) => string.Join(",", values.Select(Hex));

static string Bool(bool value) => value ? "1" : "0";

static (byte, uint, uint) Triple(BaseMaterialFile.AlphaBlendModeType mode) => mode switch
{
    BaseMaterialFile.AlphaBlendModeType.None => (0, 0u, 0u),
    BaseMaterialFile.AlphaBlendModeType.Standard => (1, 6u, 7u),
    BaseMaterialFile.AlphaBlendModeType.Additive => (1, 6u, 0u),
    BaseMaterialFile.AlphaBlendModeType.Multiplicative => (1, 4u, 1u),
    _ => (0, 6u, 7u),
};

// Part 1 of every fixture: a v2 body, version patched to 100 after the save.
byte[] Part1(Action<BGSM>? edit)
{
    var bgsm = new BGSM
    {
        DiffuseTexture = @"textures\architecture\rock_d.dds",
        NormalTexture = @"textures\architecture\rock_n.dds",
        SmoothSpecTexture = @"textures\architecture\rock_s.dds",
        EmitEnabled = true,
    };
    edit?.Invoke(bgsm);
    var path = Path.GetTempFileName();
    try
    {
        using (var file = File.Create(path))
            if (!bgsm.Save(file)) throw new InvalidOperationException("Part 1 save failed.");
        var bytes = File.ReadAllBytes(path);
        BitConverter.GetBytes(BGSM.TruePbrFileVersion).CopyTo(bytes, 4);
        return bytes;
    }
    finally { File.Delete(path); }
}

// Part 1 read back as a v2 material seeds the CSPB common fields.
TruePbrSection NewSection(byte[] part1)
{
    var v2 = (byte[])part1.Clone();
    BitConverter.GetBytes(2u).CopyTo(v2, 4);
    var path = Path.GetTempFileName();
    try
    {
        File.WriteAllBytes(path, v2);
        var bgsm = new BGSM();
        using var file = File.OpenRead(path);
        if (!bgsm.Open(file)) throw new InvalidOperationException("Part 1 read failed.");
        return TruePbrSection.FromMaterial(bgsm);
    }
    finally { File.Delete(path); }
}

string Emit(string name, byte[] bytes)
{
    var path = Path.Combine(root, name.Replace('\\', Path.DirectorySeparatorChar) + ".bgsm");
    Directory.CreateDirectory(Path.GetDirectoryName(path)!);
    File.WriteAllBytes(path, bytes);
    return name.Replace('\\', '/') + ".bgsm";
}

void Valid(string name, Action<TruePbrSection> edit, Action<BGSM>? part1 = null,
    Func<TruePbrSection, MaterialBlock[]>? blocks = null, Action<TruePbrSection>? configure = null)
{
    var head = Part1(part1);
    var section = NewSection(head);
    edit(section);
    configure?.Invoke(section);
    var list = blocks?.Invoke(section) ?? new[]
    {
        Block("CSPB", section.Write),
    }.Concat(section.ThinSurfaceOverride is null ? [] : [Block("CSTS", section.ThinSurfaceOverride.Write)])
     .Concat(section.FuzzOverride is null ? [] : [Block("CSFZ", section.FuzzOverride.Write)]).ToArray();
    var bytes = Concat([head, .. list]);
    var errors = TruePbrValidator.Validate(section);
    if (errors.Errors.Count > 0) { Console.WriteLine($"{name}: reference rejects: {errors.Errors[0]}"); failures++; return; }
    var file = Emit(name, bytes);
    rows.Add(Row(file, head.Length, bytes, errors.Warnings.Count));
}

void Invalid(string name, Action<TruePbrSection> edit)
{
    var head = Part1(null);
    var section = NewSection(head);
    Textures(section, Rock + "color.dds", Rock + "n.dds", rmaos: Rock + "rmaos.dds");
    edit(section);
    var result = TruePbrValidator.Validate(section);
    if (result.Errors.Count != 1) { Console.WriteLine($"{name}: expected one reference error, got {result.Errors.Count}"); failures++; return; }
    var text = result.Errors[0];
    var cut = text.IndexOf(" (is ", StringComparison.Ordinal);
    var bytes = Concat(head, Block("CSPB", section.Write));
    var file = Emit(@"invalid\" + name, bytes);
    rows.Add($"{file}\t{head.Length}\treject\t0\t{(cut >= 0 ? text[..cut] : text)}");
}

void Unreferenced(string name, Action<TruePbrSection> edit, string error)
{
    var head = Part1(null);
    var section = NewSection(head);
    Textures(section, Rock + "color.dds", Rock + "n.dds", rmaos: Rock + "rmaos.dds");
    edit(section);
    var file = Emit(@"invalid\" + name, Concat(head, Block("CSPB", section.Write)));
    rows.Add($"{file}	{head.Length}	reject	0	{error}");
}

void Raw(string name, Func<TruePbrSection, byte[], byte[]> build, bool reference = true)
{
    var head = Part1(null);
    var section = NewSection(head);
    Textures(section, Rock + "color.dds", Rock + "n.dds", rmaos: Rock + "rmaos.dds");
    var file = Emit(@"invalid\" + name, build(section, head));
    rows.Add($"{file}\t{head.Length}\treject\t0\t-");
}

// The reference reader's view of the written file, as stable text cells.
string Row(string file, int part2, byte[] bytes, int warnings)
{
    var path = Path.GetTempFileName();
    try
    {
        File.WriteAllBytes(path, bytes);
        var bgsm = new BGSM();
        using (var stream = File.OpenRead(path))
            if (!bgsm.Open(stream)) throw new InvalidOperationException($"{file}: reference reader rejects.");
        using (var stream = File.OpenRead(path))
        {
            stream.Position = part2;
            using var reader = new BinaryReader(stream);
            _ = MaterialBlock.ReadAll(reader).ToList(); // the offset really starts Part 2
        }

        var s = bgsm.TruePBR!;
        var c = s.Common;
        var (a, b, d) = Triple(c.AlphaBlendMode);
        var resolved = (float[])s.Parameters.Clone();
        if (s.ThinSurfaceOverride is { } t)
        {
            var at = Index("subsurfaceBalance");
            resolved[at] = t.SubsurfaceBalance;
            resolved[at + 1] = t.FrontSpecularWeight;
            resolved[at + 2] = t.BackSpecularWeight;
            resolved[at + 3] = t.BackRoughnessScale;
        }

        var cells = new List<string>
        {
            $"tileFlags={(c.TileU ? 2 : 0) | (c.TileV ? 1 : 0)}",
            $"uvOffset={Floats(c.UvOffset)}",
            $"uvScale={Floats(c.UvScale)}",
            $"alpha={Hex(c.Alpha)}",
            $"blend={a}/{b}/{d}",
            $"alphaTestRef={c.AlphaTestRef}",
            $"alphaTest={Bool(c.AlphaTest)}",
            $"refraction={Bool(c.Refraction)}",
            $"refractionFalloff={Bool(c.RefractionFalloff)}",
            $"refractionPower={Hex(c.RefractionPower)}",
            $"modelSpaceNormals={Bool(c.ModelSpaceNormals)}",
            $"emitEnabled={Bool(c.Emissive)}",
            $"emittanceColor={Floats(c.EmissiveColor)}",
            $"emittanceMult={Hex(c.EmissiveScale)}",
            $"externalEmittance={Bool(c.ExternalEmittance)}",
        };
        for (var i = 0; i < TruePbrSchema.Textures.Count; i++)
            cells.Add($"{TruePbrSchema.Textures[i].Name}={s.Textures[i]}");
        cells.Add($"payloadVersion={s.PayloadVersion}");
        cells.Add($"featureFlags={s.FeatureFlags}");
        cells.Add($"parameters={Floats(s.Parameters)}");
        cells.Add($"resolved={Floats(resolved)}");
        cells.Add("thin=" + (s.ThinSurfaceOverride is { } th
            ? Floats([th.SubsurfaceBalance, th.FrontSpecularWeight, th.BackSpecularWeight, th.BackRoughnessScale]) : "-"));
        cells.Add("fuzz=" + (s.FuzzOverride is { } fz ? $"{Hex(fz.Roughness)},{Bool(fz.RoughnessFromDisplacementGreen)}" : "-"));
        cells.Add("unknownTags=" + (bgsm.UnknownBlocks.Count == 0 ? "-" : string.Join(",", bgsm.UnknownBlocks.Select(x => x.Tag))));
        return $"{file}\t{part2}\tok\t{warnings}\t-\t{string.Join("\t", cells)}";
    }
    finally { File.Delete(path); }
}

static class BlockExtensions
{
    public static byte[] ToBytes(this MaterialBlock block)
    {
        using var stream = new MemoryStream();
        using (var writer = new BinaryWriter(stream, Encoding.UTF8, true))
            block.Write(writer);
        return stream.ToArray();
    }
}
