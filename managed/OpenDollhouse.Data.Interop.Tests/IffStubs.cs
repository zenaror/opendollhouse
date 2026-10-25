using System.IO;

// Only the unrelated IFF container types are stubbed. Both BCON implementations
// and BinaryReader/IoBuffer/IoWriter behavior are actual source.
namespace FSO.Files.Formats.IFF
{
    public class IffFile { }
}

namespace FSO.Files.Formats.IFF.Chunks
{
    public abstract class IffChunk
    {
        public abstract void Read(IffFile iff, Stream stream);
        public abstract bool Write(IffFile iff, Stream stream);
    }
}
