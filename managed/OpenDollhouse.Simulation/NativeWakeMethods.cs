using System;
using System.Runtime.InteropServices;
using System.Runtime.CompilerServices;

[assembly: InternalsVisibleTo("OpenDollhouse.Simulation.Tests")]

namespace OpenDollhouse.Simulation
{
    // No global closure or GCHandle roots the queue/VM. SafeHandle owns this
    // non-generic callback target and its state through the final native release.
    internal abstract class WakeRegistry
    {
        internal Exception CallbackFailure;
        internal bool CallbackProblem;
        internal abstract bool TryLookup(ulong token, out short id);
        internal abstract void RetainToken(ulong token);
        internal abstract void ReleaseToken(ulong token);
        internal abstract void Sweep();
        internal abstract void Clear();
    }

    internal sealed class WakeCallbacks
    {
        private readonly WakeRegistry state;
        internal readonly NativeWakeMethods.LookupId Lookup;
        internal readonly NativeWakeMethods.TokenAction Retain;
        internal readonly NativeWakeMethods.TokenAction Release;
        internal WakeCallbacks(WakeRegistry state)
        {
            this.state = state;
            Lookup = LookupImpl;
            Retain = RetainImpl;
            Release = ReleaseImpl;
        }
        private int LookupImpl(IntPtr ignored, ulong token, out short id)
        {
            id = 0;
            try
            {
                if (state.TryLookup(token, out id)) return 0;
                state.CallbackProblem = true;
            }
            catch (Exception exception) { state.CallbackFailure = exception; }
            return 1;
        }
        private void RetainImpl(IntPtr ignored, ulong token) { state.RetainToken(token); }
        private void ReleaseImpl(IntPtr ignored, ulong token) { state.ReleaseToken(token); }
        internal void Clear() { state.Clear(); }
    }

    internal sealed class WakeQueueHandle : SafeHandle
    {
        internal static long SuccessfulReleases;
        private readonly WakeCallbacks callbacks;
        internal WakeQueueHandle(WakeCallbacks callbacks) : base(IntPtr.Zero, true)
        {
            this.callbacks = callbacks;
        }
        internal void Initialize(IntPtr pointer) { SetHandle(pointer); }
        public override bool IsInvalid { get { return handle == IntPtr.Zero; } }
        protected override bool ReleaseHandle()
        {
            bool result = false;
            try
            {
                result = NativeWakeMethods.Destroy(handle) == 0;
                if (result) System.Threading.Interlocked.Increment(ref SuccessfulReleases);
            }
            catch { /* Finalization must never throw into the host. */ }
            finally { callbacks.Clear(); GC.KeepAlive(callbacks); }
            return result;
        }
    }

    internal static class NativeWakeMethods
    {
        private const string Library = "odh_simulation";
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate int LookupId(IntPtr context, ulong token, out short id);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate void TokenAction(IntPtr context, ulong token);
        [DllImport(Library, EntryPoint = "odh_wake_abi_version", ExactSpelling = true, CallingConvention = CallingConvention.Cdecl)]
        internal static extern uint AbiVersion();
        [DllImport(Library, EntryPoint = "odh_wake_create_v1", ExactSpelling = true, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int Create(IntPtr context, LookupId lookup, TokenAction retain, TokenAction release,
            uint maxBuckets, uint maxEntries, ulong maxBytes, out IntPtr queue);
        [DllImport(Library, EntryPoint = "odh_wake_destroy_v1", ExactSpelling = true, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int Destroy(IntPtr queue);
        [DllImport(Library, EntryPoint = "odh_wake_schedule_v1", ExactSpelling = true, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int Schedule(WakeQueueHandle queue, ulong token, uint tick);
        [DllImport(Library, EntryPoint = "odh_wake_schedule_current_v1", ExactSpelling = true, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int ScheduleCurrent(WakeQueueHandle queue, ulong token);
        [DllImport(Library, EntryPoint = "odh_wake_deschedule_v1", ExactSpelling = true, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int Deschedule(WakeQueueHandle queue, ulong token, uint scheduledEnd, uint currentTick);
        [DllImport(Library, EntryPoint = "odh_wake_prepare_interrupt_v1", ExactSpelling = true, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int PrepareInterrupt(WakeQueueHandle queue, ulong token, uint scheduledEnd,
            uint currentTick, short currentObjectId, out uint target);
        [DllImport(Library, EntryPoint = "odh_wake_begin_v1", ExactSpelling = true, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int Begin(WakeQueueHandle queue, uint tick, uint previousTick);
        [DllImport(Library, EntryPoint = "odh_wake_select_current_v1", ExactSpelling = true, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int SelectCurrent(WakeQueueHandle queue, uint tick, out uint found);
        [DllImport(Library, EntryPoint = "odh_wake_current_count_v1", ExactSpelling = true, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int CurrentCount(WakeQueueHandle queue, out uint count);
        [DllImport(Library, EntryPoint = "odh_wake_current_at_v1", ExactSpelling = true, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int CurrentAt(WakeQueueHandle queue, uint index, out ulong token);
        [DllImport(Library, EntryPoint = "odh_wake_complete_v1", ExactSpelling = true, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int Complete(WakeQueueHandle queue, uint tick);
        [DllImport(Library, EntryPoint = "odh_wake_reset_v1", ExactSpelling = true, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int Reset(WakeQueueHandle queue);
        [DllImport(Library, EntryPoint = "odh_wake_stats_v1", ExactSpelling = true, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int Stats(WakeQueueHandle queue, out uint scheduledBuckets, out uint peakScheduledBuckets,
            out uint liveLists, out uint liveEntries, out uint peakEntries, out uint currentAlias,
            out ulong liveBytes, out ulong peakBytes);
    }
}
