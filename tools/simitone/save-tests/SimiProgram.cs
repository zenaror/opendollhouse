using System.Buffers.Binary;
using FSO.Files.Formats.IFF.Chunks;
using FSO.Files.Utils;

static class SimiProgram
{
    static int Passed, Failed;
    static void Assert(bool ok, string message) { if (!ok) throw new Exception(message); }
    static void Case(string name, Action run)
    {
        try { run(); Passed++; Console.WriteLine("PASS " + name); }
        catch (Exception error) { Failed++; Console.WriteLine("FAIL " + name + ": " + error.Message); }
    }
    static SIMI Fixture(uint version)
    {
        return new SIMI
        {
            Version = version,
            GlobalData = Enumerable.Range(0, 38).Select(i => (short)(i % 2 == 0 ? 1000 + i : -1000 - i)).ToArray(),
            Unknown1 = -123, Unknown2 = 0x12345678, Unknown3 = -7654321,
            GUID1 = 0x13572468, GUID2 = -123456789, Unknown4 = 404,
            LotValue = 12345, ObjectsValue = 23456, ArchitectureValue = 34567,
            BudgetDays = Enumerable.Range(0, 6).Select(i => i % 3 == 2 ? new SIMI.SIMIBudgetDay() : new SIMI.SIMIBudgetDay
            {
                Valid = 1, MiscIncome = 11 + i, JobIncome = 22 + i,
                ServiceExpense = 33 + i, FoodExpense = 44 + i, BillsExpense = 55 + i,
                MiscExpense = 66 + i, HouseholdExpense = 77 + i, ArchitectureExpense = 88 + i
            }).ToArray()
        };
    }
    static byte[] Write(SIMI simi)
    {
        using var output = new MemoryStream();
        Assert(simi.Write(null, output), "writer did not report success");
        return output.ToArray();
    }
    static int[] Fields(SIMI simi) => new[] { (int)simi.Unknown1, simi.Unknown2, simi.Unknown3, simi.GUID1,
        simi.GUID2, simi.Unknown4, simi.LotValue, simi.ObjectsValue, simi.ArchitectureValue };
    static int[] Budget(SIMI.SIMIBudgetDay day) => new[] { day.Valid, day.MiscIncome, day.JobIncome,
        day.ServiceExpense, day.FoodExpense, day.BillsExpense, day.MiscExpense, day.HouseholdExpense, day.ArchitectureExpense };
    static void RoundTrip(uint version)
    {
        var original = Fixture(version); var globals = original.GlobalData;
        var globalCopy = globals.ToArray(); var fields = Fields(original);
        var budgets = original.BudgetDays.Select(Budget).ToArray();
        var bytes = Write(original);
        Assert(ReferenceEquals(original.GlobalData, globals), "Write replaced the live GlobalData array");
        Assert(original.GlobalData.SequenceEqual(globalCopy), "Write modified live GlobalData bytes");
        Assert(original.Version == version, "Write changed the live Version field");
        Assert(Fields(original).SequenceEqual(fields), "Write changed live scalar fields");
        Assert(original.BudgetDays.Select(Budget).Zip(budgets).All(x => x.First.SequenceEqual(x.Second)), "Write changed live budget fields");

        // The reader's established schema switches to 64 words at version0x40.
        // All38 runtime globals require that schema, including source version0.
        var expectedVersion = Math.Max(0x40u, version);
        Assert(BinaryPrimitives.ReadUInt32LittleEndian(bytes.AsSpan(4, 4)) == expectedVersion, "output version did not select the non-lossy schema");
        var expectedLength = 12 + 64 * 2 + 2 + 8 * 4
            + original.BudgetDays.Sum(day => day.Valid == 0 ? 4 : 36);
        Assert(bytes.Length == expectedLength, "header and serialized global count disagree");
        for (var i = 0; i < 38; i++)
            Assert(BinaryPrimitives.ReadInt16LittleEndian(bytes.AsSpan(12 + i * 2, 2)) == globalCopy[i], "serialized global word lost: " + i);
        for (var i = 38; i < 64; i++)
            Assert(BinaryPrimitives.ReadInt16LittleEndian(bytes.AsSpan(12 + i * 2, 2)) == 0, "unused wire word was not padded: " + i);

        using var input = new MemoryStream(); input.Write(bytes);
        // An adjacent record must remain untouched by the real SIMI reader.
        using (var writer = new BinaryWriter(input, System.Text.Encoding.UTF8, true)) writer.Write(0x5a39b17du);
        input.Position = 0; var restored = new SIMI(); restored.Read(null, input);
        Assert(input.Position == bytes.Length, "reader did not stop at the exact end of SIMI");
        using (var reader = new BinaryReader(input, System.Text.Encoding.UTF8, true))
            Assert(reader.ReadUInt32() == 0x5a39b17du && input.Position == input.Length, "adjacent record alignment changed");
        Assert(restored.Version == expectedVersion, "read version changed");
        Assert(restored.GlobalData.SequenceEqual(globalCopy) && restored.GlobalData[37] == globalCopy[37], "roundtrip lost globals, including word37");
        Assert(Fields(restored).SequenceEqual(fields), "roundtrip changed scalar fields");
        Assert(restored.BudgetDays.Select(Budget).Zip(budgets).All(x => x.First.SequenceEqual(x.Second)), "roundtrip changed budget fields");
        Assert(Write(original).SequenceEqual(bytes) && Write(restored).SequenceEqual(bytes), "repeated or post-read writes are not deterministic");
    }
    static void Main()
    {
        foreach (var version in new uint[] { 0, 0x3e, 0x3f, 0x40, 0x45 })
            Case($"real SIMI roundtrip input version0x{version:x2}: globals38/budget/alignment/immutability/repeat", () => RoundTrip(version));
        Case("budget writer keeps MiscExpense distinct from MiscIncome", () =>
        {
            var original = Fixture(0x40).BudgetDays[0];
            using var output = new MemoryStream();
            using (var writer = IoWriter.FromStream(output, ByteOrder.LITTLE_ENDIAN)) original.Write(writer);
            output.Position = 0;
            using var reader = IoBuffer.FromStream(output, ByteOrder.LITTLE_ENDIAN);
            var restored = new SIMI.SIMIBudgetDay(reader);
            Assert(restored.MiscIncome == 11 && restored.MiscExpense == 66, "MiscExpense was replaced by MiscIncome");
            Assert(Budget(restored).SequenceEqual(Budget(original)), "budget roundtrip changed another field");
        });
        Case("writer pads absent optional globals without changing source reference", () =>
        {
            var source = Fixture(0); source.GlobalData = null;
            var bytes = Write(source);
            Assert(source.GlobalData == null && source.Version == 0, "writer allocated live globals or changed version");
            var restored = new SIMI(); restored.Read(null, new MemoryStream(bytes));
            Assert(restored.GlobalData.Length == 38 && restored.GlobalData.All(x => x == 0), "absent globals did not zero-pad");
        });
        Console.WriteLine($"SIMI serializer: {Passed} passed, {Failed} failed. Synthetic records; actual linked SIMI reader/writer and real FSO.Files base/IO types.");
        Environment.ExitCode = Failed == 0 ? 0 : 1;
    }
}
