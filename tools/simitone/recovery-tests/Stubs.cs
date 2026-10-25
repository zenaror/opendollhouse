// Explicit synthetic engine boundary. No recovery algorithm is implemented here.
// These doubles expose state, callbacks, category indexing and scheduling used by
// the source-linked adapter. They do not execute BHAV or model native placement.
namespace FSO.LotView.Model
{
    public enum Direction { NORTH, NORTHEAST, EAST, SOUTHEAST, SOUTH, SOUTHWEST, WEST, NORTHWEST }
    public record struct LotTilePos(short x, short y, sbyte Level)
    {
        public static readonly LotTilePos OUT_OF_WORLD = new(-16, -16, 0);
    }
}
namespace FSO.Files.Formats.IFF.Chunks
{
    [Flags] public enum TTABFlags { None = 0, MustRun = 1 }
}
namespace FSO.SimAntics.Model
{
    public enum VMStackObjectVariable
    {
        Direction = 1, ContainerId = 2, SlotNumber = 3, Flags = 8,
        ObjectId = 11, SlotHierNumber = 14, LockoutCount = 25, ParentId = 26, Hidden = 34, CurrentValue = 41,
        ObjectVersion = 58, Category = 59, UseCount = 62
    }
    [Flags] public enum VMEntityFlags { Occupied = 32 }
    public enum VMPlacementError { Success }
    public class VMPlacementResult { public VMPlacementResult(VMPlacementError error) { } }
    public record VMRoomPortal(short ObjectID, ushort TargetRoom);
}
namespace FSO.SimAntics.Marshals
{
    public class VMEntityMarshal { public short[] Attributes; public short[] ObjectData; }
}
namespace FSO.SimAntics.Entities
{
    public class VMMultitileGroup
    {
        public bool MultiTile;
        public List<VMEntity> Objects = new();
        public List<FSO.LotView.Model.LotTilePos> Offsets = new();
        public VMEntity BaseObject => Objects.FirstOrDefault();
        public int InitialPrice;
    }
}
namespace FSO.SimAntics.Engine
{
    public enum VMQueueMode { Normal, ParentIdle }
    public class VMStackFrame { public VMEntity Callee, StackObject; public object Routine = new(); }
    public class VMQueuedAction
    {
        public ushort UID;
        public VMEntity Callee, StackObject, IconOwner;
        public VMQueueMode Mode;
        public FSO.Files.Formats.IFF.Chunks.TTABFlags Flags;
    }
    public class VMThread
    {
        public List<VMStackFrame> Stack = new();
        public List<VMQueuedAction> Queue = new();
        public short[] TempRegisters = new short[20];
        public int[] TempXL = new int[2];
        public int ActiveQueueBlock = -1;
        public object BlockingState, EODConnection;
        private readonly VMContext Context;
        private readonly VMEntity Entity;
        public VMThread(VMContext context, VMEntity entity, ushort stackSize)
        { Context = context; Entity = entity; }
        public void CancelAction(ushort uid)
        {
            var action = Queue.SingleOrDefault(x => x.UID == uid);
            if (action == null) return;
            Queue.Remove(action);
            // Contract of real VMThread.CancelAction for a nonactive normal TS1 action.
            action.Callee.ExecuteEntryPoint(4, Context, true, Entity);
        }
    }
}
namespace FSO.SimAntics
{
    using FSO.LotView.Model;
    using FSO.SimAntics.Engine;
    using FSO.SimAntics.Model;
    using FSO.SimAntics.Marshals;
    using FSO.SimAntics.Entities;
    public class Definition { public uint GUID = 0x1234; public ushort ObjectVersion = 29, StackSize = 8, MasterID; }
    public class GameObject { public Definition OBJ = new(); }
    public struct Entry { public ushort ActionFunction; }
    public class Scheduler
    {
        public List<string> Events = new();
        public HashSet<VMEntity> Scheduled = new();
        public void DescheduleTick(VMEntity obj) { Events.Add($"unschedule:{obj.ObjectID}"); Scheduled.Remove(obj); }
        public void ScheduleTickIn(VMEntity obj, int ticks) { Events.Add($"schedule:{obj.ObjectID}:{ticks}"); Scheduled.Add(obj); }
    }
    public class VM
    {
        public bool TS1 = true;
        public List<VMEntity> Entities = new();
        public VMContext Context;
        public Scheduler Scheduler = new();
        public VM() { Context = new VMContext { VM = this }; }
        public VMEntity GetObjectById(short id) => Entities.SingleOrDefault(x => x.ObjectID == id);
    }
    public class VMContext
    {
        public VM VM;
        public List<string> Events = new();
        public Dictionary<short, HashSet<VMEntity>> Categories = new();
        public Architecture Architecture = new();
        public RoomInfo[] RoomInfo = Enumerable.Range(0, 4).Select(_ => new RoomInfo()).ToArray();
        public Dictionary<short, ushort> Rooms = new();
        public List<(ushort Room, sbyte Level)> RegeneratedRooms = new();
        public ushort GetObjectRoom(VMEntity obj) => Rooms.GetValueOrDefault(obj.ObjectID);
        public void AddRoomPortal(VMEntity obj, ushort room)
        {
            // Real VMContext.AddRoomPortal selects the first opposite-room
            // portal part; the helper must supply valid final topology.
            foreach (var other in obj.MultitileGroup.Objects)
                if (other != obj && other.Portal && GetObjectRoom(other) != room)
                { RoomInfo[room].Portals.Add(new(obj.ObjectID, GetObjectRoom(other))); break; }
        }
        public void RegenRoomObs(ushort room, sbyte level) => RegeneratedRooms.Add((room, level));
        public void RegisterCategory(VMEntity obj, short category)
        { if (!Categories.ContainsKey(category)) Categories[category] = new(); Categories[category].Add(obj); }
        public void ChangeCategory(VMEntity obj, short oldValue, short value)
        { if (Categories.TryGetValue(oldValue, out var entries)) entries.Remove(obj); RegisterCategory(obj, value); }
    }
    public class Architecture { public int Stories = 2; }
    public class Room { public int Floor; }
    public class RoomInfo { public Room Room = new(); public List<VMRoomPortal> Portals = new(); }
    public class VMEntity
    {
        public GameObject Object = new();
        public short ObjectID;
        public bool Portal, Dead, GhostImage;
        public short[] ObjectData = new short[80];
        public Entry[] EntryPoints = new Entry[33];
        public VMThread Thread;
        public VMMultitileGroup MultitileGroup;
        public VMEntity Container;
        public short ContainerSlot = -1;
        public VMEntity[] Contained = Array.Empty<VMEntity>();
        public short[] Attributes = new short[3];
        public LotTilePos Position;
        public Direction Direction;
        public VMContext Context;
        public Dictionary<int, Action<VMEntity>> Callbacks = new();
        public void SaveEnt(VMEntityMarshal marshal)
        { marshal.Attributes = (short[])Attributes.Clone(); marshal.ObjectData = (short[])ObjectData.Clone(); }
        public void SetAttribute(int i, short value) => Attributes[i] = value;
        public short GetValue(VMStackObjectVariable variable) => ObjectData[(int)variable];
        public void SetValue(VMStackObjectVariable variable, short value)
        {
            if (variable == VMStackObjectVariable.Category)
                Context.ChangeCategory(this, ObjectData[(int)variable], value);
            if (variable == VMStackObjectVariable.Direction) Direction = (Direction)value;
            if (variable == VMStackObjectVariable.CurrentValue) MultitileGroup.InitialPrice = value;
            ObjectData[(int)variable] = value;
        }
        public void SetFlag(VMEntityFlags flag, bool enabled)
        { if (enabled) ObjectData[8] |= (short)flag; else ObjectData[8] &= (short)~flag; }
        public bool ExecuteEntryPoint(int ep, VMContext context, bool immediately, VMEntity stackObject = null)
        {
            context.Events.Add($"ep{ep}:{ObjectID}");
            if (Callbacks.TryGetValue(ep, out var callback)) callback(this);
            if (!immediately && EntryPoints[ep].ActionFunction != 0) Thread.Stack.Add(new VMStackFrame { Callee = this });
            return true;
        }
        public void PrePositionChange(VMContext context)
        { context.Events.Add($"detach:{ObjectID}"); if (Container != null) Container.ClearSlot(ContainerSlot); }
        public void SetIndivPosition(LotTilePos pos, Direction direction, VMContext context, VMPlacementResult info)
        { Position = pos; Direction = direction; }
        public void PositionChange(VMContext context, bool noEntryPoint)
        {
            foreach (var child in Contained.Where(x => x != null))
            { child.Position = Position; child.PositionChange(context, noEntryPoint); }
            if (Container != null || Position == LotTilePos.OUT_OF_WORLD) return;
            if (!noEntryPoint) ExecuteEntryPoint(9, context, true);
        }
        public bool PlaceInSlot(VMEntity child, int slot, bool cleanOld, VMContext context)
        {
            if (slot < 0 || slot >= Contained.Length || Contained[slot] != null) return false;
            Contained[slot] = child; child.Container = this; child.ContainerSlot = (short)slot; child.Position = Position;
            return true;
        }
        public VMEntity GetSlot(int slot) => Contained[slot];
        public int TotalSlots() => Contained.Length;
        public void ClearSlot(int slot)
        { Contained[slot].Container = null; Contained[slot].ContainerSlot = -1; Contained[slot] = null; }
        public void RefreshGraphic() { }
        public void RefreshLight() { }
    }
    public class VMGameObject : VMEntity { }
    public class VMAvatar : VMEntity { }
}
