// SPDX-License-Identifier: MPL-2.0
using System;
using System.Runtime.InteropServices;

namespace OpenDollhouse.Simulation
{
    internal sealed class NativeStateHandle : SafeHandle
    {
        private NativeStateHandle() : base(IntPtr.Zero, true) { }
        internal NativeStateHandle(IntPtr value) : base(IntPtr.Zero, true) { SetHandle(value); }
        public override bool IsInvalid { get { return handle == IntPtr.Zero; } }
        protected override bool ReleaseHandle() { return NativeThreadStateMethods.StateRelease(handle) == 0; }
    }

    internal sealed class NativeBankHandle : SafeHandle
    {
        private NativeBankHandle() : base(IntPtr.Zero, true) { }
        internal NativeBankHandle(IntPtr value) : base(IntPtr.Zero, true) { SetHandle(value); }
        public override bool IsInvalid { get { return handle == IntPtr.Zero; } }
        protected override bool ReleaseHandle() { return NativeThreadStateMethods.BankRelease(handle) == 0; }
    }

    internal static class NativeThreadStateMethods
    {
        private const string Library = "odh_simulation";

        [DllImport(Library, EntryPoint = "odh_state_abi_version", ExactSpelling = true,
            CallingConvention = CallingConvention.Cdecl)]
        internal static extern uint AbiVersion();

        [DllImport(Library, EntryPoint = "odh_state_create_v1", ExactSpelling = true,
            CallingConvention = CallingConvention.Cdecl)]
        internal static extern int StateCreate(uint initializeBanks, out IntPtr state);

        [DllImport(Library, EntryPoint = "odh_state_release_v1", ExactSpelling = true,
            CallingConvention = CallingConvention.Cdecl)]
        internal static extern int StateRelease(IntPtr state);

        [DllImport(Library, EntryPoint = "odh_state_get_bank_v1", ExactSpelling = true,
            CallingConvention = CallingConvention.Cdecl)]
        internal static extern int StateGetBank(NativeStateHandle state, uint kind, out IntPtr bank);

        [DllImport(Library, EntryPoint = "odh_state_bind_bank_v1", ExactSpelling = true,
            CallingConvention = CallingConvention.Cdecl)]
        internal static extern int StateBindBank(NativeStateHandle state, uint kind, NativeBankHandle bank);

        [DllImport(Library, EntryPoint = "odh_bank_create_v1", ExactSpelling = true,
            CallingConvention = CallingConvention.Cdecl)]
        internal static extern int BankCreate(uint kind, out IntPtr bank);

        [DllImport(Library, EntryPoint = "odh_bank_release_v1", ExactSpelling = true,
            CallingConvention = CallingConvention.Cdecl)]
        internal static extern int BankRelease(IntPtr bank);

        [DllImport(Library, EntryPoint = "odh_bank_clone_v1", ExactSpelling = true,
            CallingConvention = CallingConvention.Cdecl)]
        internal static extern int BankClone(NativeBankHandle bank, out IntPtr clone);

        [DllImport(Library, EntryPoint = "odh_bank_read_v1", ExactSpelling = true,
            CallingConvention = CallingConvention.Cdecl)]
        internal static extern int BankRead(NativeBankHandle bank, int index, out int value);

        [DllImport(Library, EntryPoint = "odh_bank_write_v1", ExactSpelling = true,
            CallingConvention = CallingConvention.Cdecl)]
        internal static extern int BankWrite(NativeBankHandle bank, int index, int value);

        [DllImport(Library, EntryPoint = "odh_bank_import16_v1", ExactSpelling = true,
            CallingConvention = CallingConvention.Cdecl)]
        internal static extern int BankImport16(NativeBankHandle bank, [In] short[] values, uint count);

        [DllImport(Library, EntryPoint = "odh_bank_export16_v1", ExactSpelling = true,
            CallingConvention = CallingConvention.Cdecl)]
        internal static extern int BankExport16(NativeBankHandle bank, [Out] short[] values, uint count);

        [DllImport(Library, EntryPoint = "odh_bank_import32_v1", ExactSpelling = true,
            CallingConvention = CallingConvention.Cdecl)]
        internal static extern int BankImport32(NativeBankHandle bank, [In] int[] values, uint count);

        [DllImport(Library, EntryPoint = "odh_bank_export32_v1", ExactSpelling = true,
            CallingConvention = CallingConvention.Cdecl)]
        internal static extern int BankExport32(NativeBankHandle bank, [Out] int[] values, uint count);

        [DllImport(Library, EntryPoint = "odh_bank_info_v1", ExactSpelling = true,
            CallingConvention = CallingConvention.Cdecl)]
        internal static extern int BankInfo(NativeBankHandle bank, out uint kind, out uint count,
            out uint references, out ulong allocationBytes);
    }
}
