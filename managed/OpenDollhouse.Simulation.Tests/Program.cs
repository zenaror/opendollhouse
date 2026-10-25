using System;
using System.Collections.Generic;
using FSO.SimAntics.Engine;

internal sealed class Harness
{
    public readonly VM Vm = new VM();
    public readonly Action<VMEntity, uint> ScheduleTick;
    public readonly Action<VMEntity, uint> ScheduleTickIn;
    public readonly Action<VMEntity> ScheduleCurrentTick;
    public readonly Action<VMEntity> Deschedule;
    public readonly Action<VMEntity> Interrupt;
    public readonly Action<uint> Begin;
    public readonly Action Run;
    public readonly Action<VMEntity> Delete;
    public readonly Action Reset;
    public readonly Action<short> SetCurrentObjectId;

    public Harness(bool baseline)
    {
        VM.UseWorld = true;
        if (baseline)
        {
            var s = new FSO.SimAntics.Engine.Baseline.VMScheduler(Vm);
            ScheduleTick = s.ScheduleTick;
            ScheduleTickIn = s.ScheduleTickIn;
            ScheduleCurrentTick = s.ScheduleCurrentTick;
            Deschedule = s.DescheduleTick;
            Interrupt = s.RescheduleInterrupt;
            Begin = s.BeginTick;
            Run = s.RunTick;
            Delete = s.Delete;
            Reset = s.Reset;
            SetCurrentObjectId = id => s.CurrentObjectID = id;
        }
        else
        {
            var s = new FSO.SimAntics.Engine.VMScheduler(Vm);
            ScheduleTick = s.ScheduleTick;
            ScheduleTickIn = s.ScheduleTickIn;
            ScheduleCurrentTick = s.ScheduleCurrentTick;
            Deschedule = s.DescheduleTick;
            Interrupt = s.RescheduleInterrupt;
            Begin = s.BeginTick;
            Run = s.RunTick;
            Delete = s.Delete;
            Reset = s.Reset;
            SetCurrentObjectId = id => s.CurrentObjectID = id;
        }
    }

    public VMEntity Entity(short id)
    {
        var entity = new VMEntity { ObjectID = id, Owner = Vm };
        Vm.Entities.Add(entity);
        return entity;
    }

    public string Result(params VMEntity[] entities)
    {
        var parts = new List<string>(Vm.Trace);
        parts.Add("seed:" + Vm.Context.RandomSeed);
        foreach (var entity in entities)
            parts.Add("state:" + entity.ObjectID + ":" + entity.Thread.ScheduleIdleEnd + ":" + entity.WorldUI.IdleFrames + ":" + entity.Dead);
        return string.Join("|", parts);
    }
}

internal static class Program
{
    private static int count;

    private static void Compare(string name, Func<Harness, string> scenario, string expected)
    {
        var oldResult = scenario(new Harness(true));
        var newResult = scenario(new Harness(false));
        if (oldResult != newResult)
            throw new Exception(name + " diverged\nold: " + oldResult + "\nnew: " + newResult);
        if (expected != null && oldResult != expected)
            throw new Exception(name + " baseline expectation changed\nactual: " + oldResult + "\nexpected: " + expected);
        Console.WriteLine("PASS " + name + " => " + oldResult);
        count++;
    }

    private static void Main(string[] args)
    {
        if (args.Length == 1 && args[0] == "--expect-native-unavailable")
        {
            NativeQueueTests.ExpectMissingNative();
            return;
        }
        Compare("live insertion, ID order and deduplication", h =>
        {
            var a = h.Entity(10); var b = h.Entity(30); var c = h.Entity(20); var duplicate = h.Entity(20);
            a.OnTick = () => { h.ScheduleCurrentTick(c); h.ScheduleCurrentTick(duplicate); };
            h.ScheduleTick(a, 5); h.ScheduleTick(b, 5);
            h.Begin(5); h.Run();
            return h.Result(a, b, c, duplicate);
        }, "tick:10|tick:20|tick:30|seed:4|state:10:5:0:False|state:30:5:0:False|state:20:5:0:False|state:20:5:0:False");

        Compare("interrupt current frame versus next frame", h =>
        {
            var a = h.Entity(10); var later = h.Entity(20); var earlier = h.Entity(5);
            a.OnTick = () => { h.Interrupt(later); h.Interrupt(earlier); };
            h.ScheduleTick(a, 5); h.ScheduleTick(later, 9); h.ScheduleTick(earlier, 9);
            h.Begin(5); h.Run(); h.Begin(6); h.Run(); h.Begin(9); h.Run();
            return h.Result(a, later, earlier);
        }, "tick:10|tick:20|tick:5|seed:9|state:10:5:0:False|state:20:5:0:False|state:5:6:1:False");

        Compare("deschedule and deferred deletion", h =>
        {
            var a = h.Entity(10); var b = h.Entity(20);
            a.OnTick = () => { h.Deschedule(b); h.Delete(b); h.Vm.Trace.Add("callback:end"); };
            h.ScheduleTick(a, 5); h.ScheduleTick(b, 9);
            h.Begin(5); h.Run(); h.Begin(9); h.Run();
            return h.Result(a, b);
        }, "tick:10|callback:end|delete:20|seed:4|state:10:5:0:False|state:20:9:0:True");

        Compare("object ID reuse retains queued reference", h =>
        {
            var old = h.Entity(20); var replacement = h.Entity(20);
            old.OnTick = () => h.Vm.Trace.Add("identity:old");
            replacement.OnTick = () => h.Vm.Trace.Add("identity:new");
            h.ScheduleTick(old, 5); h.ScheduleTick(replacement, 5);
            h.Deschedule(replacement);
            h.Begin(5); h.Run();
            return h.Result(old, replacement);
        }, "tick:20|identity:old|seed:2|state:20:5:0:False|state:20:5:0:False");

        Compare("tick-zero save-resume migration overwrites destination", h =>
        {
            var migrated = h.Entity(20); var overwritten = h.Entity(30);
            h.ScheduleTick(migrated, 1); h.ScheduleTick(overwritten, 100);
            h.Begin(100); h.Run();
            return h.Result(migrated, overwritten);
        }, "tick:20|seed:2|state:20:1:0:False|state:30:100:0:False");

        Compare("uint tick wrap", h =>
        {
            var entity = h.Entity(20);
            h.Begin(uint.MaxValue);
            h.ScheduleTickIn(entity, 1);
            h.Begin(0); h.Run();
            return h.Result(entity);
        }, "tick:20|seed:1|state:20:0:1:False");

        Compare("RunEveryFrame clamp and WorldUI idle frames", h =>
        {
            var entity = h.Entity(20); entity.EveryFrame = true;
            h.Begin(5); h.ScheduleTickIn(entity, 9);
            h.Begin(6); h.Run();
            return h.Result(entity);
        }, "tick:20|seed:1|state:20:6:1:False");

        Compare("missing tick clears stale frame list", h =>
        {
            var first = h.Entity(10); var interrupted = h.Entity(20);
            h.ScheduleTick(first, 5); h.Begin(5); h.Run();
            h.Reset(); h.ScheduleTick(interrupted, 9);
            h.Begin(6); h.Run();
            h.SetCurrentObjectId(0); h.Interrupt(interrupted);
            h.Begin(9); h.Run();
            return h.Result(first, interrupted);
        }, "tick:10|seed:6|state:10:5:0:False|state:20:7:1:False");

        Compare("reset retains frame list until next lookup", h =>
        {
            var first = h.Entity(10); var interrupted = h.Entity(20);
            h.ScheduleTick(first, 5); h.Begin(5); h.Run();
            h.Reset(); h.ScheduleTick(interrupted, 9);
            h.Begin(6); h.SetCurrentObjectId(0); h.Interrupt(interrupted);
            h.Run(); h.Begin(9); h.Run();
            return h.Result(first, interrupted);
        }, "tick:10|seed:6|state:10:5:0:False|state:20:6:0:False");

        Compare("aborted run retains queue without RNG", h =>
        {
            var entity = h.Entity(20);
            h.ScheduleTick(entity, 5); h.Begin(5);
            h.Vm.Aborting = true; h.Run();
            h.Vm.Aborting = false; h.Run();
            return h.Result(entity);
        }, "tick:20|seed:1|state:20:5:0:False");

        Compare("dead entity is not queued", h =>
        {
            var entity = h.Entity(20); entity.Dead = true;
            h.ScheduleTick(entity, 5); h.Begin(5); h.Run();
            return h.Result(entity);
        }, "seed:1|state:20:0:0:True");

        Compare("reentrant tick replaces outer live-list field", h =>
        {
            var first = h.Entity(10); var skipped = h.Entity(20); var inner = h.Entity(30);
            bool reentered = false;
            first.OnTick = () =>
            {
                if (reentered) return;
                reentered = true;
                h.Begin(6);
                h.Run();
            };
            h.ScheduleTick(first, 5); h.ScheduleTick(skipped, 5); h.ScheduleTick(inner, 6);
            h.Begin(5); h.Run();
            return h.Result(first, skipped, inner);
        }, "tick:10|tick:30|seed:6|state:10:5:0:False|state:20:5:0:False|state:30:6:0:False");

        Compare("mutable signed ObjectIDs use live comparisons without resorting", h =>
        {
            var a = h.Entity(-1); var b = h.Entity(10); var c = h.Entity(0);
            h.ScheduleTick(a, 5); h.ScheduleTick(b, 5);
            a.ObjectID = short.MaxValue; b.ObjectID = short.MinValue;
            h.ScheduleTick(c, 5); h.Begin(5); h.Run();
            return h.Result(a, b, c);
        }, null);

        Compare("mutable IDs can insert the same reference twice", h =>
        {
            var a = h.Entity(10); var b = h.Entity(20); var c = h.Entity(30);
            h.ScheduleTick(a, 5); h.ScheduleTick(b, 5); h.ScheduleTick(c, 5);
            a.ObjectID = 40; h.ScheduleTick(a, 5);
            h.Begin(5); h.Run();
            return h.Result(a, b, c);
        }, "tick:40|tick:20|tick:30|tick:40|seed:3|state:40:5:0:False|state:20:5:0:False|state:30:5:0:False");

        Compare("insertion before current index can tick same entity again", h =>
        {
            var first = h.Entity(10); var later = h.Entity(30); var before = h.Entity(5);
            bool inserted = false;
            first.OnTick = () => { if (!inserted) { inserted = true; h.ScheduleCurrentTick(before); } };
            h.ScheduleTick(first, 5); h.ScheduleTick(later, 5); h.Begin(5); h.Run();
            return h.Result(first, later, before);
        }, "tick:10|tick:10|tick:30|seed:3|state:10:5:0:False|state:30:5:0:False|state:5:5:0:False");

        Compare("same reference remains queued in an older tick bucket", h =>
        {
            var entity = h.Entity(10);
            h.ScheduleTick(entity, 5); h.ScheduleTick(entity, 7); h.Deschedule(entity);
            h.Begin(5); h.Run(); h.Begin(7); h.Run();
            return h.Result(entity);
        }, "tick:10|seed:2|state:10:7:0:False");

        Compare("reentrant missing lookup clears alias and outer run throws", h =>
        {
            var first = h.Entity(10); var skipped = h.Entity(20);
            first.OnTick = () => { h.Begin(6); h.Run(); };
            h.ScheduleTick(first, 5); h.ScheduleTick(skipped, 5); h.Begin(5);
            try { h.Run(); throw new Exception("Expected missing-list exception"); }
            catch (NullReferenceException) { h.Vm.Trace.Add("missing-list:null"); }
            return h.Result(first, skipped);
        }, null);

        Compare("aborted run does not clear stale current alias", h =>
        {
            var first = h.Entity(10); var interrupted = h.Entity(20);
            h.ScheduleTick(first, 5); h.Begin(5); h.Run(); h.Reset();
            h.Begin(6); h.Vm.Aborting = true; h.Run(); h.Vm.Aborting = false;
            h.ScheduleTick(interrupted, 9); h.SetCurrentObjectId(0); h.Interrupt(interrupted);
            h.Run(); h.Begin(9); h.Run();
            return h.Result(first, interrupted);
        }, null);

        Compare("save-resume reset rebuilds tick1 then migrates restored clock", h =>
        {
            var entity = h.Entity(10);
            h.ScheduleTick(entity, 5); h.Begin(5); h.Run();
            h.Reset(); h.ScheduleTickIn(entity, 1); h.Begin(500); h.Run();
            return h.Result(entity);
        }, "tick:10|tick:10|seed:2|state:10:1:1:False");

        Console.WriteLine("Compared " + count + " scenarios against the original scheduler source.");
        NativeQueueTests.Run();
    }
}
