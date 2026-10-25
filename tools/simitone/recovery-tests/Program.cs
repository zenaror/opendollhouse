using FSO.LotView.Model;
using FSO.SimAntics;
using FSO.SimAntics.Engine;
using FSO.SimAntics.Entities;
using FSO.SimAntics.Model;
using FSO.SimAntics.Utils;

static class Program
{
    private static int Passed;
    static void Assert(bool condition, string message)
    { if (!condition) throw new Exception(message); }
    static void Test(string name, Action run)
    { run(); Passed++; Console.WriteLine("PASS " + name); }
    static VMGameObject Object(VM vm, short id)
    {
        var obj = new VMGameObject { ObjectID = id, Context = vm.Context, Position = new(80, 96, 1) };
        obj.Thread = new VMThread(vm.Context, obj, 8);
        obj.Thread.Stack.Add(new VMStackFrame());
        obj.EntryPoints[1].ActionFunction = 4097;
        obj.MultitileGroup = new VMMultitileGroup { InitialPrice = 700 };
        obj.MultitileGroup.Objects.Add(obj); obj.MultitileGroup.Offsets.Add(new());
        for (var i = 0; i < obj.ObjectData.Length; i++) obj.ObjectData[i] = (short)(100 + i);
        obj.ObjectData[1] = 3; obj.ObjectData[11] = id;
        obj.Attributes = new short[] { 71, 72, 73 };
        obj.Thread.TempRegisters[0] = 99;
        vm.Entities.Add(obj);
        vm.Context.RegisterCategory(obj, obj.ObjectData[59]);
        vm.Scheduler.Scheduled.Add(obj);
        return obj;
    }
    static void Unsupported(Action run)
    {
        try { run(); }
        catch (NotSupportedException) { return; }
        throw new Exception("Expected unsupported recovery to fail closed.");
    }
    static string State(VM vm) => System.Text.Json.JsonSerializer.Serialize(new
    {
        Entities = vm.Entities.Select(x => new
        {
            x.ObjectID, x.Dead, x.Position, x.Direction, Data = x.ObjectData,
            x.Attributes, Container = x.Container?.ObjectID, x.ContainerSlot,
            Children = x.Contained.Select(c => c?.ObjectID),
            Stack = x.Thread?.Stack.Count, Queue = x.Thread?.Queue.Select(q => q.UID),
            Temps = x.Thread?.TempRegisters,
            Group = x.MultitileGroup?.Objects.Select(o => o.ObjectID),
            Offsets = x.MultitileGroup?.Offsets
        }),
        vm.Context.Events, Scheduler = vm.Scheduler.Events,
        Scheduled = vm.Scheduler.Scheduled.Select(x => x.ObjectID),
        Categories = vm.Context.Categories.Select(x => new { x.Key, IDs = x.Value.Select(o => o.ObjectID) })
    });
    static VMAvatar Person(VM vm, params VMQueuedAction[] queue)
    {
        var person = new VMAvatar { ObjectID = 90, Context = vm.Context };
        person.Thread = new VMThread(vm.Context, person, 8);
        person.Thread.Queue.AddRange(queue);
        vm.Entities.Add(person); return person;
    }
    static void Main()
    {
        Test("native 72-word clear, nine retained fields, identity/version and runtime extension", () =>
        {
            var vm = new VM(); var obj = Object(vm, 17);
            var before = (short[])obj.ObjectData.Clone(); var position = obj.Position;
            var nativeRetained = new HashSet<int> { 1, 21, 24, 29, 41, 45, 46, 47, 67 };
            // Inspect the reset boundary before Init can deliberately change words.
            obj.Callbacks[0] = _ =>
            {
                for (var i = 0; i < 72; i++)
                {
                    short expected = i == 11 ? obj.ObjectID : i == 58 ? (short)29
                        : nativeRetained.Contains(i) ? before[i] : (short)0;
                    Assert(obj.ObjectData[i] == expected, $"word {i}: expected {expected}, got {obj.ObjectData[i]}");
                }
                Assert(obj.Attributes.All(x => x == 0), "native attribute block must clear");
                Assert(obj.Thread.TempRegisters.All(x => x == 0), "fallback must clear temporaries");
                Assert(obj.Thread.Stack.Count == 1, "Main must be installed before Init");
            };
            VMTS1ObjectRecovery.Apply(vm, obj, true);
            Assert(ReferenceEquals(vm.GetObjectById(17), obj), "object identity changed");
            Assert(obj.Position == position, "position changed");
            Assert(obj.ObjectData.Skip(72).SequenceEqual(before.Skip(72)), "runtime extension was overwritten");
            Assert(vm.Scheduler.Scheduled.Contains(obj), "recovered Main is unscheduled");
        });
        Test("negative direction normalizes to native octant", () =>
        {
            var vm = new VM(); var obj = Object(vm, 17); obj.ObjectData[1] = -9;
            VMTS1ObjectRecovery.Apply(vm, obj, true);
            Assert(obj.ObjectData[1] == 7 && obj.Direction == Direction.NORTHWEST, "negative direction did not normalize");
        });
        Test("Init category changes remove the old category index", () =>
        {
            var vm = new VM(); var obj = Object(vm, 17); var oldCategory = obj.ObjectData[59];
            obj.Callbacks[0] = x => x.SetValue(VMStackObjectVariable.Category, 8);
            VMTS1ObjectRecovery.Apply(vm, obj, true);
            Assert(!vm.Context.Categories[oldCategory].Contains(obj), "stale saved category remains indexed");
            Assert(vm.Context.Categories[8].Contains(obj), "Init category was not indexed");
            Assert(vm.Context.Categories.Where(x => x.Value.Contains(obj)).Count() == 1, "object indexed in multiple categories");
        });
        Test("EP3 preserves temporary registers and mutable words while restarting Main", () =>
        {
            var vm = new VM(); var obj = Object(vm, 17);
            obj.EntryPoints[3].ActionFunction = 4099;
            obj.Thread.TempXL[1] = 12345; obj.ObjectData[8] = (short)VMEntityFlags.Occupied;
            var data = (short[])obj.ObjectData.Clone();
            obj.Callbacks[3] = x => x.Thread.TempRegisters[3] = 777;
            VMTS1ObjectRecovery.Apply(vm, obj, true);
            Assert(obj.Thread.TempRegisters[0] == 99 && obj.Thread.TempRegisters[3] == 777,
                "EP3 temporary state was discarded");
            Assert(obj.Thread.TempXL[1] == 12345, "EP3 extended temporaries were discarded");
            Assert(obj.Thread.Stack.Count == 1, "EP3 did not restart Main");
            for (var i = 0; i < data.Length; i++)
                Assert(obj.ObjectData[i] == (i == 8 || i == 62 ? 0 : data[i]), "EP3 changed unrelated word " + i);
            Assert(!vm.Context.Events.Any(x => x.StartsWith("detach:")), "EP3 unexpectedly detached object");
        });
        Test("EP4 notification changes precede the reset snapshot and preserve queue order", () =>
        {
            var vm = new VM(); var obj = Object(vm, 17); var other = Object(vm, 18);
            var person = Person(vm,
                new VMQueuedAction { UID = 1, Callee = other },
                new VMQueuedAction { UID = 2, Callee = obj },
                new VMQueuedAction { UID = 3, Callee = other });
            var notifiedPosition = new LotTilePos(192, 208, 2);
            obj.Callbacks[4] = x => { x.Position = notifiedPosition; x.SetValue(VMStackObjectVariable.CurrentValue, 1234); };
            VMTS1ObjectRecovery.Apply(vm, obj, true);
            Assert(obj.Position == notifiedPosition && obj.ObjectData[41] == 1234, "pre-EP4 snapshot overwrote callback state");
            Assert(person.Thread.Queue.Select(x => x.UID).SequenceEqual(new ushort[] { 1, 3 }), "unrelated queue order changed");
            Assert(vm.Context.Events.IndexOf("ep4:17") < vm.Context.Events.IndexOf("detach:17"), "notification ran after reset");
        });
        Test("EP10 mutable changes survive clear while original placement survives detach", () =>
        {
            var vm = new VM(); var obj = Object(vm, 17); var oldPosition = obj.Position;
            obj.EntryPoints[10].ActionFunction = 4106;
            obj.Callbacks[10] = x => { x.ObjectData[21] = 432; x.ObjectData[41] = 543; x.ObjectData[1] = -1; };
            VMTS1ObjectRecovery.Apply(vm, obj, true);
            Assert(obj.ObjectData[21] == 432 && obj.ObjectData[41] == 543, "post-EP10 retained data was lost");
            Assert(obj.ObjectData[1] == 7 && obj.Position == oldPosition, "direction or original placement lost");
        });
        Test("supported preflight is mutation free", () =>
        {
            var vm = new VM(); var obj = Object(vm, 17);
            Person(vm, new VMQueuedAction { UID = 1, Callee = obj });
            var before = State(vm); VMTS1ObjectRecovery.Validate(vm, obj, true);
            Assert(State(vm) == before, "preflight mutated data, queues, category index or scheduler");
        });
        Test("unsupported portal, person and active reference fail before mutation", () =>
        {
            var vm = new VM(); var obj = Object(vm, 17); obj.Portal = true;
            var before = State(vm); Unsupported(() => VMTS1ObjectRecovery.Apply(vm, obj, true));
            Assert(State(vm) == before, "portal rejection mutated state");
            obj.Portal = false; var person = Person(vm); before = State(vm);
            Unsupported(() => VMTS1ObjectRecovery.Apply(vm, person, true));
            Assert(State(vm) == before, "person rejection mutated state");
            person.Thread.Stack.Add(new VMStackFrame { StackObject = obj }); before = State(vm);
            Unsupported(() => VMTS1ObjectRecovery.Apply(vm, obj, true));
            Assert(State(vm) == before, "active-reference rejection mutated state");
        });
        Test("group detach/Main/Init/placement order, retained data and contained child links", () =>
        {
            var vm = new VM(); var a = Object(vm, 17); var b = Object(vm, 18); var child = Object(vm, 19);
            b.Position = new LotTilePos(96, 96, 1);
            var group = a.MultitileGroup; group.MultiTile = true;
            group.Objects.Add(b); group.Offsets.Add(new(16, 0, 0)); b.MultitileGroup = group;
            a.Object.OBJ.MasterID = b.Object.OBJ.MasterID = 1;
            a.Contained = new VMEntity[1]; a.PlaceInSlot(child, 0, false, vm.Context);
            a.ObjectData[67] = 1;
            var aPos = a.Position; var bPos = b.Position; var offsets = group.Offsets.ToArray();
            group.InitialPrice = 777; a.ObjectData[41] = 200; b.ObjectData[41] = 300;
            foreach (var piece in new[] { a, b })
            {
                piece.EntryPoints[10].ActionFunction = 4106;
                piece.Callbacks[10] = x =>
                {
                    x.ObjectData[21] = (short)(500 + x.ObjectID); x.ObjectData[41] += 10;
                    if (x == a) group.InitialPrice = 888;
                };
                piece.Callbacks[0] = _ =>
                {
                    Assert(vm.Context.Events.Contains("ep1:17") && vm.Context.Events.Contains("ep1:18"), "Init began before every Main restart");
                    Assert(group.Offsets.SequenceEqual(offsets), "group offsets were not restored before Init");
                };
            }
            VMTS1ObjectRecovery.Apply(vm, a, true);
            var events = vm.Context.Events;
            Assert(Math.Max(events.IndexOf("ep10:17"), events.IndexOf("ep10:18"))
                < Math.Min(events.IndexOf("detach:17"), events.IndexOf("detach:18")), "detached a piece before all EP10 callbacks");
            Assert(a.Position == aPos && b.Position == bPos, "group placement changed");
            Assert(a.ObjectData[21] == 517 && b.ObjectData[21] == 518, "per-piece EP10 retained state lost");
            Assert(a.ObjectData[41] == 210 && b.ObjectData[41] == 310, "per-piece saved value lost");
            Assert(group.InitialPrice == 888, "post-EP10 shared group price was lost or replaced by slave value");
            Assert(a.Contained[0] == child && child.Container == a && child.ContainerSlot == 0 && child.Position == aPos,
                "contained child links/position changed");
            Assert(!events.Contains("ep0:19") && !events.Contains("ep10:19"), "child was independently reset");
        });
        Test("EP10 deletion aborts group fallback before detaching surviving pieces", () =>
        {
            var vm = new VM(); var a = Object(vm, 17); var b = Object(vm, 18);
            var group = a.MultitileGroup; group.MultiTile = true;
            group.Objects.Add(b); group.Offsets.Add(new(16, 0, 0)); b.MultitileGroup = group;
            var original = a.Position;
            b.EntryPoints[10].ActionFunction = 4106;
            b.Callbacks[10] = x => x.Dead = true;
            VMTS1ObjectRecovery.Apply(vm, a, true);
            Assert(a.Position == original, "survivor was stranded after a callback deleted another piece");
            Assert(!vm.Context.Events.Any(x => x.StartsWith("detach:")), "group detach began after EP10 deletion");
            // EP10 itself is an authorized mutation. This is not an atomic-migration claim.
            Assert(b.Dead, "callback deletion was unexpectedly rolled back");
        });
        Test("inconsistent group preflight fails before callbacks", () =>
        {
            var vm = new VM(); var obj = Object(vm, 17);
            obj.MultitileGroup.Offsets.Clear(); var before = State(vm);
            Unsupported(() => VMTS1ObjectRecovery.Apply(vm, obj, true));
            Assert(State(vm) == before, "invalid group mutated before rejection");
        });
        Test("direct contained leaf preserves raw links and native callback sequence", () =>
        {
            var vm = new VM(); var parent = Object(vm, 17); var child = Object(vm, 18);
            parent.Contained = new VMEntity[1]; parent.PlaceInSlot(child, 0, false, vm.Context);
            child.ObjectData[2] = 17; child.ObjectData[3] = 0;
            child.ObjectData[14] = 1; child.ObjectData[26] = 17;
            child.EntryPoints[10].ActionFunction = 4106;
            var before = State(vm); VMTS1ObjectRecovery.Validate(vm, child, true);
            Assert(State(vm) == before, "supported direct-leaf preflight mutated state");
            VMTS1ObjectRecovery.Apply(vm, child, true);
            Assert(parent.GetSlot(0) == child && child.Container == parent && child.ContainerSlot == 0,
                "direct parent slot links were lost");
            Assert(child.Position == parent.Position, "contained leaf position was not restored");
            Assert(child.ObjectData[2] == 17 && child.ObjectData[3] == 0
                && child.ObjectData[14] == 1 && child.ObjectData[26] == 17, "native raw direct-slot links were lost");
            Assert(vm.Context.Events.Where(x => x.StartsWith("ep") && x.EndsWith(":18"))
                .SequenceEqual(new[] { "ep10:18", "ep9:18", "ep1:18", "ep0:18", "ep9:18", "ep9:18", "ep0:18" }),
                "direct-leaf detach/Main/Init/placement callback sequence changed");
            Assert(!vm.Context.Events.Any(x => x.EndsWith(":17")), "recovering the leaf reset its parent");
        });
        Test("ambiguous native parent hierarchy fails before mutation", () =>
        {
            var vm = new VM(); var parent = Object(vm, 17); var child = Object(vm, 18);
            parent.Contained = new VMEntity[1]; parent.PlaceInSlot(child, 0, false, vm.Context);
            child.ObjectData[2] = 17; child.ObjectData[3] = 0;
            child.ObjectData[14] = 1; child.ObjectData[26] = 99;
            var before = State(vm);
            Unsupported(() => VMTS1ObjectRecovery.Apply(vm, child, true));
            Assert(State(vm) == before, "ambiguous hierarchy rejection mutated state");
        });
        Console.WriteLine($"{Passed} synthetic source-linked native recovery policy cases passed. No real engine/BHAV/graphics/gameplay validation.");
    }
}
