using System;
using System.IO;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

[assembly: InternalsVisibleTo("OpenDollhouse.Data.Interop.Tests")]

namespace OpenDollhouse.Data.Interop
{
    public sealed class BconData
    {
        public byte Flags { get; private set; }
        public ushort[] Constants { get; private set; }
        public uint Remaining { get; private set; }

        internal BconData(byte flags, ushort[] constants, uint remaining)
        {
            Flags = flags;
            Constants = constants;
            Remaining = remaining;
        }
    }

    /// <summary>Calls the native ODH BCON reader. No managed parser fallback.</summary>
    public static class BconDecoder
    {
        public const uint AbiVersion = 1;
        public const int MaximumConstants = 255;

        public static unsafe BconData Decode(byte[] data)
        {
            if (data == null) throw new ArgumentNullException("data");
            uint actualAbi = NativeMethods.AbiVersion();
            if (actualAbi != AbiVersion)
                throw new NotSupportedException("odh_data ABI " + actualAbi + "; expected " + AbiVersion + ".");

            // The ABI requires a non-null input address even for a zero length.
            byte[] input = data.Length == 0 ? new byte[1] : data;
            var words = new ushort[MaximumConstants];
            uint count = 0, flags = 0, remaining = 0;
            int status;
            fixed (byte* inputPointer = input)
            fixed (ushort* wordsPointer = words)
            {
                status = NativeMethods.Decode(inputPointer, (uint)data.Length, wordsPointer,
                    MaximumConstants, &count, &flags, &remaining);
            }
            ThrowForStatus(status);
            if (count > MaximumConstants || flags > byte.MaxValue ||
                data.Length < 2 + 2 * count || remaining != (uint)data.Length - 2 - 2 * count)
                throw new InvalidDataException("odh_data returned inconsistent BCON metadata.");

            var constants = new ushort[(int)count];
            Array.Copy(words, constants, (int)count);
            return new BconData((byte)flags, constants, remaining);
        }

        /// <summary>
        /// Reads only one BCON record at the current position. The stream remains
        /// open, and trailing bytes remain unread, including on nonseekable streams.
        /// </summary>
        public static BconData Read(Stream stream)
        {
            if (stream == null) throw new ArgumentNullException("stream");
            int count = ReadRequiredByte(stream);
            int flags = ReadRequiredByte(stream);
            var data = new byte[2 + 2 * count];
            data[0] = (byte)count;
            data[1] = (byte)flags;
            int offset = 2;
            while (offset < data.Length)
            {
                int read = stream.Read(data, offset, data.Length - offset);
                if (read == 0) throw new EndOfStreamException();
                offset += read;
            }
            return Decode(data);
        }

        private static int ReadRequiredByte(Stream stream)
        {
            int value = stream.ReadByte();
            if (value < 0) throw new EndOfStreamException();
            return value;
        }

        internal static void ThrowForStatus(int status)
        {
            switch (status)
            {
                case 0: return;
                case 1: throw new ArgumentException("odh_data rejected BCON arguments.");
                case 2: throw new InvalidDataException("Invalid or truncated BCON data.");
                case 3: throw new InvalidDataException("BCON exceeds the 255-word format limit.");
                case 4: throw new InvalidOperationException("odh_data encountered an internal BCON decoding error.");
                default: throw new InvalidOperationException("Unknown odh_data status: " + status + ".");
            }
        }
    }

    internal static unsafe class NativeMethods
    {
        private const string Library = "odh_data";

        [DllImport(Library, EntryPoint = "odh_data_abi_version", ExactSpelling = true,
            CallingConvention = CallingConvention.Cdecl)]
        internal static extern uint AbiVersion();

        [DllImport(Library, EntryPoint = "odh_bcon_decode_v1", ExactSpelling = true,
            CallingConvention = CallingConvention.Cdecl)]
        internal static extern int Decode(byte* data, uint length, ushort* words, uint capacity,
            uint* count, uint* flags, uint* remaining);
    }
}
