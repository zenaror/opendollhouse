// Actual pre-adapter BCON.cs, SHA256 cf5276727b3f2ba7c91a5756a9d3d8dae1d08bee596b17636c71b662cdc6f565
// Only namespace changed to permit differential tests. See ../LICENSE.FreeSO.md.
using System.IO;
using FSO.Files.Utils;

namespace FSO.Files.Formats.IFF.Chunks.Baseline
{
    /// <summary>
    /// This chunk type holds a number of constants that behavior code can refer to. 
    /// Labels may be provided for them in a TRCN chunk with the same ID.
    /// </summary>
    public class BCON : IffChunk
    {
        public byte Flags;
        public ushort[] Constants = new ushort[0];

        /// <summary>
        /// Reads a BCON chunk from a stream.
        /// </summary>
        /// <param name="iff">An Iff instance.</param>
        /// <param name="stream">A Stream instance holding a BCON.</param>
        public override void Read(IffFile iff, Stream stream)
        {
            using (var io = IoBuffer.FromStream(stream, ByteOrder.LITTLE_ENDIAN))
            {
                var num = io.ReadByte();
                Flags = io.ReadByte();

                Constants = new ushort[num];
                for (var i = 0; i < num; i++)
                {
                    Constants[i] = io.ReadUInt16();
                }
            }
        }

        public override bool Write(IffFile iff, Stream stream)
        {
            using (var io = IoWriter.FromStream(stream, ByteOrder.LITTLE_ENDIAN))
            {
                io.WriteByte((byte)Constants.Length);
                io.WriteByte(Flags);
                foreach (var c in Constants)
                {
                    io.WriteUInt16(c);
                }

                return true;
            }
        }
    }
}
