// SPDX-License-Identifier: MPL-2.0
using System;
using System.Runtime.InteropServices;

namespace OpenDollhouse.Simulation
{
    internal static class NativeExpressionMethods
    {
        private const string Library = "odh_simulation";

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate int ReadVariable(IntPtr context, byte scope, short index, out int value);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate int WriteVariable(IntPtr context, byte scope, short index, int value, out uint accepted);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate int ReadLevels(IntPtr context, out int caller, out int callee, out int offset);

        [DllImport(Library, EntryPoint = "odh_expression_abi_version", ExactSpelling = true,
            CallingConvention = CallingConvention.Cdecl)]
        internal static extern uint AbiVersion();

        [DllImport(Library, EntryPoint = "odh_expression_execute_ts1_v1", ExactSpelling = true,
            CallingConvention = CallingConvention.Cdecl)]
        internal static extern int Execute(ref ExpressionOperand operand, uint operandLength, IntPtr context,
            ReadVariable read, WriteVariable write, ReadLevels levels, out uint gotoTrue);
    }
}
