// SPDX-License-Identifier: MPL-2.0
using System;

namespace OpenDollhouse.Simulation
{
    /// <summary>A captured view keeps its own native bank alive across a thread's
    /// later Load or independent TEMP replacement. All indexed values live in C++.</summary>
    public sealed class NativeTemp16View
    {
        internal readonly NativeBankHandle Handle;
        internal NativeTemp16View(NativeBankHandle handle) { Handle = handle; }
        public int Length { get { return 20; } }

        public short this[int index]
        {
            get
            {
                int value;
                NativeThreadState.Check(NativeThreadStateMethods.BankRead(Handle, index, out value));
                return unchecked((short)value);
            }
            set { NativeThreadState.Check(NativeThreadStateMethods.BankWrite(Handle, index, value)); }
        }

        public short[] Snapshot()
        {
            var values = new short[20];
            NativeThreadState.Check(NativeThreadStateMethods.BankExport16(Handle, values, 20));
            return values;
        }

        public short[] SnapshotFirstFour()
        {
            // RunTreeByName copies exactly arguments 0..3 synchronously.
            return new[] { this[0], this[1], this[2], this[3] };
        }

        internal NativeTemp16View CloneBank()
        {
            IntPtr clone;
            NativeThreadState.Check(NativeThreadStateMethods.BankClone(Handle, out clone));
            return new NativeTemp16View(new NativeBankHandle(clone));
        }
    }

    public sealed class NativeTemp32View
    {
        internal readonly NativeBankHandle Handle;
        internal NativeTemp32View(NativeBankHandle handle) { Handle = handle; }
        public int Length { get { return 2; } }

        public int this[int index]
        {
            get
            {
                int value;
                NativeThreadState.Check(NativeThreadStateMethods.BankRead(Handle, index, out value));
                return value;
            }
            set { NativeThreadState.Check(NativeThreadStateMethods.BankWrite(Handle, index, value)); }
        }

        public int[] Snapshot()
        {
            var values = new int[2];
            NativeThreadState.Check(NativeThreadStateMethods.BankExport32(Handle, values, 2));
            return values;
        }

        internal NativeTemp32View CloneBank()
        {
            IntPtr clone;
            NativeThreadState.Check(NativeThreadStateMethods.BankClone(Handle, out clone));
            return new NativeTemp32View(new NativeBankHandle(clone));
        }
    }

    /// <summary>Stable native holder for two independently replaceable register
    /// banks. The game must serialize mutations on its VM thread; native refcounts
    /// protect lifetime, not concurrent writes.</summary>
    public sealed class NativeThreadState
    {
        private const uint TempKind = 1;
        private const uint XLKind = 2;
        internal readonly NativeStateHandle Handle;
        private NativeTemp16View temps;
        private NativeTemp32View xl;

        static NativeThreadState()
        {
            uint abi = NativeThreadStateMethods.AbiVersion();
            if (abi != 1) throw new NotSupportedException("odh_simulation state ABI " + abi + "; expected 1.");
        }

        public NativeThreadState() : this(true) { }

        private NativeThreadState(bool initializeBanks)
        {
            IntPtr state;
            Check(NativeThreadStateMethods.StateCreate(initializeBanks ? 1u : 0u, out state));
            Handle = new NativeStateHandle(state);
        }

        public NativeTemp16View Temps
        {
            get
            {
                if (temps == null)
                {
                    IntPtr bank;
                    Check(NativeThreadStateMethods.StateGetBank(Handle, TempKind, out bank));
                    temps = new NativeTemp16View(new NativeBankHandle(bank));
                }
                return temps;
            }
        }

        public NativeTemp32View TempXL
        {
            get
            {
                if (xl == null)
                {
                    IntPtr bank;
                    Check(NativeThreadStateMethods.StateGetBank(Handle, XLKind, out bank));
                    xl = new NativeTemp32View(new NativeBankHandle(bank));
                }
                return xl;
            }
        }

        private void BindTemps(NativeTemp16View replacement)
        {
            Check(NativeThreadStateMethods.StateBindBank(Handle, TempKind, replacement.Handle));
            temps = replacement;
        }

        private void BindXL(NativeTemp32View replacement)
        {
            Check(NativeThreadStateMethods.StateBindBank(Handle, XLKind, replacement.Handle));
            xl = replacement;
        }

        public void ImportTemps(short[] values)
        {
            if (values == null) throw new ArgumentNullException("values");
            if (values.Length != 20) throw new ArgumentException("TEMP requires exactly 20 values.", "values");
            IntPtr bank;
            Check(NativeThreadStateMethods.BankCreate(TempKind, out bank));
            var handle = new NativeBankHandle(bank);
            try
            {
                Check(NativeThreadStateMethods.BankImport16(handle, values, 20));
                BindTemps(new NativeTemp16View(handle));
            }
            catch { handle.Dispose(); throw; }
        }

        public void ImportXL(int[] values)
        {
            if (values == null) throw new ArgumentNullException("values");
            if (values.Length != 2) throw new ArgumentException("TempXL requires exactly 2 values.", "values");
            IntPtr bank;
            Check(NativeThreadStateMethods.BankCreate(XLKind, out bank));
            var handle = new NativeBankHandle(bank);
            try
            {
                Check(NativeThreadStateMethods.BankImport32(handle, values, 2));
                BindXL(new NativeTemp32View(handle));
            }
            catch { handle.Dispose(); throw; }
        }

        public NativeThreadState Share()
        {
            var result = new NativeThreadState(false);
            result.BindTemps(Temps);
            result.BindXL(TempXL);
            return result;
        }

        public NativeThreadState Clone()
        {
            var result = new NativeThreadState(false);
            result.BindTemps(Temps.CloneBank());
            result.BindXL(TempXL.CloneBank());
            return result;
        }

        internal static void Check(int status)
        {
            switch (status)
            {
                case 0: return;
                case 1: throw new ArgumentException("Invalid native register bank argument.");
                case 2: throw new IndexOutOfRangeException();
                case 3: throw new InvalidOperationException("Native register bank kind mismatch.");
                case 4: throw new InvalidOperationException("Native register bank is missing.");
                case 5: throw new OutOfMemoryException();
                case 6: throw new OverflowException("Native register bank reference count overflow.");
                default: throw new InvalidOperationException("Native register bank internal error: " + status);
            }
        }
    }
}
