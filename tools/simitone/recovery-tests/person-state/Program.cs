using FSO.Files.Formats.IFF.Chunks;
using FSO.SimAntics.Utils;

static class Program
{
    private static int Passed;
    static void Assert(bool condition, string message)
    { if (!condition) throw new Exception(message); }
    static void Test(string name, Action run)
    { run(); Passed++; Console.WriteLine("PASS " + name); }
    static OBJMInteraction Action(int uid, short target, short icon, int tta = 12) => new()
    {
        UID = uid, CallerID = 31, TargetID = target, Icon = icon, TTAIndex = tta,
        Args = new short[] { -3, 17, 254, short.MinValue }, Priority = 200000,
        ActionTreeID = 4137, Attenuation = 0.625f,
        Flags = OBJMInteractionFlags.UserInterrupted | OBJMInteractionFlags.CarryNameOver
    };
    static OBJMPerson Fixture() => new()
    {
        ObjectUses = new[] {
            new OBJMObjectUse { TargetID = 110, StackLength = 3, Unknown2 = 1 },
            new OBJMObjectUse { TargetID = 111, StackLength = 8, Unknown2 = 0 },
            new OBJMObjectUse { TargetID = 110, StackLength = 11, Unknown2 = 0 } },
        ActiveInteraction = Action(10001, 120, 121),
        LastInteraction = Action(10002, 130, 131),
        InteractionQueue = new[] { Action(10003, 140, 141), Action(10004, 150, 151) }
    };
    static void CheckInteraction(VMTS1SavedPersonRecoveryState.Interaction snapshot, OBJMInteraction original)
    {
        Assert(snapshot.UID == original.UID && snapshot.CallerID == original.CallerID
            && snapshot.TargetID == original.TargetID && snapshot.Icon == original.Icon
            && snapshot.TTAIndex == original.TTAIndex && snapshot.Args.SequenceEqual(original.Args)
            && snapshot.Priority == original.Priority && snapshot.ActionTreeID == original.ActionTreeID
            && snapshot.Attenuation == original.Attenuation && snapshot.Flags == original.Flags,
            "interaction field or argument was dropped");
    }
    static void Main()
    {
        Test("real FSO.Files interaction fields, ordered use boundaries and queue entries survive capture", () =>
        {
            var person = Fixture(); var state = new VMTS1SavedPersonRecoveryState(77, person);
            Assert(state.ObjectID == 77 && state.ObjectUses.Count == 3, "person identity/use count lost");
            Assert(state.ObjectUses.Select(x => (x.TargetID, x.StackLength, x.Unknown2))
                .SequenceEqual(new[] { ((short)110, 3, (byte)1), ((short)111, 8, (byte)0), ((short)110, 11, (byte)0) }),
                "ordered use boundaries/flags were flattened");
            CheckInteraction(state.ActiveInteraction, person.ActiveInteraction);
            CheckInteraction(state.LastInteraction, person.LastInteraction);
            for (var i = 0; i < 2; i++) CheckInteraction(state.QueuedInteractions[i], person.InteractionQueue[i]);
        });
        Test("snapshot owns argument arrays and collections independently of mutable OBJMPerson", () =>
        {
            var person = Fixture(); var state = new VMTS1SavedPersonRecoveryState(77, person);
            person.ObjectUses[0] = new OBJMObjectUse { TargetID = 999 };
            person.ActiveInteraction.Args[0] = 999; person.LastInteraction.Args[1] = 999;
            person.InteractionQueue[0].Args[2] = 999; person.InteractionQueue[1] = Action(9, 9, 9);
            Assert(state.ObjectUses[0].TargetID == 110 && state.ActiveInteraction.Args[0] == -3
                && state.LastInteraction.Args[1] == 17 && state.QueuedInteractions[0].Args[2] == 254
                && state.QueuedInteractions[1].UID == 10004, "snapshot aliases mutable source arrays");
            var args = (IList<short>)state.ActiveInteraction.Args;
            try { args[0] = 123; throw new Exception("snapshot Args accepted writes"); }
            catch (NotSupportedException) { }
            var queue = (IList<VMTS1SavedPersonRecoveryState.Interaction>)state.QueuedInteractions;
            try { queue.Clear(); throw new Exception("snapshot queue accepted writes"); }
            catch (NotSupportedException) { }
            var uses = (IList<VMTS1SavedPersonRecoveryState.ObjectUse>)state.ObjectUses;
            try { uses.Clear(); throw new Exception("snapshot uses accepted writes"); }
            catch (NotSupportedException) { }
        });
        Test("full reset selection uses ObjectUses and active Icon even with invalid TTA, not Last/queued/caller/Args", () =>
        {
            var person = Fixture(); var state = new VMTS1SavedPersonRecoveryState(77, person);
            foreach (short target in new short[] { 110, 111, 121 })
                Assert(state.RequiresFullReset(target), "missed native active reset target " + target);
            foreach (short unrelated in new short[] { 0, 120, 130, 131, 140, 141, 31, 17, 77, 999 })
                Assert(!state.RequiresFullReset(unrelated), "selected unrelated/last/queued target " + unrelated);
            person.ActiveInteraction.TTAIndex = -1;
            Assert(new VMTS1SavedPersonRecoveryState(77, person).RequiresFullReset(121), "invalid TTA incorrectly suppressed native active-icon pointer comparison");
            // No VM stack is supplied to this helper: an arbitrary stack object
            // cannot participate in selection. Target999 tests an unlisted ID.
        });
        Test("queued notification matches target or icon irrespective of flags, never caller/Args", () =>
        {
            var state = new VMTS1SavedPersonRecoveryState(77, Fixture());
            var queued = state.QueuedInteractions[0];
            Assert(queued.ReferencesTarget(140) && queued.ReferencesTarget(141), "queued target/icon was not matched");
            foreach (short unrelated in new short[] { 0, 31, 17, 130, 131, 999 })
                Assert(!queued.ReferencesTarget(unrelated), "arbitrary queued reference selected " + unrelated);
            var person = Fixture(); person.InteractionQueue[0].Flags = 0;
            Assert(new VMTS1SavedPersonRecoveryState(77, person).QueuedInteractions[0].ReferencesTarget(141), "queue flags changed target matching");
        });
        Test("absent optional collections and Args remain representable", () =>
        {
            var person = new OBJMPerson {
                ActiveInteraction = new OBJMInteraction { TTAIndex = -1 },
                LastInteraction = new OBJMInteraction { TTAIndex = -1 } };
            var state = new VMTS1SavedPersonRecoveryState(77, person);
            Assert(state.ObjectUses.Count == 0 && state.QueuedInteractions.Count == 0
                && state.ActiveInteraction.Args == null && !state.ActiveInteraction.IsValid,
                "missing optional metadata was fabricated");
        });
        Console.WriteLine($"{Passed} source-linked person snapshot cases passed using real FSO.Files types. No person reset/gameplay validation.");
    }
}
