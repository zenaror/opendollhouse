// SPDX-License-Identifier: MPL-2.0
using System;
using System.Runtime.ExceptionServices;
using System.Runtime.InteropServices;

namespace OpenDollhouse.Simulation
{
    /// <summary>The exact eight-byte TS1 Expression operand. The native engine
    /// consumes its fields without owning this memory.</summary>
    [StructLayout(LayoutKind.Sequential, Pack = 1)]
    public struct ExpressionOperand
    {
        public short LhsData;
        public short RhsData;
        public byte IsSigned;
        public byte Operator;
        public byte LhsOwner;
        public byte RhsOwner;
    }

    public delegate int ExpressionRead<TContext>(TContext context, byte scope, short index);
    public delegate bool ExpressionWrite<TContext>(TContext context, byte scope, short index, int value);
    public delegate void ExpressionLevels<TContext>(TContext context, out int caller, out int callee, out int offset);

    public sealed class ExpressionUnknownOperatorException : InvalidOperationException
    {
        internal ExpressionUnknownOperatorException() : base("Unknown TS1 Expression operator.") { }
    }

    /// <summary>Runs all TS1 Expression operators in odh_simulation. Only host
    /// memory and level access remain managed. Callback exceptions are captured
    /// and rethrown after native code returns, preserving earlier writes.</summary>
    public sealed class ExpressionExecutor<TContext> where TContext : class
    {
        private sealed class CallFrame
        {
            internal ExpressionExecutor<TContext> Executor;
            internal TContext Context;
            internal ExceptionDispatchInfo Failure;
            internal bool NegativeSqrt;
            internal byte Operator;
            internal CallFrame Next;
        }

        [ThreadStatic] private static CallFrame current;
        [ThreadStatic] private static CallFrame free;

        private static readonly NativeExpressionMethods.ReadVariable NativeRead = ReadCallback;
        private static readonly NativeExpressionMethods.WriteVariable NativeWrite = WriteCallback;
        private static readonly NativeExpressionMethods.ReadLevels NativeLevels = LevelsCallback;

        private readonly ExpressionRead<TContext> read;
        private readonly ExpressionWrite<TContext> write;
        private readonly ExpressionLevels<TContext> levels;

        public ExpressionExecutor(ExpressionRead<TContext> read, ExpressionWrite<TContext> write,
            ExpressionLevels<TContext> levels)
        {
            this.read = read ?? throw new ArgumentNullException("read");
            this.write = write ?? throw new ArgumentNullException("write");
            this.levels = levels ?? throw new ArgumentNullException("levels");
            // The borrowed packed struct is passed as bytes to the C ABI.
            // A future big-endian managed host must encode LE explicitly.
            if (!BitConverter.IsLittleEndian)
                throw new NotSupportedException("The managed Expression bridge requires a little-endian host.");
            uint abi = NativeExpressionMethods.AbiVersion();
            if (abi != 1) throw new NotSupportedException("odh_simulation Expression ABI " + abi + "; expected 1.");
            if (Marshal.SizeOf(typeof(ExpressionOperand)) != 8)
                throw new NotSupportedException("Expression operand layout is not eight bytes.");
        }

        public bool Execute(TContext context, ExpressionOperand operand, out bool negativeSqrt)
        {
            if (context == null) throw new ArgumentNullException("context");
            var frame = free;
            if (frame == null) frame = new CallFrame();
            else free = frame.Next;
            frame.Next = current;
            frame.Executor = this;
            frame.Context = context;
            frame.Failure = null;
            frame.NegativeSqrt = false;
            frame.Operator = operand.Operator;
            current = frame;

            try
            {
                uint result;
                int status = NativeExpressionMethods.Execute(ref operand, 8, IntPtr.Zero,
                    NativeRead, NativeWrite, NativeLevels, out result);
                negativeSqrt = frame.NegativeSqrt;
                if (frame.Failure != null) frame.Failure.Throw();
                switch (status)
                {
                    case 0:
                        if (result > 1) throw new InvalidOperationException("Native Expression returned a nonboolean result.");
                        return result != 0;
                    case 2: throw new InvalidOperationException("Native Expression callback failed without a captured exception.");
                    case 3: throw new ExpressionUnknownOperatorException();
                    case 4: throw new OverflowException();
                    case 1: throw new ArgumentException("Invalid native Expression argument.");
                    default: throw new InvalidOperationException("Native Expression internal error: " + status);
                }
            }
            finally
            {
                current = frame.Next;
                frame.Context = null;
                frame.Executor = null;
                frame.Failure = null;
                frame.Next = free;
                free = frame;
            }
        }

        private static int ReadCallback(IntPtr ignored, byte scope, short index, out int value)
        {
            value = 0;
            var frame = current;
            if (frame == null) return 1;
            try
            {
                value = frame.Executor.read(frame.Context, scope, index);
                if (frame.Operator == 20 && value < 0) frame.NegativeSqrt = true;
                return 0;
            }
            catch (Exception exception)
            {
                frame.Failure = ExceptionDispatchInfo.Capture(exception);
                return 1;
            }
        }

        private static int WriteCallback(IntPtr ignored, byte scope, short index, int value, out uint accepted)
        {
            accepted = 0;
            var frame = current;
            if (frame == null) return 1;
            try
            {
                accepted = frame.Executor.write(frame.Context, scope, index, value) ? 1u : 0u;
                return 0;
            }
            catch (Exception exception)
            {
                frame.Failure = ExceptionDispatchInfo.Capture(exception);
                return 1;
            }
        }

        private static int LevelsCallback(IntPtr ignored, out int caller, out int callee, out int offset)
        {
            caller = callee = offset = 0;
            var frame = current;
            if (frame == null) return 1;
            try
            {
                frame.Executor.levels(frame.Context, out caller, out callee, out offset);
                return 0;
            }
            catch (Exception exception)
            {
                frame.Failure = ExceptionDispatchInfo.Capture(exception);
                return 1;
            }
        }
    }
}
