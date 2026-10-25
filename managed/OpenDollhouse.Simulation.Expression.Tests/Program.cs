// SPDX-License-Identifier: MPL-2.0
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Threading;
using FSO.SimAntics.Engine;
using FSO.SimAntics.Engine.Primitives;
using FSO.SimAntics.Engine.Scopes;

internal static class Program
{
    private static int cases;

    private sealed class Observation
    {
        internal VMPrimitiveExitCode? Exit;
        internal Exception Error;
        internal string Trace;
        internal string Values;
    }

    private sealed class PlainContext { internal int Value = 7; }
    private sealed class ThrowingTextWriter : StringWriter
    {
        public override void WriteLine(string value) { throw new IOException("diagnostic writer failed"); }
    }
    private static int PlainRead(PlainContext context, byte scope, short index) { return context.Value; }
    private static bool PlainWrite(PlainContext context, byte scope, short index, int value)
    { context.Value = value; return true; }
    private static void PlainLevels(PlainContext context, out int caller, out int callee, out int offset)
    { caller = callee = offset = 0; }

    private static VMExpressionOperand Operand(int op, short lhsData = 1, short rhsData = 2,
        VMVariableScope lhsOwner = VMVariableScope.Temps, VMVariableScope rhsOwner = VMVariableScope.Temps)
    {
        return new VMExpressionOperand { Operator = (VMExpressionOperator)op, LhsData = lhsData,
            RhsData = rhsData, LhsOwner = lhsOwner, RhsOwner = rhsOwner, IsSigned = 1 };
    }

    private static VMStackFrame Frame(SyntheticMemory memory, bool ts1 = true)
    {
        var frame = new VMStackFrame { Memory = memory };
        frame.VM.TS1 = ts1;
        frame.Caller.Position.Level = 3;
        frame.Callee.Position.Level = 2;
        frame.Callee.Object.OBJ.LevelOffset = 1;
        return frame;
    }

    private static Observation Run(bool native, VMExpressionOperand operand, VMStackFrame frame)
    {
        var result = new Observation();
        try
        {
            result.Exit = native ? new VMExpression().Execute(frame, operand)
                : new BaselineVMExpression().Execute(frame, operand);
        }
        catch (Exception exception) { result.Error = exception; }
        result.Trace = string.Join("|", frame.Memory.Trace);
        result.Values = string.Join("|", frame.Memory.Values.OrderBy(x => x.Key)
            .Select(x => x.Key + "=" + x.Value));
        return result;
    }

    private static void AssertSame(string name, Observation baseline, Observation actual)
    {
        if (baseline.Exit != actual.Exit || baseline.Error?.GetType() != actual.Error?.GetType()
            || baseline.Error?.Message != actual.Error?.Message || baseline.Trace != actual.Trace
            || baseline.Values != actual.Values)
            throw new Exception(name + " baseline=" + Describe(baseline) + " native=" + Describe(actual));
        Interlocked.Increment(ref cases);
    }

    private static string Describe(Observation o)
    {
        return "[exit=" + o.Exit + ", error=" + o.Error?.GetType().Name + ":" + o.Error?.Message
            + ", trace=" + o.Trace + ", values=" + o.Values + "]";
    }

    private static SyntheticMemory Memory(VMExpressionOperand op, int lhs, int rhs, bool acceptWrites = true)
    {
        var memory = new SyntheticMemory { AcceptWrites = acceptWrites };
        memory.Values[(byte)op.LhsOwner + ":" + op.LhsData] = lhs;
        memory.Values[(byte)op.RhsOwner + ":" + op.RhsData] = rhs;
        return memory;
    }

    private static void Compare(string name, VMExpressionOperand op, int lhs, int rhs,
        bool acceptWrites = true, Action<VMStackFrame> setup = null)
    {
        var left = Frame(Memory(op, lhs, rhs, acceptWrites));
        var right = Frame(left.Memory.Clone());
        if (setup != null) { setup(left); setup(right); }
        AssertSame(name, Run(false, op, left), Run(true, op, right));
    }

    private static void AllOperators()
    {
        for (int op = 0; op <= 20; op++)
        {
            Compare("opcode " + op, Operand(op), 7, 3);
            Compare("opcode alias " + op, Operand(op, 1, 1), -9, 31);
            Compare("opcode write-false " + op, Operand(op), 7, 3, false);
        }
        Compare("unknown operator", Operand(255), 4, 6);
        Compare("unknown encoded operator", Operand(21), 4, 6);
        Compare("operator enum beyond byte", Operand(256), 4, 6);
        Compare("negative operator enum", Operand(-1), 4, 6);
        Compare("distinct scopes", Operand(3, 1, 2, VMVariableScope.TempXL, VMVariableScope.Literal),
            int.MaxValue, -3);
        var unsignedFlag = Operand(0);
        unsignedFlag.IsSigned = 0;
        Compare("signed flag ignored by source", unsignedFlag, -3, 1);
        Compare("signed flag", Operand(8), -1, 32);
        Compare("flag shift zero", Operand(9), -1, 0);
        Compare("flag shift 64", Operand(10), int.MinValue, 64);
        Compare("division by zero", Operand(7), 19, 0);
        Compare("modulo by zero", Operand(12), 19, 0);
        Compare("division overflow", Operand(7), int.MinValue, -1);
        Compare("modulo overflow", Operand(12), int.MinValue, -1);
        Compare("modulo wrapped sum", Operand(12), int.MaxValue - 1, int.MaxValue);
        Compare("increment overflow", Operand(11), int.MaxValue, -3);
        Compare("decrement overflow", Operand(17), int.MinValue, 4);
        Compare("addition overflow", Operand(3), int.MaxValue, 1);
        Compare("multiplication overflow", Operand(6), int.MinValue, 3);
        foreach (int value in new[] { -1, 0, 1, 1073676288, 1073741824, int.MaxValue })
            Compare("sqrt " + value, Operand(20), 0, value);
        Compare("room less below", Operand(1, 29, 2, VMVariableScope.MyObject), 5, 1024);
        Compare("room greater equal above", Operand(14, 29, 2, VMVariableScope.MyObject), 5, 1024);
        Compare("room ordinary no levels", Operand(1, 29, 2, VMVariableScope.MyObject), 5, 1023);
    }

    private static void CallbackFailures()
    {
        var op = Operand(3);
        Compare("read failure", op, 5, 7, setup: f => f.Memory.OnRead = (ctx, scope, index) => {
            if (index == op.RhsData) throw new InvalidOperationException("read fault");
        });
        Compare("write failure", op, 5, 7, setup: f => f.Memory.OnWrite = (ctx, scope, index, value) =>
            throw new InvalidOperationException("write fault"));
        // Inc/Dec write before reading RHS. A failing RHS callback must retain
        // the earlier LHS effect, without an artificial native rollback.
        Compare("increment then read failure", Operand(11), 5, 7, setup: f =>
            f.Memory.OnRead = (ctx, scope, index) => {
                if (index == 2) throw new InvalidOperationException("read after write");
            });
        var room = Operand(1, 29, 2, VMVariableScope.MyObject);
        var baseline = Frame(Memory(room, 1, 1024));
        var native = Frame(baseline.Memory.Clone());
        baseline.Callee = native.Callee = null;
        AssertSame("level callback failure", Run(false, room, baseline), Run(true, room, native));

        var lazy = Frame(Memory(room, 1, 1023));
        var lazyNative = Frame(lazy.Memory.Clone());
        lazy.Callee = lazyNative.Callee = null;
        AssertSame("level callback remains lazy", Run(false, room, lazy), Run(true, room, lazyNative));

        var getterFirst = Frame(Memory(room, 1, 1024));
        var getterFirstNative = Frame(getterFirst.Memory.Clone());
        getterFirst.Callee = getterFirstNative.Callee = null;
        getterFirst.Memory.OnRead = getterFirstNative.Memory.OnRead = (ctx, scope, index) => {
            if (index == room.RhsData) throw new InvalidOperationException("getter before levels");
        };
        AssertSame("getter failure precedes levels", Run(false, room, getterFirst),
            Run(true, room, getterFirstNative));

        var marker = new InvalidOperationException("same callback instance");
        var baselineIdentity = Frame(Memory(op, 5, 7));
        var nativeIdentity = Frame(baselineIdentity.Memory.Clone());
        baselineIdentity.Memory.OnRead = nativeIdentity.Memory.OnRead = (ctx, scope, index) => throw marker;
        var left = Run(false, op, baselineIdentity);
        var right = Run(true, op, nativeIdentity);
        AssertSame("callback exception identity", left, right);
        if (!ReferenceEquals(left.Error, marker) || !ReferenceEquals(right.Error, marker))
            throw new Exception("Callback exception was replaced at the native boundary.");
    }

    private static void ReentrantAndThreads()
    {
        var outer = Operand(11);
        var inner = Operand(3, 3, 4);
        foreach (bool native in new[] { false, true })
        {
            var memory = Memory(outer, 5, 9);
            memory.Values["8:3"] = 20;
            memory.Values["8:4"] = 22;
            var frame = Frame(memory);
            bool fired = false;
            memory.OnRead = (ctx, scope, index) => {
                if (!fired && index == outer.RhsData)
                {
                    fired = true;
                    if (native) new VMExpression().Execute(ctx, inner);
                    else new BaselineVMExpression().Execute(ctx, inner);
                }
            };
            var outcome = Run(native, outer, frame);
            if (native)
            {
                var baselineMemory = Memory(outer, 5, 9);
                baselineMemory.Values["8:3"] = 20;
                baselineMemory.Values["8:4"] = 22;
                bool baselineFired = false;
                baselineMemory.OnRead = (ctx, scope, index) => {
                    if (!baselineFired && index == outer.RhsData)
                    {
                        baselineFired = true;
                        new BaselineVMExpression().Execute(ctx, inner);
                    }
                };
                AssertSame("reentrant expression", Run(false, outer, Frame(baselineMemory)), outcome);
            }
        }
        Observation NestedFailure(bool useNative)
        {
            var memory = Memory(outer, 5, 9);
            memory.Values["8:3"] = int.MinValue;
            memory.Values["8:4"] = -1;
            var frame = Frame(memory);
            bool fired = false;
            memory.OnRead = (ctx, scope, index) => {
                if (!fired && index == outer.RhsData)
                {
                    fired = true;
                    if (useNative) new VMExpression().Execute(ctx, Operand(7, 3, 4));
                    else new BaselineVMExpression().Execute(ctx, Operand(7, 3, 4));
                }
            };
            return Run(useNative, outer, frame);
        }
        AssertSame("reentrant inner overflow after outer write", NestedFailure(false), NestedFailure(true));
        Exception failed = null;
        var threads = new Thread[4];
        for (int i = 0; i < threads.Length; i++)
        {
            int id = i;
            threads[i] = new Thread(() => {
                try
                {
                    for (int n = 0; n < 150; n++)
                        Compare("thread " + id + " cycle " + n, Operand((id + n) % 21), n, id + 1);
                }
                catch (Exception e) { Interlocked.CompareExchange(ref failed, e, null); }
            });
            threads[i].Start();
        }
        foreach (var thread in threads) thread.Join();
        if (failed != null) throw failed;
    }

    private static void TSOStillManaged()
    {
        foreach (var op in new[] { Operand(18, 1, 2), Operand(19, 1, 0) })
        {
            var left = Frame(Memory(op, 5, 7), false);
            var right = Frame(left.Memory.Clone(), false);
            left.Caller.MyList.AddLast(11);
            left.Caller.MyList.AddLast(12);
            right.Caller.MyList.AddLast(11);
            right.Caller.MyList.AddLast(12);
            AssertSame("TSO managed " + op.Operator, Run(false, op, left), Run(true, op, right));
            if (!left.Caller.MyList.SequenceEqual(right.Caller.MyList))
                throw new Exception("TSO list changed differently.");
        }
    }

    private static void NoPerCallAllocation()
    {
        var executor = new OpenDollhouse.Simulation.ExpressionExecutor<PlainContext>(
            PlainRead, PlainWrite, PlainLevels);
        var context = new PlainContext();
        var operand = new OpenDollhouse.Simulation.ExpressionOperand { Operator = 2 };
        bool negative;
        for (int i = 0; i < 1000; i++) executor.Execute(context, operand, out negative);
        long before = GC.GetAllocatedBytesForCurrentThread();
        for (int i = 0; i < 10000; i++) executor.Execute(context, operand, out negative);
        long allocated = GC.GetAllocatedBytesForCurrentThread() - before;
        if (allocated != 0) throw new Exception("Native Expression bridge allocated " + allocated +
            " bytes across 10,000 warmed executions.");
        cases++;
    }

    private static void DiagnosticWriterDoesNotChangeSimulation()
    {
        if (Environment.GetEnvironmentVariable("ODH_VM_DIAGNOSTICS") != "1")
            throw new Exception("Run diagnostic writer case with ODH_VM_DIAGNOSTICS=1.");
        var originalWriter = Console.Out;
        try
        {
            Console.SetOut(new ThrowingTextWriter());
            // First native execution emits the first sampled diagnostic.
            var success = Run(true, Operand(0), Frame(Memory(Operand(0), 7, 3)));
            if (success.Error != null || success.Exit != VMPrimitiveExitCode.GOTO_TRUE)
                throw new Exception("A diagnostic write changed a successful Expression: " + Describe(success));

            // Force the next sparse sample to coincide with a real arithmetic
            // failure. Its OverflowException must survive the diagnostic fault.
            var counter = typeof(VMExpression).GetField("TS1Calls", BindingFlags.NonPublic | BindingFlags.Static);
            if (counter == null) throw new Exception("Diagnostic call counter is missing.");
            counter.SetValue(null, 16383L);
            var overflow = Run(true, Operand(7), Frame(Memory(Operand(7), int.MinValue, -1)));
            if (overflow.Error?.GetType() != typeof(OverflowException))
                throw new Exception("A diagnostic write replaced OverflowException: " + Describe(overflow));
        }
        finally { Console.SetOut(originalWriter); }
        Console.WriteLine("PASS: diagnostic writer failures preserve success and OverflowException.");
    }

    private static void Main(string[] args)
    {
        if (args.Length == 1 && args[0] == "--throwing-writer")
        {
            DiagnosticWriterDoesNotChangeSimulation();
            return;
        }
        AllOperators();
        CallbackFailures();
        ReentrantAndThreads();
        TSOStillManaged();
        NoPerCallAllocation();
        Console.WriteLine("PASS: " + cases + " differential TS1 Expression cases using actual baseline source and production dispatch.");
    }
}
