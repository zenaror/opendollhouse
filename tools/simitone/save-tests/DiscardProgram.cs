using System.Reflection;
using System.Runtime.CompilerServices;
using FSO.Common;
using FSO.Content;
using FSO.Content.Model;
using FSO.Content.Interfaces;
using FSO.Content.TS1;
using FSO.Files.Formats.IFF;
using FSO.Files.Formats.IFF.Chunks;

sealed class DiscardCacheProbe : TS1ObjectProvider
{
    public DiscardCacheProbe() : base(null, null) { Entries = new(); }
    public void SeedWrapper(uint id, GameObject value) => Cache.GetOrAdd(id, _ => value);
    public void SeedResource(string path, GameObjectResource value) => ProcessedFiles.GetOrAdd(path, _ => value);
    public GameObject Wrapper(uint id) => Cache.GetOrAdd(id, _ => null);
    public GameObjectResource Resource(string path) => ProcessedFiles.GetOrAdd(path, _ => null);
}

static class DiscardProgram
{
    static int Passed;
    static void Assert(bool ok, string reason) { if (!ok) throw new Exception(reason); }
    static void Case(string name, Action run) { run(); Passed++; Console.WriteLine("PASS " + name); }
    static GameObjectResource Opaque(string name) => new(null, null, null, name, null);
    static GameObjectReference Register(DiscardCacheProbe provider, uint id, string filename, GameObjectSource source)
    {
        var entry = new GameObjectReference(provider) { ID = id, FileName = filename, Source = source, Name = filename };
        provider.Entries[id] = entry; return entry;
    }
    static T Ready<T>(T chunk, string type, ushort id = 1) where T : IffChunk
    { chunk.ChunkType = type; chunk.ChunkID = id; chunk.ChunkLabel = "synthetic"; chunk.ChunkProcessed = true; return chunk; }
    static void Iff(string path, params IffChunk[] chunks)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        var iff = new IffFile(); foreach (var chunk in chunks) iff.AddChunk(chunk);
        using var stream = File.Create(path); iff.Write(stream);
    }
    static void Character(string path, uint guid)
    { Iff(path, Ready(new OBJD { GUID = guid, ObjectType = OBJDType.Person }, "OBJD")); }
    static void Neighborhood(string path, int budget, uint guid)
    {
        Directory.CreateDirectory(Path.Combine(path, "Characters"));
        Iff(Path.Combine(path, "Neighborhood.iff"), Ready(new NBRS(), "NBRS"), Ready(new NGBH(), "NGBH"),
            Ready(new TATT(), "TATT"), Ready(new FAMI { HouseNumber = 7, Budget = budget }, "FAMI"));
        Iff(Path.Combine(path, "LotLocations.iff"));
        var zones = new STR(); zones.LanguageSets[0].Strings = new[] { new STRItem { LanguageCode = 1, Value = "7, residential", Comment = "" } };
        Iff(Path.Combine(path, "LotZoning.iff"), Ready(zones, "STR#"));
        Iff(Path.Combine(path, "StreetNames.iff"));
        foreach (var name in new[] { "NeighborhoodDesc", "STDesc", "MTDesc" }) Iff(Path.Combine(path, "Houses", name + ".iff"));
        // Must not cause LoadCharacters to return before the valid uppercase IFF.
        File.WriteAllText(Path.Combine(path, "Characters", "notes.txt"), "synthetic non-game file");
        Character(Path.Combine(path, "Characters", "User00012.IFF"), guid);
    }
    static void Main()
    {
        Case("ClearUserCharacters evicts user GUID/resource caches while retaining FAR, standalone and catalog", () =>
        {
            var provider = new DiscardCacheProbe();
            const string userFile = "/synthetic/profile/UserData3/Characters/User00012.IFF";
            Register(provider, 101, userFile, GameObjectSource.User); Register(provider, 102, userFile, GameObjectSource.User);
            var far = Register(provider, 201, "people.iff", GameObjectSource.Far);
            var standalone = Register(provider, 202, "/synthetic/Downloads/thing.iff", GameObjectSource.Standalone);
            var clone = Register(provider, 203, "patched.iff", GameObjectSource.PIFFClone);
            var userResource = Opaque(userFile); var farResource = Opaque(far.FileName); var standaloneResource = Opaque(standalone.FileName);
            var userObject = new GameObject { GUID = 101 }; var farObject = new GameObject { GUID = 201 }; var standaloneObject = new GameObject { GUID = 202 };
            provider.SeedWrapper(101, userObject); provider.SeedWrapper(102, new GameObject { GUID = 102 });
            provider.SeedWrapper(201, farObject); provider.SeedWrapper(202, standaloneObject);
            provider.SeedResource(userFile, userResource); provider.SeedResource(far.FileName, farResource); provider.SeedResource(standalone.FileName, standaloneResource);
            provider.PersonGUIDs.UnionWith(new uint[] { 101, 102, 201 }); provider.ControllerObjects.Add(far);
            var catalog = new Dictionary<uint, ObjectCatalogItem> { [201] = new ObjectCatalogItem { GUID = 201, Price = 345 } };
            typeof(TS1ObjectProvider).GetField("ItemsByGUID", BindingFlags.NonPublic | BindingFlags.Static)!.SetValue(null, catalog);
            provider.ClearUserCharacters();
            Assert(!provider.Entries.ContainsKey(101) && !provider.Entries.ContainsKey(102)
                && provider.Wrapper(101) == null && provider.Wrapper(102) == null && provider.Resource(userFile) == null,
                "discarded user wrappers or mutable IFF resource remain cached");
            Assert(provider.PersonGUIDs.SetEquals(new uint[] { 201 }), "stale person GUIDs remained or FAR person was removed");
            Assert(provider.Entries[201] == far && provider.Entries[202] == standalone && provider.Entries[203] == clone,
                "non-user registry entries changed");
            Assert(provider.Wrapper(201) == farObject && provider.Wrapper(202) == standaloneObject
                && provider.Resource(far.FileName) == farResource && provider.Resource(standalone.FileName) == standaloneResource,
                "FAR/standalone cached resources were evicted");
            Assert(provider.GetItemByGUID(201)?.Price == 345 && provider.ControllerObjects.Single() == far,
                "catalog or controller indexes changed");
            provider.ClearUserCharacters();
            Assert(provider.Entries.Count == 3, "repeated discard changed non-user definitions");
        });
        Case("ReloadCurrent restores disk budget, current slot/exact path, and newly indexed user GUID", () =>
        {
            var temporary = Path.Combine(Path.GetTempPath(), "odh-discard-synthetic-" + Guid.NewGuid().ToString("N"));
            var previousUserDir = FSOEnvironment.UserDir; var previousTarget = IffFile.TargetTS1;
            try
            {
                IffFile.TargetTS1 = true;
                var userRoot = Path.Combine(temporary, "profile");
                Neighborhood(Path.Combine(userRoot, "UserData"), 111, 0xa000);
                var selectedPath = Path.Combine(userRoot, "UserData3"); Neighborhood(selectedPath, 665, 0xb000);
                FSOEnvironment.UserDir = userRoot;
                // Content is only a service holder for WorldObjects; its runtime
                // constructor loads game resources. No other field/method is used.
                var content = (Content)RuntimeHelpers.GetUninitializedObject(typeof(Content));
                var provider = new DiscardCacheProbe(); content.WorldObjects = provider;
                var current = new TS1NeighborhoodProvider(content); current.InitSpecific(2);
                Assert(current.CurrentNeighborhoodID == 2 && current.FamilyForHouse[7].Budget == 665,
                    "fixture did not select UserData3 independently of map mode");
                Assert(provider.Entries.ContainsKey(0xb000) && current.NextSim == 13,
                    "uppercase IFF or non-IFF directory entry prevented character indexing");
                var oldFile = provider.Entries[0xb000].FileName; var stale = Opaque(oldFile);
                provider.SeedResource(oldFile, stale); provider.SeedWrapper(0xb000, new GameObject { GUID = 0xb000 });
                var oldState = current.GameState; var oldFamily = current.FamilyForHouse[7];
                oldFamily.Budget = 660; oldState.ActiveFamily = oldFamily; current.DirtyAvatars.Add(0xb000);
                // A retained path must win over both its conventional directory
                // name and a later change to the profile-root configuration.
                var exactPath = Path.Combine(temporary, "exact-selected-user-path"); Directory.Move(selectedPath, exactPath); current.UserPath = exactPath;
                var decoyRoot = Path.Combine(temporary, "decoy"); Neighborhood(Path.Combine(decoyRoot, "UserData3"), 999, 0xd000);
                FSOEnvironment.UserDir = decoyRoot;
                Character(Path.Combine(exactPath, "Characters", "User00012.IFF"), 0xc000);
                current.ReloadCurrent();
                Assert(current.CurrentNeighborhoodID == 2 && current.UserPath == exactPath && current.FamilyForHouse[7].Budget == 665,
                    "reload used a map mode/recomputed path, or retained discarded family budget");
                Assert(!ReferenceEquals(current.GameState, oldState) && current.GameState.ActiveFamily == null
                    && !ReferenceEquals(current.FamilyForHouse[7], oldFamily) && current.DirtyAvatars.Count == 0,
                    "cross-lot family/dirty state survived discard");
                Assert(!provider.Entries.ContainsKey(0xb000) && !provider.PersonGUIDs.Contains(0xb000)
                    && provider.Wrapper(0xb000) == null && provider.Resource(oldFile) == null,
                    "old user GUID or IFF cache survived file reload");
                Assert(provider.Entries.ContainsKey(0xc000) && provider.PersonGUIDs.Contains(0xc000)
                    && provider.Entries[0xc000].FileName == Path.Combine(exactPath, "Characters", "User00012.IFF")
                    && current.NextSim == 13, "new file GUID was not reindexed at the retained path");
            }
            finally { FSOEnvironment.UserDir = previousUserDir; IffFile.TargetTS1 = previousTarget; if (Directory.Exists(temporary)) Directory.Delete(temporary, true); }
        });
        Console.WriteLine($"Discard reload: {Passed} passed. Actual source-linked providers, real cache/base/IFF types, synthetic records; no full UI or commercial data.");
    }
}
