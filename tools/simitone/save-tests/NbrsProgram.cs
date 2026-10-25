using System;
using System.IO;
using System.Collections.Generic;
using System.Linq;
using Actual = FSO.Files.Formats.IFF.Chunks.NBRS;
using Neighbor = FSO.Files.Formats.IFF.Chunks.Neighbour;
using Original = FSO.Files.Formats.IFF.Chunks.Baseline.NBRS;

internal static class NbrsProgram
{
    private static int groups;
    private static void Check(bool condition, string message) { if (!condition) throw new Exception(message); }
    private static void Group(string name, Action test) { test(); groups++; Console.WriteLine("PASS " + name); }

    // Synthetic input builder only; it is never the parsing oracle.
    private static byte[] Fixture(int placeholders, params short[] ids)
    {
        using (var stream = new MemoryStream())
        using (var writer = new BinaryWriter(stream))
        {
            writer.Write(0); writer.Write(0x49); writer.Write(new byte[] { 83, 82, 66, 78 });
            writer.Write(placeholders + ids.Length);
            for (int i = 0; i < ids.Length; i++)
            {
                writer.Write(1); writer.Write(0xA); writer.Write(9);
                string name = "sim" + ids[i];
                writer.Write(System.Text.Encoding.ASCII.GetBytes(name)); writer.Write((byte)0);
                if (name.Length % 2 == 0) writer.Write((byte)0);
                writer.Write(0); writer.Write(0); // MysteryZero, PersonMode
                writer.Write(ids[i]); writer.Write((uint)(0x12340000 + ids[i])); writer.Write(-1);
                writer.Write(1); writer.Write(1); writer.Write(777); writer.Write(2); writer.Write(11); writer.Write(-5);
                if (i < placeholders) writer.Write(0);
            }
            for (int i = ids.Length; i < placeholders; i++) writer.Write(0);
            return stream.ToArray();
        }
    }
    private static Actual Read(byte[] bytes)
    {
        var result = new Actual();
        using (var stream = new MemoryStream(bytes))
        {
            result.Read(null, stream);
            Check(stream.Position == stream.Length, "Unread NBRS bytes");
            Check(stream.CanRead, "Reader closed caller stream");
        }
        return result;
    }
    private static Original ReadOriginal(byte[] bytes)
    {
        var result = new Original();
        using (var stream = new MemoryStream(bytes)) result.Read(null, stream);
        return result;
    }
    private static byte[] Write(Actual input)
    {
        using (var stream = new MemoryStream()) { input.Write(null, stream); Check(stream.CanWrite, "Writer closed caller stream"); return stream.ToArray(); }
    }
    private static byte[] WriteOriginal(Original input)
    {
        using (var stream = new MemoryStream()) { input.Write(null, stream); return stream.ToArray(); }
    }
    private static int Declared(byte[] bytes) { return BitConverter.ToInt32(bytes, 12); }
    private static string Ids(Actual input) { return string.Join(",", input.NeighbourByID.Keys.OrderBy(x => x)); }
    private static string ActiveState(Actual input)
    {
        return string.Join("|", input.NeighbourByID.Values.OrderBy(x => x.NeighbourID).Select(n =>
            n.NeighbourID + ":" + n.GUID + ":" + n.Name + ":" + n.Version + ":" + n.PersonMode + ":" +
            (n.PersonData == null ? "none" : string.Join(",", n.PersonData)) + ":" +
            string.Join(";", n.Relationships.OrderBy(x => x.Key).Select(x => x.Key + "=" + string.Join(",", x.Value)))));
    }
    private static Neighbor NewNeighbor(short id, uint guid)
    {
        return new Neighbor { NeighbourID = id, GUID = guid, Name = "new", PersonMode = 9,
            PersonData = Enumerable.Range(0, 88).Select(x => (short)(x - 44)).ToArray(),
            Relationships = new Dictionary<int, List<short>> { { 3, new List<short> { -1, 2, 127 } } } };
    }
    private static void Main()
    {
        Group("original writer reproduces inflated header with omitted holes", () =>
        {
            var original = ReadOriginal(Fixture(16, Enumerable.Range(1, 33).Select(x => (short)x).ToArray()));
            byte[] broken = WriteOriginal(original);
            var reread = ReadOriginal(broken);
            Check(Declared(broken) == 49 && reread.Entries.Count == 33 && reread.NeighbourByID.Count == 33, "Baseline no longer reproduces original defect");
        });
        Group("empty NBRS and placeholder-only NBRS are valid round trips", () =>
        {
            foreach (int holes in new[] { 0, 1, 16, 256 })
            {
                var before = Read(Fixture(holes)); byte[] bytes = Write(before); var after = Read(bytes);
                Check(Declared(bytes) == holes && after.Entries.Count == holes && after.NeighbourByID.Count == 0, "Placeholder count lost");
                Check(bytes.Length == 16 + holes * 4 && after.GetFreeID() == 1, "Placeholder encoding/ID allocation");
                Check(after.Entries.All(x => x.Unknown1 == 0), "Placeholder marker changed");
            }
        });
        Group("active-only output remains byte-identical to original writer", () =>
        {
            byte[] input = Fixture(0, 1, 3, 4);
            Check(Convert.ToBase64String(Write(Read(input))) == Convert.ToBase64String(WriteOriginal(ReadOriginal(input))), "Active record writer changed");
        });
        Group("holes, IDs, GUID index, relationships and runtime indices survive reload", () =>
        {
            var before = Read(Fixture(16, 1, 3, 4)); string active = ActiveState(before);
            var indices = before.NeighbourByID.ToDictionary(x => x.Key, x => x.Value.RuntimeIndex);
            var after = Read(Write(before));
            Check(after.Entries.Count == 19 && after.Entries.Count(x => x.Unknown1 == 0) == 16, "Holes lost");
            Check(ActiveState(after) == active && Ids(after) == "1,3,4", "Active identities/data changed");
            foreach (var pair in after.NeighbourByID)
            {
                Check(pair.Value.RuntimeIndex == indices[pair.Key], "Runtime continuation index changed");
                Check(after.DefaultNeighbourByGUID[pair.Value.GUID] == pair.Key, "GUID index changed");
            }
            Check(before.GetFreeID() == 2 && after.GetFreeID() == 2, "Reload changed first free neighbor ID");
        });
        Group("AddNeighbor fills actual ID hole and never reassigns existing IDs", () =>
        {
            var before = Read(Fixture(16, 1, 3, 4)); var original = ReadOriginal(Fixture(16, 1, 3, 4));
            short next = before.GetFreeID(); Check(next == original.GetFreeID() && next == 2, "Allocator baseline mismatch");
            before.AddNeighbor(NewNeighbor(next, 0x76543210)); var after = Read(Write(before));
            Check(Ids(after) == "1,2,3,4" && after.GetFreeID() == 5, "New ID/reload allocation mismatch");
            Check(after.NeighbourByID[2].GUID == 0x76543210 && after.NeighbourByID[1].GUID == 0x12340001, "Existing ID rebound");
            Check(after.Entries.Count == 20 && after.NeighbourByID[2].PersonData[61] == 17, "New neighbor data lost");
            for (int i = 0; i < 10; i++) after = Read(Write(after));
            Check(Ids(after) == "1,2,3,4" && after.GetFreeID() == 5 && after.Entries.Count == 20, "Repeated reload changed allocation");
        });
        Group("synthetic 289-slot case emits 273 active plus 16 placeholders", () =>
        {
            var input = Read(Fixture(16, Enumerable.Range(1, 33).Select(x => (short)x).ToArray()));
            for (uint i = 0; i < 240; i++) input.AddNeighbor(NewNeighbor(input.GetFreeID(), 0x60000000 + i));
            string active = ActiveState(input); byte[] bytes = Write(input); var after = Read(bytes);
            Check(Declared(bytes) == 289 && after.Entries.Count == 289 && after.NeighbourByID.Count == 273, "289-slot case malformed");
            Check(after.Entries.Count(x => x.Unknown1 == 0) == 16 && after.GetFreeID() == 274, "Placeholder or new ID semantics changed");
            Check(ActiveState(after) == active, "Large round trip changed active state");
        });
        Group("old malformed header re-emits only known records without inventing holes or IDs", () =>
        {
            var old = ReadOriginal(Fixture(16, Enumerable.Range(1, 273).Select(x => (short)x).ToArray()));
            byte[] broken = WriteOriginal(old);
            Check(Declared(broken) == 289, "Malformed baseline fixture mismatch");
            var loaded = Read(broken);
            Check(loaded.Entries.Count == 273 && loaded.NeighbourByID.Count == 273 && loaded.GetFreeID() == 274,
                "Legacy lenient read changed known neighbor allocation");
            string active = ActiveState(loaded);
            byte[] corrected = Write(loaded);
            var reloaded = Read(corrected);
            Check(Declared(corrected) == 273 && reloaded.Entries.Count == 273 && ActiveState(reloaded) == active,
                "Writer invented missing slots or changed known records");
            reloaded.AddNeighbor(NewNeighbor(reloaded.GetFreeID(), 0x76543210));
            var next = Read(Write(reloaded));
            Check(next.NeighbourByID[274].GUID == 0x76543210 && next.GetFreeID() == 275 &&
                next.NeighbourByID[273].GUID == 0x12340111, "Next addition reused/reassigned an existing ID");
        });
        Group("writer does not mutate entries, indices or dictionaries", () =>
        {
            var input = Read(Fixture(3, 2, 5)); var entries = input.Entries.ToArray(); string state = ActiveState(input);
            Write(input);
            Check(input.Entries.Count == entries.Length && input.NeighbourByID.Count == 2 && ActiveState(input) == state, "Writer mutated state");
            for (int i = 0; i < entries.Length; i++) Check(object.ReferenceEquals(entries[i], input.Entries[i]) && entries[i].RuntimeIndex == i, "Writer moved entries");
        });
        Console.WriteLine("PASS " + groups + " NBRS actual-source groups; synthetic data only.");
    }
}
