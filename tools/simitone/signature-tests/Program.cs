using FSO.Files.Formats.IFF.Chunks;
using FSO.SimAntics.Utils;

static BHAV Tree(uint? versionWord, params ushort[] calls) => new BHAV
{
    NativeVersionWord = versionWord, Instructions = calls.Select(op => new BHAVInstruction { Opcode = op }).ToArray()
};
static void Check(string name, uint expected, ushort root, Dictionary<ushort, BHAV> trees,
    params ushort[] expectedLookups)
{
    var lookups = new List<ushort>();
    var actual = VMTS1CodeSignature.Compute(root, id =>
    {
        lookups.Add(id);
        return trees.GetValueOrDefault(id);
    });
    if (actual != expected) throw new Exception($"{name}: expected {expected:x8}, got {actual:x8}");
    if (expectedLookups.Length != 0 &&
        !lookups.Order().SequenceEqual(expectedLookups.Order()))
        throw new Exception($"{name}: resolution calls differed: {string.Join(',', lookups)}");
    Console.WriteLine($"PASS {name}: {actual:x8}");
}

// Hand-calculated cases from native (rootWord << 16) + sum(unique reachable uint32 words).
Check("root leaf", 0x00050005u, 4096, new() { [4096] = Tree(5) }, 4096);
Check("cycle and duplicate call", 0x00020005u, 4096,
    new() { [4096] = Tree(2, 4097, 4097), [4097] = Tree(3, 4096) }, 4096, 4097);
Check("supplied resolver crosses scopes", 0x0004000eu, 4096,
    new() { [4096] = Tree(4, 257, 8192), [257] = Tree(2), [8192] = Tree(8) }, 4096, 257, 8192);
Check("missing root", 0u, 4096, new() { [257] = Tree(100) }, 4096);
Check("missing dependency visited once", 0x00060006u, 4096,
    new() { [4096] = Tree(6, 4097, 4097) }, 4096, 4097);
Check("opcode flag masked and primitives excluded", 0x00070015u, 8192,
    new() { [8192] = Tree(7, 0x8000, 0x8001, 0x00ff, 0x8100, 0x9000),
            [256] = Tree(11), [4096] = Tree(3) }, 8192, 256, 4096);
Check("uint32 wrap", 0u, 4096,
    new() { [4096] = Tree(0xffffffffu, 4097), [4097] = Tree(0x00010001u) }, 4096, 4097);
Check("root high16 retained in sum", 0x12360002u, 4096,
    new() { [4096] = Tree(0x12340002u) }, 4096);
Check("dependency high16 retained in sum", 0x01020005u, 4096,
    new() { [4096] = Tree(2, 4097), [4097] = Tree(0x01000003u) }, 4096, 4097);
foreach (var unsupportedRoot in new[] { true, false })
{
    var unsupported = unsupportedRoot
        ? new Dictionary<ushort, BHAV> { [4096] = Tree(null) }
        : new Dictionary<ushort, BHAV> { [4096] = Tree(2, 4097), [4097] = Tree(null) };
    try
    {
        VMTS1CodeSignature.Compute(4096, id => unsupported.GetValueOrDefault(id));
        throw new Exception("Unsupported native BHAV header accepted");
    }
    catch (NotSupportedException)
    {
        Console.WriteLine($"PASS unsupported {(unsupportedRoot ? "root" : "dependency")} header rejected");
    }
}
try { VMTS1CodeSignature.Compute(4096, null); throw new Exception("null resolver accepted"); }
catch (ArgumentNullException) { Console.WriteLine("PASS null resolver rejected"); }
HeaderTests.Run();
Console.WriteLine("12 graph cases and 5 real-parser cases passed; reset policy is not tested.");

namespace FSO.Files.Formats.IFF.Chunks
{
    public class BHAV { public uint? NativeVersionWord; public BHAVInstruction[] Instructions = Array.Empty<BHAVInstruction>(); }
    public class BHAVInstruction { public ushort Opcode; }
}
