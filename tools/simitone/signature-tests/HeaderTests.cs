extern alias RealFiles;
using NativeBHAV = RealFiles::FSO.Files.Formats.IFF.Chunks.BHAV;

static class HeaderTests
{
    public static void Run()
    {
        foreach (ushort format in new ushort[] { 0x8000, 0x8001, 0x8002 })
        {
            // No instructions; the native DWORD begins at byte 8 in all three.
            byte[] raw = { (byte)(format & 255), (byte)(format >> 8), 0, 0,
                           0, 0, 0, 0, 0xef, 0xcd, 0xab, 0x89 };
            using var input = new MemoryStream(raw);
            var bhav = new NativeBHAV();
            bhav.Read(null, input);
            if (bhav.NativeVersionWord != 0x89abcdefu || input.Position != 12
                || bhav.Instructions.Length != 0
                || bhav.Version != (format == 0x8002 ? 0xcdef : 0))
                throw new Exception($"Native header word/alignment changed for {format:x4}");
            Console.WriteLine($"PASS real parser {format:x4}: full DWORD and existing Version preserved");
        }

        var tso = new NativeBHAV();
        tso.Read(null, new MemoryStream(new byte[] {
            3, 0x80, 0, 0, 0, 0, 0, 0x23, 0x81, 0, 0, 0, 0 }));
        if (tso.NativeVersionWord != null || tso.Version != 0x8123)
            throw new Exception("TSO header was treated as native TS1");
        Console.WriteLine("PASS real parser 8003: TS1 signature unavailable");

        try
        {
            new NativeBHAV().Read(null, new MemoryStream(new byte[] {
                2, 0x80, 0, 0, 0, 0, 0, 0, 0xef, 0xcd, 0xab }));
            throw new Exception("Truncated native header word accepted");
        }
        catch (EndOfStreamException)
        {
            Console.WriteLine("PASS real parser: truncated native DWORD rejected");
        }
    }
}
