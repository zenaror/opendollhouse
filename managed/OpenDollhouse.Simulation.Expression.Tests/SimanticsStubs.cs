// SPDX-License-Identifier: MPL-2.0
// Narrow synthetic VM boundary for compiling the actual VMExpression sources.
using System;
using System.Collections.Generic;

namespace FSO.SimAntics.Model { internal sealed class ModelNamespaceMarker { } }
namespace FSO.SimAntics.Engine.Utils { internal sealed class UtilsNamespaceMarker { } }

namespace FSO.Files.Utils
{
    public enum ByteOrder { LITTLE_ENDIAN }
    public sealed class IoBuffer : IDisposable
    {
        private readonly byte[] bytes;
        private int offset;
        private IoBuffer(byte[] bytes) { this.bytes = bytes; }
        public static IoBuffer FromBytes(byte[] bytes, ByteOrder order) { return new IoBuffer(bytes); }
        public short ReadInt16()
        {
            var result = (short)(bytes[offset] | (bytes[offset + 1] << 8));
            offset += 2; return result;
        }
        public byte ReadByte() { return bytes[offset++]; }
        public void Dispose() { }
    }
}

namespace FSO.SimAntics.Engine.Scopes
{
    public enum VMVariableScope : byte { MyObject = 3, Literal = 7, Temps = 8, TempXL = 42 }
}

namespace FSO.SimAntics.Engine
{
    using FSO.SimAntics.Engine.Scopes;
    public enum VMPrimitiveExitCode : byte { GOTO_TRUE = 0, GOTO_FALSE = 1 }
    public static class VMPrimitiveExitCodeExtensions
    {
        public static VMPrimitiveExitCode AsGotoExitCode(this bool value)
        { return value ? VMPrimitiveExitCode.GOTO_TRUE : VMPrimitiveExitCode.GOTO_FALSE; }
    }
    public abstract class VMPrimitiveOperand { }
    public abstract class VMPrimitiveHandler
    {
        public abstract VMPrimitiveExitCode Execute(VMStackFrame context, VMPrimitiveOperand args);
    }
    public sealed class VMSimanticsException : Exception
    {
        public VMSimanticsException(string message, VMStackFrame context) : base(message) { }
    }
    public enum VMStackObjectVariable { Room = 29 }
    public sealed class SyntheticVM { public bool TS1 = true; }
    public sealed class SyntheticPosition { public int Level; }
    public sealed class SyntheticOBJ { public int LevelOffset; }
    public sealed class SyntheticObject { public SyntheticOBJ OBJ = new SyntheticOBJ(); }
    public sealed class SyntheticEntity
    {
        public SyntheticPosition Position = new SyntheticPosition();
        public SyntheticObject Object = new SyntheticObject();
        public LinkedList<short> MyList = new LinkedList<short>();
    }
    public sealed class VMStackFrame
    {
        public SyntheticVM VM = new SyntheticVM();
        public SyntheticEntity Caller = new SyntheticEntity();
        public SyntheticEntity Callee = new SyntheticEntity();
        public SyntheticEntity StackObject = new SyntheticEntity();
        public SyntheticMemory Memory = new SyntheticMemory();
    }
    public sealed class SyntheticMemory
    {
        public readonly Dictionary<string, int> Values = new Dictionary<string, int>();
        public readonly List<string> Trace = new List<string>();
        public bool AcceptWrites = true;
        public Action<VMStackFrame, byte, short> OnRead;
        public Action<VMStackFrame, byte, short, int> OnWrite;
        public int Get(VMStackFrame context, byte scope, short index)
        {
            Trace.Add("R:" + scope + ":" + index);
            if (OnRead != null) OnRead(context, scope, index);
            int value;
            return Values.TryGetValue(scope + ":" + index, out value) ? value : 0;
        }
        public bool Set(VMStackFrame context, byte scope, short index, int value)
        {
            Trace.Add("W:" + scope + ":" + index + ":" + value);
            if (OnWrite != null) OnWrite(context, scope, index, value);
            if (AcceptWrites) Values[scope + ":" + index] = value;
            return AcceptWrites;
        }
        public SyntheticMemory Clone()
        {
            var result = new SyntheticMemory { AcceptWrites = AcceptWrites };
            foreach (var item in Values) result.Values.Add(item.Key, item.Value);
            return result;
        }
    }
    public static class VMMemory
    {
        public static int GetBigVariable(VMStackFrame context, VMVariableScope scope, short index)
        { return context.Memory.Get(context, (byte)scope, index); }
        public static bool SetBigVariable(VMStackFrame context, VMVariableScope scope, short index, int value)
        { return context.Memory.Set(context, (byte)scope, index, value); }
        public static LinkedList<short> GetList(VMStackFrame context, VMVariableScope scope)
        { return context.Caller.MyList; }
    }
}
