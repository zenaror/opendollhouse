// SPDX-License-Identifier: MPL-2.0
using System;
using System.Collections.Generic;
using System.Runtime.CompilerServices;
using System.Runtime.ExceptionServices;
using System.Threading;

namespace OpenDollhouse.Simulation
{
    public sealed class WakeQueueLimits
    {
        public uint MaxBuckets { get; private set; }
        public uint MaxEntries { get; private set; }
        public ulong MaxBytes { get; private set; }
        public WakeQueueLimits(uint maxBuckets, uint maxEntries, ulong maxBytes)
        {
            if (maxBuckets == 0 || maxEntries == 0 || maxEntries > int.MaxValue || maxBytes == 0)
                throw new ArgumentOutOfRangeException("limits");
            MaxBuckets = maxBuckets; MaxEntries = maxEntries; MaxBytes = maxBytes;
        }
    }

    public sealed class WakeQueueStats
    {
        public uint ScheduledBuckets { get; internal set; }
        public uint PeakScheduledBuckets { get; internal set; }
        public uint LiveLists { get; internal set; }
        public uint LiveEntries { get; internal set; }
        public uint PeakEntries { get; internal set; }
        public bool HasCurrentAlias { get; internal set; }
        public ulong LiveBytes { get; internal set; }
        public ulong PeakBytes { get; internal set; }
        public int RetainedTokens { get; internal set; }
        public int PeakRegisteredTokens { get; internal set; }
    }

    public sealed class WakeQueueCapacityException : InvalidOperationException
    {
        internal WakeQueueCapacityException() : base("Native wake queue resource capacity exceeded; no entries were discarded.") { }
    }

    /// <summary>Native ID-ordered wake lists for reference-identity entities.
    /// CurrentCount/CurrentAt read the replaceable live alias after reentrant
    /// execution. The host owns ticks/execution. No managed queue fallback.</summary>
    public sealed class WakeQueue<T> : IDisposable where T : class
    {
        public const uint AbiVersion = 1;
        private readonly Registry state;
        private readonly WakeQueueHandle handle;
        private readonly object operationGate = new object();
        private bool inNativeCall;

        public WakeQueue(Func<T, short> objectId, WakeQueueLimits limits)
        {
            if (objectId == null) throw new ArgumentNullException("objectId");
            if (limits == null) throw new ArgumentNullException("limits");
            uint actual = NativeWakeMethods.AbiVersion();
            if (actual != AbiVersion) throw new NotSupportedException("odh_simulation ABI " + actual + "; expected 1.");
            state = new Registry(objectId);
            var callbacks = new WakeCallbacks(state);
            handle = new WakeQueueHandle(callbacks);
            IntPtr pointer;
            int status = NativeWakeMethods.Create(IntPtr.Zero, callbacks.Lookup, callbacks.Retain, callbacks.Release,
                limits.MaxBuckets, limits.MaxEntries, limits.MaxBytes, out pointer);
            CheckStatus(status);
            if (pointer == IntPtr.Zero) throw new InvalidOperationException("Native wake queue returned a null handle.");
            handle.Initialize(pointer);
        }
        public void Schedule(T entity, uint tick)
        {
            BeginOperation();
            try { CheckStatus(NativeWakeMethods.Schedule(handle, state.Register(entity), tick)); }
            finally { EndOperation(); }
        }
        public void ScheduleCurrentTick(T entity)
        {
            BeginOperation();
            try { CheckStatus(NativeWakeMethods.ScheduleCurrent(handle, state.Register(entity))); }
            finally { EndOperation(); }
        }
        public void Deschedule(T entity, uint scheduledEnd, uint currentTick)
        {
            BeginOperation();
            try { CheckStatus(NativeWakeMethods.Deschedule(handle, state.Register(entity), scheduledEnd, currentTick)); }
            finally { EndOperation(); }
        }
        public WakeTarget PrepareInterrupt(T entity, uint scheduledEnd, uint currentTick, short currentObjectId)
        {
            BeginOperation();
            try
            {
                uint target;
                CheckStatus(NativeWakeMethods.PrepareInterrupt(handle, state.Register(entity), scheduledEnd,
                    currentTick, currentObjectId, out target));
                if (target > 2) throw new InvalidOperationException("Invalid native wake target.");
                return (WakeTarget)target;
            }
            finally { EndOperation(); }
        }
        public void BeginTick(uint tick, uint previousTick)
        {
            BeginOperation();
            try { CheckStatus(NativeWakeMethods.Begin(handle, tick, previousTick)); }
            finally { EndOperation(); }
        }
        public bool SelectCurrentTick(uint tick)
        {
            BeginOperation();
            try
            {
                uint found;
                CheckStatus(NativeWakeMethods.SelectCurrent(handle, tick, out found));
                if (found > 1) throw new InvalidOperationException("Invalid native lookup result.");
                return found != 0;
            }
            finally { EndOperation(); }
        }
        public int CurrentCount
        {
            get
            {
                BeginOperation();
                try
                {
                    uint count;
                    CheckStatus(NativeWakeMethods.CurrentCount(handle, out count));
                    return checked((int)count);
                }
                finally { EndOperation(); }
            }
        }
        public T CurrentAt(int index)
        {
            if (index < 0) throw new ArgumentOutOfRangeException("index");
            BeginOperation();
            try
            {
                ulong token;
                CheckStatus(NativeWakeMethods.CurrentAt(handle, (uint)index, out token));
                return state.Resolve(token);
            }
            finally { EndOperation(); }
        }
        public void CompleteCurrentTick(uint tick)
        {
            BeginOperation();
            try { CheckStatus(NativeWakeMethods.Complete(handle, tick)); }
            finally { EndOperation(); }
        }
        public void Reset()
        {
            BeginOperation();
            try { CheckStatus(NativeWakeMethods.Reset(handle)); }
            finally { EndOperation(); }
        }
        public WakeQueueStats GetStats()
        {
            BeginOperation();
            try
            {
                uint buckets, peakBuckets, lists, entries, peakEntries, alias;
                ulong bytes, peakBytes;
                CheckStatus(NativeWakeMethods.Stats(handle, out buckets, out peakBuckets, out lists,
                    out entries, out peakEntries, out alias, out bytes, out peakBytes));
                return new WakeQueueStats { ScheduledBuckets = buckets, PeakScheduledBuckets = peakBuckets,
                    LiveLists = lists, LiveEntries = entries, PeakEntries = peakEntries,
                    HasCurrentAlias = alias != 0, LiveBytes = bytes, PeakBytes = peakBytes,
                    RetainedTokens = state.Count, PeakRegisteredTokens = state.PeakCount };
            }
            finally { EndOperation(); }
        }
        public void Dispose()
        {
            if (!Monitor.TryEnter(operationGate)) throw new InvalidOperationException("Concurrent wake queue access.");
            try
            {
                if (inNativeCall) throw new InvalidOperationException("Cannot dispose the wake queue during a native callback.");
                handle.Dispose();
                state.Clear();
            }
            finally { Monitor.Exit(operationGate); }
        }
        private void BeginOperation()
        {
            if (!Monitor.TryEnter(operationGate)) throw new InvalidOperationException("Concurrent wake queue access.");
            try
            {
                if (handle.IsClosed) throw new ObjectDisposedException("WakeQueue");
                if (inNativeCall) throw new InvalidOperationException("A wake queue callback cannot reenter the queue.");
                inNativeCall = true;
                state.CallbackFailure = null;
                state.CallbackProblem = false;
            }
            catch { Monitor.Exit(operationGate); throw; }
        }
        private void EndOperation()
        {
            try { state.Sweep(); }
            finally
            {
                state.CallbackFailure = null;
                inNativeCall = false;
                Monitor.Exit(operationGate);
                GC.KeepAlive(handle);
            }
        }
        private void CheckStatus(int status)
        {
            if (state.CallbackFailure != null) ExceptionDispatchInfo.Capture(state.CallbackFailure).Throw();
            if (state.CallbackProblem) throw new InvalidOperationException("Native wake token registry mismatch.");
            switch (status)
            {
                case 0: return;
                case 1: throw new ArgumentException("Invalid native wake queue argument.");
                case 2: throw new NullReferenceException("There is no current wake list.");
                case 3: throw new KeyNotFoundException("The scheduled wake bucket does not exist.");
                case 4: throw new WakeQueueCapacityException();
                case 5: throw new InvalidOperationException("Native wake ObjectID lookup failed.");
                case 6: throw new InvalidOperationException("Internal native wake queue error.");
                case 7: throw new InvalidOperationException("A native wake callback attempted queue reentry.");
                default: throw new InvalidOperationException("Unknown native wake queue status: " + status);
            }
        }
        private sealed class Entry
        {
            internal T Entity;
            internal ulong Token;
            internal int References;
            internal Entry NextZero;
            internal bool Pending;
        }
        private sealed class ReferenceComparer : IEqualityComparer<T>
        {
            public bool Equals(T a, T b) { return object.ReferenceEquals(a, b); }
            public int GetHashCode(T value) { return RuntimeHelpers.GetHashCode(value); }
        }
        private sealed class Registry : WakeRegistry
        {
            private readonly Func<T, short> objectId;
            private readonly Dictionary<T, Entry> byEntity = new Dictionary<T, Entry>(new ReferenceComparer());
            private readonly Dictionary<ulong, Entry> byToken = new Dictionary<ulong, Entry>();
            private Entry zeroHead;
            private ulong nextToken;
            internal int Count { get { return byToken.Count; } }
            internal int PeakCount;
            internal Registry(Func<T, short> objectId) { this.objectId = objectId; }
            internal ulong Register(T entity)
            {
                if (entity == null) throw new ArgumentNullException("entity");
                Entry entry;
                if (byEntity.TryGetValue(entity, out entry)) return entry.Token;
                if (nextToken == ulong.MaxValue) throw new InvalidOperationException("Wake identity token space exhausted.");
                entry = new Entry { Entity = entity, Token = ++nextToken };
                byEntity.Add(entity, entry);
                try { byToken.Add(entry.Token, entry); }
                catch { byEntity.Remove(entity); throw; }
                QueueZero(entry);
                if (Count > PeakCount) PeakCount = Count;
                return entry.Token;
            }
            internal T Resolve(ulong token) { return byToken[token].Entity; }
            internal override bool TryLookup(ulong token, out short id)
            {
                Entry entry; id = 0;
                if (!byToken.TryGetValue(token, out entry)) return false;
                id = objectId(entry.Entity);
                return true;
            }
            internal override void RetainToken(ulong token)
            {
                Entry entry;
                if (!byToken.TryGetValue(token, out entry)) { CallbackProblem = true; return; }
                entry.References++;
            }
            internal override void ReleaseToken(ulong token)
            {
                Entry entry;
                if (!byToken.TryGetValue(token, out entry) || entry.References == 0) { CallbackProblem = true; return; }
                entry.References--;
                if (entry.References == 0) QueueZero(entry);
            }
            private void QueueZero(Entry entry)
            {
                if (entry.Pending) return;
                entry.Pending = true;
                entry.NextZero = zeroHead;
                zeroHead = entry;
            }
            internal override void Sweep()
            {
                while (zeroHead != null)
                {
                    var entry = zeroHead;
                    zeroHead = entry.NextZero;
                    entry.NextZero = null; entry.Pending = false;
                    if (entry.References != 0) continue;
                    byToken.Remove(entry.Token);
                    byEntity.Remove(entry.Entity);
                }
            }
            internal override void Clear()
            {
                byToken.Clear(); byEntity.Clear(); zeroHead = null; CallbackFailure = null;
            }
        }
    }
    public enum WakeTarget { None, CurrentTick, NextTick }
}
