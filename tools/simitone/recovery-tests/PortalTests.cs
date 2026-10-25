using FSO.LotView.Model;
using FSO.SimAntics;
using FSO.SimAntics.Entities;
using FSO.SimAntics.Model;
using FSO.SimAntics.Utils;

// Actual VMTS1PortalRecovery.cs executes against synthetic room/engine services.
// Assertions describe graph invariants, not helper implementation steps.
static class PortalTests
{
    static void Assert(bool ok, string reason) { if (!ok) throw new Exception(reason); }
    static VMGameObject Add(VM vm, short id, ushort room)
    {
        var portal = new VMGameObject { ObjectID = id, Portal = true, Context = vm.Context, Position = new(64, 64, 1) };
        vm.Entities.Add(portal); vm.Context.Rooms[id] = room; return portal;
    }
    static VMMultitileGroup Group(params VMGameObject[] portals)
    {
        var group = new VMMultitileGroup { MultiTile = true };
        foreach (var portal in portals)
        { group.Objects.Add(portal); group.Offsets.Add(new()); portal.MultitileGroup = group; }
        return group;
    }
    static (VM Vm, VMGameObject A, VMGameObject B) Pair()
    {
        var vm = new VM(); var a = Add(vm, 17, 1); var b = Add(vm, 18, 2);
        Group(a, b);
        vm.Context.AddRoomPortal(a, 1); vm.Context.AddRoomPortal(b, 2);
        return (vm, a, b);
    }
    static string Links(VM vm) => string.Join(";", vm.Context.RoomInfo.SelectMany((r, i) =>
        r.Portals.Select(p => $"{i}:{p.ObjectID}>{p.TargetRoom}")));
    static void Link(VM vm, ushort room, short source, ushort target)
    { Assert(vm.Context.RoomInfo[room].Portals.Count(p => p.ObjectID == source && p.TargetRoom == target) == 1,
        $"expected exactly one room{room} portal{source} to room{target}: {Links(vm)}"); }
    public static void Run(Action<string, Action> test)
    {
        test("portal capture is read-only and successful restore keeps unrelated topology", () =>
        {
            var (vm, a, b) = Pair(); vm.Context.RoomInfo[3].Portals.Add(new VMRoomPortal(99, 1));
            var before = Links(vm); var snapshot = VMTS1PortalRecovery.Capture(vm, a);
            Assert(Links(vm) == before && vm.Context.RegeneratedRooms.Count == 0 && !snapshot.IsDetached,
                "portal capture changed topology");
            VMTS1PortalRecovery.BeforeDetach(vm, snapshot);
            Assert(snapshot.IsDetached && Links(vm) == "3:99>1", "detach left stale links or removed unrelated portal");
            VMTS1PortalRecovery.AfterRestore(vm, snapshot);
            Link(vm, 1, 17, 2); Link(vm, 2, 18, 1); Link(vm, 3, 99, 1);
            Assert(!snapshot.IsDetached, "successful restore retained cleanup ownership");
        });
        test("portal cleanup removes a deleted partner and never resurrects it", () =>
        {
            var (vm, a, b) = Pair(); var snapshot = VMTS1PortalRecovery.Capture(vm, a);
            VMTS1PortalRecovery.BeforeDetach(vm, snapshot); b.Dead = true;
            // Simulate stale links emitted by a callback before finally cleanup.
            vm.Context.RoomInfo[2].Portals.Add(new VMRoomPortal(18, 1));
            VMTS1PortalRecovery.Reconcile(vm, snapshot);
            Assert(vm.Context.RoomInfo.All(r => r.Portals.Count == 0), "deleted partner was resurrected or survivor linked to it");
            Assert(b.Dead && vm.Entities.Contains(b), "cleanup changed entity lifetime");
        });
        test("portal finally cleanup repairs a partially failed AfterRestore", () =>
        {
            var (vm, a, b) = Pair(); var snapshot = VMTS1PortalRecovery.Capture(vm, a);
            VMTS1PortalRecovery.BeforeDetach(vm, snapshot); b.Dead = true;
            var failed = false;
            try { VMTS1PortalRecovery.AfterRestore(vm, snapshot); }
            catch (NotSupportedException) { failed = true; }
            finally { VMTS1PortalRecovery.Reconcile(vm, snapshot); }
            Assert(failed && !snapshot.IsDetached && vm.Context.RoomInfo.All(r => r.Portals.Count == 0),
                "finally cleanup left partial portal links after a restore failure");
        });
        test("portal regrouping rebuilds each actual surviving group without old cross-links", () =>
        {
            var (vm, a, b) = Pair(); var snapshot = VMTS1PortalRecovery.Capture(vm, a);
            VMTS1PortalRecovery.BeforeDetach(vm, snapshot);
            var c = Add(vm, 19, 3); var d = Add(vm, 20, 0);
            Group(a, c); Group(b, d);
            VMTS1PortalRecovery.Reconcile(vm, snapshot);
            Link(vm, 1, 17, 3); Link(vm, 3, 19, 1); Link(vm, 2, 18, 0); Link(vm, 0, 20, 2);
            Assert(vm.Context.RoomInfo.Sum(r => r.Portals.Count) == 4, "old/redundant group links survived");
        });
        test("portal cleanup is idempotent and does not register out-of-world or removed pieces", () =>
        {
            var (vm, a, b) = Pair(); var snapshot = VMTS1PortalRecovery.Capture(vm, a);
            VMTS1PortalRecovery.BeforeDetach(vm, snapshot);
            b.Position = LotTilePos.OUT_OF_WORLD; vm.Entities.Remove(a);
            VMTS1PortalRecovery.Reconcile(vm, snapshot);
            Assert(vm.Context.RoomInfo.All(r => r.Portals.Count == 0), "removed or out-of-world portal was registered");
            var before = Links(vm); var regenCount = vm.Context.RegeneratedRooms.Count;
            VMTS1PortalRecovery.Reconcile(vm, snapshot);
            Assert(Links(vm) == before && vm.Context.RegeneratedRooms.Count == regenCount,
                "repeated cleanup changed topology or refreshed rooms again");
        });
        test("portal stale-link removal regenerates room Floor plus one", () =>
        {
            var (vm, a, b) = Pair(); vm.Context.RoomInfo[3].Room.Floor = 1;
            vm.Context.RoomInfo[3].Portals.Add(new VMRoomPortal(17, 2));
            var snapshot = VMTS1PortalRecovery.Capture(vm, a);
            VMTS1PortalRecovery.BeforeDetach(vm, snapshot);
            Assert(vm.Context.RegeneratedRooms.Contains(((ushort)3, (sbyte)2)),
                "zero-based room Floor was passed as a one-based placement Level");
            Assert(!vm.Context.RegeneratedRooms.Contains(((ushort)3, (sbyte)1)), "wrong floor was regenerated");
        });
    }
}
