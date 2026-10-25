using System;
using System.Collections.Generic;

namespace OpenDollhouse.Simulation
{
    /// <summary>
    /// ID-ordered, reference-retaining wake lists for a simulation scheduler.
    /// The caller owns ticks, entity state, and the execution loop. A returned
    /// list is live: inserting during its traversal changes that traversal.
    /// </summary>
    public sealed class WakeQueue<T> where T : class
    {
        private readonly Func<T, short> objectId;
        private readonly Dictionary<uint, List<T>> schedule = new Dictionary<uint, List<T>>();
        private List<T> tickThisFrame;

        public WakeQueue(Func<T, short> objectId)
        {
            if (objectId == null) throw new ArgumentNullException("objectId");
            this.objectId = objectId;
        }

        public void Schedule(T entity, uint tick)
        {
            List<T> list;
            if (!schedule.TryGetValue(tick, out list))
            {
                list = new List<T>();
                schedule[tick] = list;
            }
            AddByObjectId(list, entity);
        }

        public void ScheduleCurrentTick(T entity)
        {
            // Reset retains this reference until the next RunTick lookup.
            AddByObjectId(tickThisFrame, entity);
        }

        public void Deschedule(T entity, uint scheduledEnd, uint currentTick)
        {
            if (scheduledEnd > currentTick)
                schedule[scheduledEnd].Remove(entity);
        }

        public WakeTarget PrepareInterrupt(T entity, uint scheduledEnd, uint currentTick, short currentObjectId)
        {
            if (scheduledEnd == currentTick || scheduledEnd == 0) return WakeTarget.None;
            Deschedule(entity, scheduledEnd, currentTick);
            return objectId(entity) > currentObjectId && tickThisFrame != null
                ? WakeTarget.CurrentTick : WakeTarget.NextTick;
        }

        public void BeginTick(uint tick, uint previousTick)
        {
            if (previousTick != 0) return;
            List<T> firstTick;
            if (schedule.TryGetValue(1, out firstTick))
            {
                // The original scheduler overwrites any destination entry.
                schedule[tick] = firstTick;
                if (tick != 1) schedule.Remove(1);
            }
        }

        public bool TryGetCurrentTick(uint tick, out List<T> entities)
        {
            // The old TryGetValue(..., out TickThisFrame) also assigns null
            // when the current tick is absent.
            bool found = schedule.TryGetValue(tick, out tickThisFrame);
            entities = tickThisFrame;
            return found;
        }

        public void CompleteCurrentTick(uint tick)
        {
            schedule.Remove(tick);
        }

        public void Reset()
        {
            schedule.Clear();
            // Keep tickThisFrame: Reset in the original scheduler only cleared
            // TickSchedule, leaving that field stale until the next lookup.
        }

        private static void AddByObjectId(List<T> list, T entity, Func<T, short> objectId)
        {
            if (objectId == null) throw new ArgumentNullException("objectId");
            if (list.Count == 0) { list.Add(entity); return; }
            int id = objectId(entity);
            int max = list.Count;
            int min = 0;
            while (max > min)
            {
                int mid = (max + min) / 2;
                int nid = objectId(list[mid]);
                if (id < nid) max = mid;
                else if (id == nid) return;
                else min = mid + 1;
            }
            list.Insert(min, entity);
        }

        private void AddByObjectId(List<T> list, T entity)
        {
            AddByObjectId(list, entity, objectId);
        }
    }

    public enum WakeTarget
    {
        None,
        CurrentTick,
        NextTick
    }
}
