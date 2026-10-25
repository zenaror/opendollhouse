using System;
using System.IO;
using OpenDollhouse.Data.Interop;
using AdaptedBcon = FSO.Files.Formats.IFF.Chunks.BCON;
using OriginalBcon = FSO.Files.Formats.IFF.Chunks.Baseline.BCON;

internal static class Program
{
    private static int groups;
    private static int comparisons;

    private static void Check(bool condition, string message)
    {
        if (!condition) throw new Exception(message);
    }

    private static void Group(string name, Action action)
    {
        action();
        groups++;
        Console.WriteLine("PASS " + name);
    }

    private static void Throws<T>(Action action) where T : Exception
    {
        try { action(); }
        catch (T) { return; }
        throw new Exception("Expected " + typeof(T).Name);
    }

    private static byte[] Fixture(int count, byte flags, int firstWord, int trailer = 0)
    {
        var bytes = new byte[2 + 2 * count + trailer];
        bytes[0] = (byte)count;
        bytes[1] = flags;
        for (int i = 0; i < count; i++)
        {
            ushort value = (ushort)(firstWord + i);
            bytes[2 + 2 * i] = (byte)value;
            bytes[3 + 2 * i] = (byte)(value >> 8);
        }
        for (int i = 2 + 2 * count; i < bytes.Length; i++) bytes[i] = 0xa5;
        return bytes;
    }

    private static void Same(ushort[] a, ushort[] b)
    {
        Check(a.Length == b.Length, "Word count differs");
        for (int i = 0; i < a.Length; i++) Check(a[i] == b[i], "Word differs at " + i);
    }

    private static void Compare(byte[] data, int offset = 0, bool partial = false)
    {
        using (var oldStream = new ObservedStream(data, offset, partial))
        using (var newStream = new ObservedStream(data, offset, partial))
        {
            var old = new OriginalBcon();
            var adapted = new AdaptedBcon();
            old.Read(null, oldStream);
            adapted.Read(null, newStream);
            Check(old.Flags == adapted.Flags, "Flags differ");
            Same(old.Constants, adapted.Constants);
            Check(oldStream.BytesRead == newStream.BytesRead, "Consumed bytes differ");
            Check(newStream.BytesRead == 2 + 2 * old.Constants.Length, "Wrong record consumption");
            Check(!oldStream.Closed && !newStream.Closed, "Reader closed caller stream");
            Check(oldStream.ReadByte() == newStream.ReadByte(), "Trailing stream bytes differ");
            // The production writer remains the old IoWriter implementation.
            using (var oldOutput = new MemoryStream())
            using (var newOutput = new MemoryStream())
            {
                old.Write(null, oldOutput);
                adapted.Write(null, newOutput);
                Check(Convert.ToBase64String(oldOutput.ToArray()) == Convert.ToBase64String(newOutput.ToArray()), "Writer changed");
            }
            comparisons++;
        }
    }

    private static void Truncated(byte[] data, bool partial)
    {
        using (var oldStream = new ObservedStream(data, 0, partial))
        using (var newStream = new ObservedStream(data, 0, partial))
        {
            Throws<EndOfStreamException>(() => new OriginalBcon().Read(null, oldStream));
            Throws<EndOfStreamException>(() => new AdaptedBcon().Read(null, newStream));
            Check(oldStream.BytesRead == newStream.BytesRead, "Truncation consumed different bytes");
            Check(!oldStream.Closed && !newStream.Closed, "Truncation closed stream");
            comparisons++;
        }
    }

    private static unsafe void NativeErrors()
    {
        byte[] bytes = Fixture(2, 0xe1, 0x1234, 3);
        ushort[] words = { 0x7777, 0x8888 };
        fixed (byte* data = bytes)
        fixed (ushort* output = words)
        {
            uint count = 99, flags = 99, remaining = 99;
            Check(NativeMethods.Decode(data, (uint)bytes.Length, output, 1, &count, &flags, &remaining) == 3, "Expected NeedCapacity");
            Check(count == 2 && flags == 0xe1 && remaining == 3, "NeedCapacity metadata");
            Check(words[0] == 0x7777 && words[1] == 0x8888, "NeedCapacity touched output");
            Check(NativeMethods.Decode(data, (uint)bytes.Length, null, 0, &count, &flags, &remaining) == 3, "Null zero-capacity query");
            Check(count == 2 && flags == 0xe1 && remaining == 3, "Query metadata");
            Check(NativeMethods.Decode(null, 0, output, 2, &count, &flags, &remaining) == 1, "Null input accepted");
            Check(count == 0 && flags == 0 && remaining == 0, "BadArg metadata not reset");
            count = flags = remaining = 99;
            Check(NativeMethods.Decode(data, (uint)bytes.Length, null, 1, &count, &flags, &remaining) == 1, "Null words with capacity accepted");
            Check(count == 0 && flags == 0 && remaining == 0, "BadArg metadata not reset");
            count = flags = remaining = 99;
            Check(NativeMethods.Decode(data, 3, output, 2, &count, &flags, &remaining) == 2, "Truncated native data accepted");
            Check(count == 0 && flags == 0 && remaining == 0, "BadFormat metadata not reset");
            Check(words[0] == 0x7777 && words[1] == 0x8888, "Failure touched output");
            count = flags = remaining = 99;
            Check(NativeMethods.Decode(data, (uint)bytes.Length, output, 2, null, &flags, &remaining) == 1, "Missing count output accepted");
            Check(flags == 0 && remaining == 0, "Available metadata not reset");
            count = flags = remaining = 99;
            Check(NativeMethods.Decode(data, (uint)bytes.Length, output, 2, &count, null, &remaining) == 1, "Missing flags output accepted");
            Check(count == 0 && remaining == 0, "Available metadata not reset");
            count = flags = remaining = 99;
            Check(NativeMethods.Decode(data, (uint)bytes.Length, output, 2, &count, &flags, null) == 1, "Missing remaining output accepted");
            Check(count == 0 && flags == 0, "Available metadata not reset");
            Check(NativeMethods.Decode(data, (uint)bytes.Length, output, 2, &count, &flags, &remaining) == 0, "Native decode failed");
            Check(words[0] == 0x1234 && words[1] == 0x1235, "Typed word endian conversion");
        }
        byte[] empty = { 0, 0xff };
        fixed (byte* data = empty)
        {
            uint count, flags, remaining;
            Check(NativeMethods.Decode(data, 2, null, 0, &count, &flags, &remaining) == 0, "Valid empty native decode");
            Check(count == 0 && flags == 0xff && remaining == 0, "Empty metadata");
        }
    }

    private static void Main(string[] args)
    {
        if (args.Length == 1 && args[0] == "--expect-native-unavailable")
        {
            Throws<DllNotFoundException>(() => BconDecoder.Decode(Fixture(0, 0, 0)));
            Console.WriteLine("PASS missing native library fails without fallback");
            return;
        }
        Group("ABI version", () => Check(NativeMethods.AbiVersion() == 1, "Wrong ABI"));
        Group("empty and maximum count, all 256 flag values", () =>
        {
            for (int flags = 0; flags < 256; flags++)
            {
                Compare(Fixture(0, (byte)flags, 0, 1));
                Compare(Fixture(255, (byte)flags, 0x8000, 1));
            }
        });
        Group("every ushort value against original reader", () =>
        {
            for (int first = 0; first < 65536; first += 255)
                Compare(Fixture(Math.Min(255, 65536 - first), 0xa5, first));
        });
        Group("every count 0..255, nonzero offset, trailing bytes", () =>
        {
            for (int count = 0; count <= 255; count++)
            {
                byte[] record = Fixture(count, 0x81, 0x12f0, 7);
                byte[] wrapped = new byte[record.Length + 5];
                Array.Copy(record, 0, wrapped, 5, record.Length);
                Compare(wrapped, 5);
            }
        });
        Group("partial-read nonseekable streams", () =>
        {
            Compare(Fixture(0, 0x80, 0, 2), 0, true);
            Compare(Fixture(255, 0xff, 0xff00, 4), 0, true);
        });
        Group("every truncation of maximum record, seekable and nonseekable", () =>
        {
            byte[] full = Fixture(255, 0xf1, 0x1234);
            for (int length = 0; length < full.Length; length++)
            {
                var cut = new byte[length];
                Array.Copy(full, cut, length);
                Truncated(cut, false);
                Truncated(cut, true);
                Throws<InvalidDataException>(() => BconDecoder.Decode(cut));
            }
        });
        Group("array decode preserves trailing-byte metadata and LE words", () =>
        {
            var value = BconDecoder.Decode(new byte[] { 3, 0x81, 0x34, 0x12, 0xcd, 0xab, 0xff, 0xff, 0xfe, 0xed });
            Check(value.Flags == 0x81 && value.Remaining == 2, "Array metadata");
            Same(value.Constants, new ushort[] { 0x1234, 0xabcd, 0xffff });
        });
        Group("independent result arrays", () =>
        {
            var first = BconDecoder.Decode(Fixture(1, 1, 7));
            first.Constants[0] = 999;
            Check(BconDecoder.Decode(Fixture(1, 1, 7)).Constants[0] == 7, "Shared result storage");
        });
        Group("failed adapter read leaves prior fields intact", () =>
        {
            ushort[] prior = { 7 };
            var adapted = new AdaptedBcon { Flags = 0x5a, Constants = prior };
            using (var stream = new MemoryStream(new byte[] { 2, 0x81, 0x34 }))
            {
                Throws<EndOfStreamException>(() => adapted.Read(null, stream));
                Check(adapted.Flags == 0x5a && object.ReferenceEquals(prior, adapted.Constants), "Failed read mutated fields");
            }
        });
        Group("managed arguments and complete status mapping", () =>
        {
            Throws<ArgumentNullException>(() => BconDecoder.Decode(null));
            Throws<ArgumentNullException>(() => BconDecoder.Read(null));
            BconDecoder.ThrowForStatus(0);
            Throws<ArgumentException>(() => BconDecoder.ThrowForStatus(1));
            Throws<InvalidDataException>(() => BconDecoder.ThrowForStatus(2));
            Throws<InvalidDataException>(() => BconDecoder.ThrowForStatus(3));
            Throws<InvalidOperationException>(() => BconDecoder.ThrowForStatus(4));
            Throws<InvalidOperationException>(() => BconDecoder.ThrowForStatus(99));
        });
        Group("actual native errors, metadata and untouched output", NativeErrors);
        Group("optional diagnostic failure cannot fail native decoding", () =>
        {
            string previousEnvironment = Environment.GetEnvironmentVariable("ODH_VM_DIAGNOSTICS");
            TextWriter previousOutput = Console.Out;
            try
            {
                Environment.SetEnvironmentVariable("ODH_VM_DIAGNOSTICS", "1");
                typeof(AdaptedBcon).GetField("NativeDecodeDiagnosticLogged",
                    System.Reflection.BindingFlags.Static | System.Reflection.BindingFlags.NonPublic).SetValue(null, 0);
                Console.SetOut(new ThrowingWriter());
                var adapted = new AdaptedBcon();
                using (var stream = new MemoryStream(new byte[] { 1, 0x81, 0x34, 0x12, 0xee }))
                {
                    adapted.Read(null, stream);
                    Check(adapted.Flags == 0x81 && adapted.Constants[0] == 0x1234, "Diagnostic failure lost decoded fields");
                    Check(stream.Position == 4 && stream.ReadByte() == 0xee, "Diagnostic failure changed consumption");
                    Check(stream.CanRead, "Diagnostic failure closed stream");
                }
            }
            finally
            {
                Console.SetOut(previousOutput);
                Environment.SetEnvironmentVariable("ODH_VM_DIAGNOSTICS", previousEnvironment);
            }
        });
        Console.WriteLine("PASS " + groups + " groups; " + comparisons + " actual-source differential cases; synthetic fixtures only.");
    }

    private sealed class ThrowingWriter : TextWriter
    {
        public override System.Text.Encoding Encoding { get { return System.Text.Encoding.UTF8; } }
        public override void WriteLine(string value) { throw new IOException("Synthetic diagnostic output failure"); }
    }

    private sealed class ObservedStream : Stream
    {
        private readonly MemoryStream inner;
        private readonly bool partial;
        public int BytesRead;
        public bool Closed;

        public ObservedStream(byte[] bytes, int offset, bool partial)
        {
            inner = new MemoryStream(bytes);
            inner.Position = offset;
            this.partial = partial;
        }
        public override bool CanRead { get { return !Closed; } }
        public override bool CanSeek { get { return !partial; } }
        public override bool CanWrite { get { return false; } }
        public override long Length { get { if (partial) throw new NotSupportedException(); return inner.Length; } }
        public override long Position
        {
            get { if (partial) throw new NotSupportedException(); return inner.Position; }
            set { if (partial) throw new NotSupportedException(); inner.Position = value; }
        }
        public override int Read(byte[] buffer, int offset, int count)
        {
            int result = inner.Read(buffer, offset, partial ? Math.Min(1, count) : count);
            BytesRead += result;
            return result;
        }
        public override long Seek(long offset, SeekOrigin origin) { if (partial) throw new NotSupportedException(); return inner.Seek(offset, origin); }
        public override void Flush() { }
        public override void SetLength(long value) { throw new NotSupportedException(); }
        public override void Write(byte[] buffer, int offset, int count) { throw new NotSupportedException(); }
        protected override void Dispose(bool disposing) { Closed = true; if (disposing) inner.Dispose(); base.Dispose(disposing); }
    }
}
