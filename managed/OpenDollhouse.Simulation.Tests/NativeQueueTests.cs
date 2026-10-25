using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.CompilerServices;
using System.Threading;
using OpenDollhouse.Simulation;
using FSO.SimAntics.Engine;

internal static class NativeQueueTests
{
    private sealed class Node { internal short Id; internal object Owner; }
    private static int groups;
    private static void Check(bool value, string message) { if (!value) throw new Exception(message); }
    private static void Throws<T>(Action action) where T : Exception
    {
        try { action(); } catch (T) { return; }
        throw new Exception("Expected " + typeof(T).Name);
    }
    private static void Group(string name, Action action)
    {
        action(); groups++; Console.WriteLine("PASS native ownership: " + name);
    }
    private static WakeQueue<Node> Queue(uint buckets = 32, uint entries = 128, ulong bytes = 1048576)
    {
        return new WakeQueue<Node>(node => node.Id, new WakeQueueLimits(buckets, entries, bytes));
    }
    internal static void ExpectMissingNative()
    {
        Throws<DllNotFoundException>(() => { using (var queue = Queue()) { } });
        Console.WriteLine("PASS missing native simulation fails without fallback");
    }
    internal static void Run()
    {
        Group("null current and missing bucket errors are not silent", () =>
        {
            using (var queue = Queue())
            {
                Check(!queue.SelectCurrentTick(1), "Unexpected current list");
                Throws<NullReferenceException>(() => { int ignored = queue.CurrentCount; });
                Throws<NullReferenceException>(() => queue.ScheduleCurrentTick(new Node { Id = 10 }));
                Throws<KeyNotFoundException>(() => queue.Deschedule(new Node { Id = 10 }, 9, 1));
                Check(queue.GetStats().RetainedTokens == 0, "Failed candidates retained");
            }
        });
        Group("detached current alias owns references until missing lookup", () =>
        {
            using (var queue = Queue())
            {
                var weak = AddDetached(queue);
                queue.Reset();
                ForceGc();
                Check(weak.IsAlive, "Detached alias lost entity");
                var stats = queue.GetStats();
                Check(stats.ScheduledBuckets == 0 && stats.LiveLists == 1 && stats.LiveEntries == 1 &&
                    stats.RetainedTokens == 1 && stats.HasCurrentAlias, "Detached alias accounting");
                queue.ScheduleCurrentTick(new Node { Id = 20 });
                Check(queue.CurrentCount == 2, "Stale alias cannot be mutated");
                Check(!queue.SelectCurrentTick(99), "Missing lookup hit");
                Check(queue.GetStats().RetainedTokens == 0 && queue.GetStats().LiveEntries == 0, "Alias release retained tokens");
                ForceGc(); Check(!weak.IsAlive, "Cleared alias still roots entity");
                GC.KeepAlive(queue);
            }
        });
        Group("deduplicated candidate is collectible immediately", () =>
        {
            using (var queue = Queue())
            {
                var weak = AddDuplicate(queue);
                Check(queue.GetStats().RetainedTokens == 1 && queue.GetStats().LiveEntries == 1, "Dedup registry leak");
                ForceGc(); Check(!weak.IsAlive, "Deduplicated candidate retained");
                GC.KeepAlive(queue);
            }
        });
        Group("one identity in multiple buckets releases per occurrence", () =>
        {
            using (var queue = Queue())
            {
                var entity = new Node { Id = 1 };
                queue.Schedule(entity, 5); queue.Schedule(entity, 7);
                Check(queue.GetStats().LiveEntries == 2 && queue.GetStats().RetainedTokens == 1, "Multi-bucket accounting");
                queue.SelectCurrentTick(5); queue.CompleteCurrentTick(5); queue.SelectCurrentTick(7);
                Check(queue.GetStats().LiveEntries == 1 && object.ReferenceEquals(queue.CurrentAt(0), entity), "Wrong occurrence released");
                queue.CompleteCurrentTick(7); queue.SelectCurrentTick(99);
                Check(queue.GetStats().RetainedTokens == 0, "Last occurrence not released");
            }
        });
        Group("same token duplicated by mutable IDs retains all occurrences", () =>
        {
            using (var queue = Queue())
            {
                var a = new Node { Id = 10 }; var b = new Node { Id = 20 }; var c = new Node { Id = 30 };
                queue.Schedule(a, 5); queue.Schedule(b, 5); queue.Schedule(c, 5);
                a.Id = 40; queue.Schedule(a, 5); queue.SelectCurrentTick(5);
                Check(queue.CurrentCount == 4 && queue.GetStats().RetainedTokens == 3, "Same-token occurrences collapsed");
                queue.Deschedule(a, 5, 0);
                Check(queue.CurrentCount == 3 && object.ReferenceEquals(queue.CurrentAt(2), a), "Removal was not first reference only");
                Check(queue.GetStats().RetainedTokens == 3, "Premature token release");
                queue.Reset(); queue.SelectCurrentTick(99);
                Check(queue.GetStats().RetainedTokens == 0, "Duplicate occurrence leak");
            }
        });
        Group("bucket and entry capacities fail without queue mutation or token leak", () =>
        {
            using (var queue = Queue(1, 2))
            {
                var a = new Node { Id = 1 }; var b = new Node { Id = 2 };
                queue.Schedule(a, 5);
                Throws<WakeQueueCapacityException>(() => queue.Schedule(new Node { Id = 3 }, 6));
                queue.Schedule(b, 5);
                Throws<WakeQueueCapacityException>(() => queue.Schedule(new Node { Id = 3 }, 5));
                queue.SelectCurrentTick(5);
                var stats = queue.GetStats();
                Check(queue.CurrentCount == 2 && stats.RetainedTokens == 2 && stats.LiveEntries == 2 && stats.ScheduledBuckets == 1,
                    "Capacity error changed logical queue");
                Check(object.ReferenceEquals(queue.CurrentAt(0), a) && object.ReferenceEquals(queue.CurrentAt(1), b), "Capacity error changed order");
            }
            Throws<WakeQueueCapacityException>(() => { using (var ignored = Queue(1, 1, 1)) { } });
        });
        Group("selector exception propagates without insertion or candidate leak", () =>
        {
            bool fail = false;
            var expected = new InvalidOperationException("Synthetic selector failure");
            using (var queue = new WakeQueue<Node>(node => { if (fail) throw expected; return node.Id; }, new WakeQueueLimits(32, 128, 1048576)))
            {
                queue.Schedule(new Node { Id = 1 }, 5); fail = true;
                try { queue.Schedule(new Node { Id = 2 }, 5); throw new Exception("Selector unexpectedly succeeded"); }
                catch (InvalidOperationException actual) { Check(object.ReferenceEquals(actual, expected), "Selector exception replaced"); }
                fail = false; queue.SelectCurrentTick(5);
                Check(queue.CurrentCount == 1 && queue.GetStats().RetainedTokens == 1, "Failed insertion retained candidate");
            }
        });
        Group("callback reentry rejected without changing queue", () =>
        {
            WakeQueue<Node> queue = null;
            bool reenter = false;
            using (queue = new WakeQueue<Node>(node => { if (reenter) queue.Reset(); return node.Id; }, new WakeQueueLimits(32, 128, 1048576)))
            {
                queue.Schedule(new Node { Id = 1 }, 5); reenter = true;
                Throws<InvalidOperationException>(() => queue.Schedule(new Node { Id = 2 }, 5));
                reenter = false; queue.SelectCurrentTick(5);
                Check(queue.CurrentCount == 1 && queue.GetStats().RetainedTokens == 1, "Reentry changed queue");
            }
        });
        Group("serialized main-worker-main handoff supports async restore", () =>
        {
            using (var queue = Queue())
            {
                queue.Schedule(new Node { Id = 1 }, 5); queue.SelectCurrentTick(5);
                Exception failure = null;
                var worker = new Thread(() =>
                {
                    try { queue.Reset(); queue.Schedule(new Node { Id = 2 }, 1); }
                    catch (Exception ex) { failure = ex; }
                });
                worker.Start(); worker.Join();
                if (failure != null) throw failure;
                queue.BeginTick(500, 0); queue.SelectCurrentTick(500);
                Check(queue.CurrentCount == 1 && queue.CurrentAt(0).Id == 2, "Worker restore state not resumed");
            }
        });
        Group("disposed queue rejects use and releases entity graph", () =>
        {
            var weak = MakeDisposedGraph();
            ForceGc(); Check(!weak[0].IsAlive && !weak[1].IsAlive, "Disposed queue roots entity graph");
            var queue = Queue(); queue.Dispose(); queue.Dispose();
            Throws<ObjectDisposedException>(() => queue.Reset());
        });
        Group("unreachable queue-VM cycles finalize and become collectible", () =>
        {
            ForceGc();
            long before = WakeQueueHandle.SuccessfulReleases;
            var weak = new List<WeakReference>();
            for (int i = 0; i < 64; i++) weak.AddRange(MakeVmCycle());
            ForceGc();
            foreach (var item in weak) Check(!item.IsAlive, "VM/scheduler/queue cycle retained");
            Check(WakeQueueHandle.SuccessfulReleases >= before + 64, "Native SafeHandle finalizers did not destroy all queues");
        });
        Group("repeated create-reset-dispose releases native handles", () =>
        {
            long before = WakeQueueHandle.SuccessfulReleases;
            for (int i = 0; i < 128; i++)
                using (var queue = Queue()) { queue.Schedule(new Node { Id = 1 }, 1); queue.SelectCurrentTick(1); queue.Reset(); }
            Check(WakeQueueHandle.SuccessfulReleases == before + 128, "Deterministic native destroy count");
        });
        Group("host diagnostics cadence and failing writer preserve ticks", DiagnosticTests);
        Console.WriteLine("Passed " + groups + " native ownership/resource/lifetime groups.");
    }
    [MethodImpl(MethodImplOptions.NoInlining)]
    private static WeakReference AddDetached(WakeQueue<Node> queue)
    {
        var node = new Node { Id = 10 };
        queue.Schedule(node, 5); queue.SelectCurrentTick(5); queue.CompleteCurrentTick(5);
        return new WeakReference(node);
    }
    [MethodImpl(MethodImplOptions.NoInlining)]
    private static WeakReference AddDuplicate(WakeQueue<Node> queue)
    {
        queue.Schedule(new Node { Id = 10 }, 5);
        var duplicate = new Node { Id = 10 }; queue.Schedule(duplicate, 5);
        return new WeakReference(duplicate);
    }
    [MethodImpl(MethodImplOptions.NoInlining)]
    private static WeakReference[] MakeDisposedGraph()
    {
        var queue = Queue(); var node = new Node { Id = 1, Owner = queue }; queue.Schedule(node, 5);
        queue.Dispose(); return new[] { new WeakReference(queue), new WeakReference(node) };
    }
    [MethodImpl(MethodImplOptions.NoInlining)]
    private static WeakReference[] MakeVmCycle()
    {
        var vm = new VM(); var scheduler = new VMScheduler(vm); vm.Scheduler = scheduler;
        var entity = new VMEntity { ObjectID = 1, Owner = vm }; vm.Entities.Add(entity); scheduler.ScheduleTick(entity, 5);
        return new[] { new WeakReference(vm), new WeakReference(scheduler), new WeakReference(entity) };
    }
    private static void ForceGc()
    {
        for (int i = 0; i < 3; i++) { GC.Collect(); GC.WaitForPendingFinalizers(); GC.Collect(); }
    }
    private static void DiagnosticTests()
    {
        var prior = Console.Out; string env = Environment.GetEnvironmentVariable("ODH_VM_DIAGNOSTICS");
        try
        {
            Environment.SetEnvironmentVariable("ODH_VM_DIAGNOSTICS", "1");
            var output = new StringWriter(); Console.SetOut(output);
            var vm = new VM(); var entity = new VMEntity { ObjectID = 1, Owner = vm }; vm.Entities.Add(entity);
            using (var scheduler = new VMScheduler(vm))
            {
                scheduler.ScheduleTick(entity, 5);
                for (uint frame = 0; frame < 256; frame++) { scheduler.BeginTick(5 + frame); scheduler.RunTick(); }
                Check(output.ToString().Split(new[] { "ODH native wake queue: " }, StringSplitOptions.None).Length == 3, "Wrong diagnostic cadence");
                Check(output.ToString().Contains("\"CompletedFrames\":256"), "No later resource summary");
            }
            Console.SetOut(new ThrowingWriter());
            using (var scheduler = new VMScheduler(vm))
            {
                scheduler.ScheduleTick(entity, 500); scheduler.BeginTick(500); scheduler.RunTick();
                Check(vm.Trace.Count == 2 && vm.Context.RandomSeed == 257, "Diagnostic IO changed tick behavior");
            }
        }
        finally { Console.SetOut(prior); Environment.SetEnvironmentVariable("ODH_VM_DIAGNOSTICS", env); }
    }
    private sealed class ThrowingWriter : TextWriter
    {
        public override System.Text.Encoding Encoding { get { return System.Text.Encoding.UTF8; } }
        public override void WriteLine(string value) { throw new IOException("Synthetic diagnostic IO failure"); }
    }
}
