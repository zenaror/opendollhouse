using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.CSharp.Syntax;
using System.Reflection;
using System.Security.Cryptography;

// Compiles the actual handler body selected by the C# syntax tree. Collaborators
// are synthetic storage/UI boundaries; no copied handler or regex is the oracle.
static class EvictProgram
{
    static int Main(string[] args)
    {
        var root = args.Length == 0 ? Directory.GetCurrentDirectory() : Path.GetFullPath(args[0]);
        var sourcePath = Path.Combine(root, "refs/work/Simitone-linux/Client/Simitone/Simitone.Client/UI/Screens/TS1GameScreen.cs");
        var source = File.ReadAllText(sourcePath);
        var tree = CSharpSyntaxTree.ParseText(source);
        var screen = tree.GetRoot().DescendantNodes().OfType<ClassDeclarationSyntax>().Single(x => x.Identifier.Text == "TS1GameScreen");
        var actual = screen.Members.OfType<MethodDeclarationSyntax>().Single(x => x.Identifier.Text == "EvictLot").ToFullString();
        var original = File.ReadAllText(Path.Combine(AppContext.BaseDirectory, "EvictOriginal.txt"));
        if (SyntaxFactory.ParseMemberDeclaration(original) is not MethodDeclarationSyntax)
            throw new Exception("Invalid original method snapshot");
        var probe = Fixture.Replace("/*ACTUAL_METHOD*/", actual).Replace("/*ORIGINAL_METHOD*/", original);
        var references = ((string)AppContext.GetData("TRUSTED_PLATFORM_ASSEMBLIES")).Split(Path.PathSeparator)
            .Select(x => MetadataReference.CreateFromFile(x));
        var compilation = CSharpCompilation.Create("EvictActualSourceProbe",
            new[] { CSharpSyntaxTree.ParseText(probe) }, references,
            new CSharpCompilationOptions(OutputKind.DynamicallyLinkedLibrary));
        using var image = new MemoryStream();
        var result = compilation.Emit(image);
        if (!result.Success)
        {
            foreach (var diagnostic in result.Diagnostics.Where(x => x.Severity == DiagnosticSeverity.Error)) Console.Error.WriteLine(diagnostic);
            return 1;
        }
        try { Assembly.Load(image.ToArray()).GetType("Probe.Cases").GetMethod("Run").Invoke(null, null); }
        catch (TargetInvocationException exception) { Console.Error.WriteLine(exception.InnerException); return 1; }
        Console.WriteLine("Actual frontend SHA256 " + Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(sourcePath))).ToLowerInvariant());
        Console.WriteLine("Original method SHA256 " + Convert.ToHexString(SHA256.HashData(System.Text.Encoding.UTF8.GetBytes(original))).ToLowerInvariant());
        return 0;
    }

    private const string Fixture = """
using System;
using System.IO;
using System.Collections.Generic;
namespace Probe
{
    public sealed class FAMI { public int HouseNumber, Budget, ValueInArch; }
    public sealed class Services { public Provider Neighborhood; }
    public static class Content { public static Services Current; public static Services Get() => Current; }
    public sealed class Provider
    {
        public FAMI Family;
        public string Path;
        public bool HasHouse = true, Fail;
        public int Saves, CharacterWrites;
        public bool? LastWithSims;
        public readonly List<string> Events = new List<string>();
        public void MoveOut(short house)
        {
            if (!HasHouse || Family.HouseNumber != house) throw new Exception("Wrong house binding");
            Events.Add("move"); Family.HouseNumber = 0; HasHouse = false;
        }
        public bool SaveNeighbourhood(bool withSims)
        {
            Events.Add("save"); Saves++; LastWithSims = withSims;
            if (withSims) { CharacterWrites++; throw new Exception("Eviction must not flush character resources"); }
            if (Fail) throw new IOException("Synthetic neighborhood write failure");
            Persist(); return true;
        }
        public void Persist()
        {
            using (var writer = new BinaryWriter(File.Create(Path)))
            { writer.Write(Family.HouseNumber); writer.Write(Family.Budget); writer.Write(Family.ValueInArch); }
        }
        public int[] Reload()
        {
            using (var reader = new BinaryReader(File.OpenRead(Path)))
                return new[] { reader.ReadInt32(), reader.ReadInt32(), reader.ReadInt32() };
        }
    }
    public sealed class Panel { public Action<short> OnSelect; public void SelectHouse(short house) => OnSelect(house); }
    public sealed class ActualScreen { public Panel TS1NeighPanel; /*ACTUAL_METHOD*/ }
    public sealed class OriginalScreen { public Panel TS1NeighPanel; /*ORIGINAL_METHOD*/ }
    public static class Cases
    {
        static int Passed;
        static void Check(bool value, string reason) { if (!value) throw new Exception(reason); }
        static void Case(string name, Action run) { run(); Passed++; Console.WriteLine("PASS " + name); }
        public static void Run()
        {
            var directory = System.IO.Path.Combine(System.IO.Path.GetTempPath(), "odh-evict-source-" + Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(directory);
            try
            {
                Case("original handler leaves old assignment persisted", () =>
                {
                    var provider = Setup(directory, 3500); bool selected = false;
                    var screen = new OriginalScreen { TS1NeighPanel = new Panel { OnSelect = house => selected = true } };
                    screen.EvictLot(provider.Family, 9);
                    var disk = provider.Reload();
                    Check(selected && provider.Family.HouseNumber == 0 && disk[0] == 9 && provider.Saves == 0,
                        "Original handler no longer reproduces non-durable move-out");
                });
                foreach (int value in new[] { 3500, 0 })
                    Case("actual handler persists move-out/refund before UI refresh, ValueInArch=" + value, () =>
                    {
                        var provider = Setup(directory, value); bool selected = false;
                        var screen = new ActualScreen { TS1NeighPanel = new Panel { OnSelect = house =>
                        {
                            var disk = provider.Reload();
                            Check(house == 9 && disk[0] == 0 && disk[1] == 16500 + value && disk[2] == 0,
                                "Refresh occurred before complete eviction state was persisted");
                            Check(!provider.HasHouse, "House map not removed before persistence");
                            provider.Events.Add("select"); selected = true;
                        } } };
                        screen.EvictLot(provider.Family, 9);
                        Check(selected && provider.Saves == 1 && provider.LastWithSims == false && provider.CharacterWrites == 0,
                            "Incorrect persistence boundary");
                        Check(string.Join(",", provider.Events) == "move,save,select", "Incorrect operation ordering");
                        var disk = provider.Reload();
                        Check(disk[0] == 0 && disk[1] == 16500 + value, "Reload lost eviction state");
                    });
                Case("write failure propagates before UI refresh; no new rollback policy", () =>
                {
                    var provider = Setup(directory, 3500); provider.Fail = true; bool selected = false;
                    var screen = new ActualScreen { TS1NeighPanel = new Panel { OnSelect = house => selected = true } };
                    bool failed = false;
                    try { screen.EvictLot(provider.Family, 9); } catch (IOException) { failed = true; }
                    Check(failed && !selected && string.Join(",", provider.Events) == "move,save", "Failure swallowed or UI refreshed");
                    Check(provider.Family.HouseNumber == 0 && provider.Family.Budget == 20000 && provider.Family.ValueInArch == 0,
                        "Unexpected rollback or refund change");
                    var disk = provider.Reload(); Check(disk[0] == 9 && disk[1] == 16500, "Failed synthetic write changed disk");
                });
                Console.WriteLine("PASS " + Passed + " actual-handler eviction groups; synthetic storage collaborators, no game profile.");
            }
            finally { Content.Current = null; Directory.Delete(directory, true); }
        }
        static Provider Setup(string directory, int value)
        {
            var provider = new Provider { Family = new FAMI { HouseNumber = 9, Budget = 16500, ValueInArch = value },
                Path = System.IO.Path.Combine(directory, "synthetic-neighborhood-state.bin") };
            provider.Persist(); Content.Current = new Services { Neighborhood = provider }; return provider;
        }
    }
}
""";
}
