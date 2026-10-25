using System;
using System.Collections.Generic;

namespace FSO.SimAntics.Engine
{
    // Minimal VM API used by the actual old and adapted scheduler source files.
    public sealed class VM
    {
        public static bool UseWorld;
        public bool Aborting;
        public object Scheduler;
        public readonly VMContext Context = new VMContext();
        public readonly List<VMEntity> Entities = new List<VMEntity>();
        public readonly List<string> Trace = new List<string>();

        public static void AddToObjList(List<VMEntity> list, VMEntity entity)
        {
            if (list.Count == 0) { list.Add(entity); return; }
            int id = entity.ObjectID;
            int max = list.Count;
            int min = 0;
            while (max > min)
            {
                int mid = (max + min) / 2;
                int nid = list[mid].ObjectID;
                if (id < nid) max = mid;
                else if (id == nid) return;
                else min = mid + 1;
            }
            list.Insert(min, entity);
        }
    }

    public sealed class VMContext { public ulong RandomSeed; }
    public sealed class VMThread { public uint ScheduleIdleEnd; }
    public sealed class VMWorldUI { public int IdleFrames; }

    public sealed class VMEntity
    {
        public short ObjectID;
        public bool Dead;
        public bool EveryFrame;
        public VMThread Thread = new VMThread();
        public VMWorldUI WorldUI = new VMWorldUI();
        public VM Owner;
        public Action OnTick;

        public bool RunEveryFrame() { return EveryFrame; }
        public void Tick()
        {
            Owner.Trace.Add("tick:" + ObjectID);
            if (OnTick != null) OnTick();
        }
        public void Delete(bool immediate, VMContext context)
        {
            Owner.Trace.Add("delete:" + ObjectID);
            Dead = true;
        }
    }
}
