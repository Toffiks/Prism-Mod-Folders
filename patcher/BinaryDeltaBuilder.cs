using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Security.Cryptography;

internal static class BinaryDeltaBuilder
{
    private const int AnchorSize = 16;
    private const int IndexStride = 8;
    private const int MinimumMatch = 24;
    private const int MaximumCandidates = 8;

    private sealed class AnchorEntry
    {
        internal int First;
        internal List<int> More;
    }

    private static int Main(string[] args)
    {
        if (args.Length == 4 && String.Equals(args[0], "--apply", StringComparison.OrdinalIgnoreCase))
        {
            Apply(args[1], args[2], args[3]);
            Console.WriteLine("outputSha256={0}", Sha256(args[3]));
            return 0;
        }

        if (args.Length != 3)
        {
            Console.Error.WriteLine("Usage:");
            Console.Error.WriteLine("  BinaryDeltaBuilder OLD NEW OUTPUT");
            Console.Error.WriteLine("  BinaryDeltaBuilder --apply OLD PATCH OUTPUT");
            return 2;
        }

        byte[] oldBytes = File.ReadAllBytes(args[0]);
        byte[] newBytes = File.ReadAllBytes(args[1]);
        Dictionary<ulong, AnchorEntry> index = BuildIndex(oldBytes);

        byte[] commands;
        int copyCommands;
        int literalCommands;
        long copiedBytes;
        using (MemoryStream commandStream = new MemoryStream())
        using (BinaryWriter writer = new BinaryWriter(commandStream))
        {
            BuildCommands(oldBytes, newBytes, index, writer, out copyCommands, out literalCommands, out copiedBytes);
            writer.Write((byte)0);
            writer.Flush();
            commands = commandStream.ToArray();
        }

        using (FileStream output = new FileStream(args[2], FileMode.Create, FileAccess.Write, FileShare.None))
        using (BinaryWriter header = new BinaryWriter(output))
        {
            header.Write(new byte[] { (byte)'P', (byte)'M', (byte)'F', (byte)'D', (byte)'L', (byte)'T', (byte)'2', 0 });
            header.Write((long)newBytes.Length);
            header.Write(commands.Length);
            header.Write(commands);
        }

        Verify(args[0], args[1], args[2]);
        FileInfo patch = new FileInfo(args[2]);
        Console.WriteLine("old={0} new={1} patch={2}", oldBytes.Length, newBytes.Length, patch.Length);
        Console.WriteLine("copyCommands={0} literalCommands={1} copiedBytes={2}", copyCommands, literalCommands, copiedBytes);
        Console.WriteLine("oldSha256={0}", Sha256(args[0]));
        Console.WriteLine("newSha256={0}", Sha256(args[1]));
        Console.WriteLine("patchSha256={0}", Sha256(args[2]));
        return 0;
    }

    private static Dictionary<ulong, AnchorEntry> BuildIndex(byte[] oldBytes)
    {
        Dictionary<ulong, AnchorEntry> index = new Dictionary<ulong, AnchorEntry>(oldBytes.Length / IndexStride);
        for (int position = 0; position + AnchorSize <= oldBytes.Length; position += IndexStride)
        {
            ulong key = ReadUInt64(oldBytes, position);
            AnchorEntry entry;
            if (!index.TryGetValue(key, out entry))
            {
                index.Add(key, new AnchorEntry { First = position });
                continue;
            }

            if (entry.More == null)
            {
                entry.More = new List<int>(MaximumCandidates - 1);
            }
            if (entry.More.Count < MaximumCandidates - 1)
            {
                entry.More.Add(position);
            }
        }
        return index;
    }

    private static void BuildCommands(
        byte[] oldBytes,
        byte[] newBytes,
        Dictionary<ulong, AnchorEntry> index,
        BinaryWriter writer,
        out int copyCommands,
        out int literalCommands,
        out long copiedBytes)
    {
        int position = 0;
        int literalStart = 0;
        copyCommands = 0;
        literalCommands = 0;
        copiedBytes = 0;

        while (position + AnchorSize <= newBytes.Length)
        {
            AnchorEntry entry;
            int bestOldPosition = -1;
            int bestLength = 0;
            ulong key = ReadUInt64(newBytes, position);

            if (index.TryGetValue(key, out entry))
            {
                TryCandidate(oldBytes, newBytes, entry.First, position, ref bestOldPosition, ref bestLength);
                if (entry.More != null)
                {
                    for (int i = 0; i < entry.More.Count; ++i)
                    {
                        TryCandidate(oldBytes, newBytes, entry.More[i], position, ref bestOldPosition, ref bestLength);
                    }
                }
            }

            if (bestLength < MinimumMatch)
            {
                ++position;
                continue;
            }

            int backward = 0;
            while (position - backward > literalStart && bestOldPosition - backward > 0 &&
                   newBytes[position - backward - 1] == oldBytes[bestOldPosition - backward - 1])
            {
                ++backward;
            }

            WriteLiteral(writer, newBytes, literalStart, position - backward - literalStart, ref literalCommands);
            writer.Write((byte)1);
            writer.Write(bestOldPosition - backward);
            writer.Write(bestLength + backward);
            ++copyCommands;
            copiedBytes += bestLength + backward;

            position += bestLength;
            literalStart = position;
        }

        WriteLiteral(writer, newBytes, literalStart, newBytes.Length - literalStart, ref literalCommands);
    }

    private static void TryCandidate(
        byte[] oldBytes,
        byte[] newBytes,
        int oldPosition,
        int newPosition,
        ref int bestOldPosition,
        ref int bestLength)
    {
        if (!EqualRange(oldBytes, oldPosition, newBytes, newPosition, AnchorSize))
        {
            return;
        }

        int maximum = Math.Min(oldBytes.Length - oldPosition, newBytes.Length - newPosition);
        int length = AnchorSize;
        while (length < maximum && oldBytes[oldPosition + length] == newBytes[newPosition + length])
        {
            ++length;
        }
        if (length > bestLength)
        {
            bestLength = length;
            bestOldPosition = oldPosition;
        }
    }

    private static bool EqualRange(byte[] left, int leftPosition, byte[] right, int rightPosition, int count)
    {
        for (int i = 0; i < count; ++i)
        {
            if (left[leftPosition + i] != right[rightPosition + i])
            {
                return false;
            }
        }
        return true;
    }

    private static ulong ReadUInt64(byte[] bytes, int position)
    {
        return (ulong)bytes[position] |
               ((ulong)bytes[position + 1] << 8) |
               ((ulong)bytes[position + 2] << 16) |
               ((ulong)bytes[position + 3] << 24) |
               ((ulong)bytes[position + 4] << 32) |
               ((ulong)bytes[position + 5] << 40) |
               ((ulong)bytes[position + 6] << 48) |
               ((ulong)bytes[position + 7] << 56);
    }

    private static void WriteLiteral(BinaryWriter writer, byte[] bytes, int position, int count, ref int literalCommands)
    {
        if (count <= 0)
        {
            return;
        }
        writer.Write((byte)2);
        writer.Write(count);
        writer.Write(bytes, position, count);
        ++literalCommands;
    }

    private static void Verify(string oldFile, string expectedFile, string patchFile)
    {
        string reconstructed = patchFile + ".verify";
        try
        {
            Apply(oldFile, patchFile, reconstructed);
            if (!String.Equals(Sha256(reconstructed), Sha256(expectedFile), StringComparison.OrdinalIgnoreCase))
            {
                throw new InvalidDataException("Delta verification failed.");
            }
        }
        finally
        {
            if (File.Exists(reconstructed))
            {
                File.Delete(reconstructed);
            }
        }
    }

    private static void Apply(string oldFile, string patchFile, string outputFile)
    {
        using (FileStream source = new FileStream(oldFile, FileMode.Open, FileAccess.Read, FileShare.Read))
        using (FileStream patch = new FileStream(patchFile, FileMode.Open, FileAccess.Read, FileShare.Read))
        using (BinaryReader header = new BinaryReader(patch))
        using (FileStream output = new FileStream(outputFile, FileMode.Create, FileAccess.Write, FileShare.None))
        {
            byte[] magic = header.ReadBytes(8);
            bool compressedV1 =
                magic.Length == 8 && magic[0] == 'P' && magic[1] == 'M' && magic[2] == 'F' && magic[3] == 'D' &&
                magic[4] == 'L' && magic[5] == 'T' && magic[6] == '1' && magic[7] == 0;
            bool rawV2 =
                magic.Length == 8 && magic[0] == 'P' && magic[1] == 'M' && magic[2] == 'F' && magic[3] == 'D' &&
                magic[4] == 'L' && magic[5] == 'T' && magic[6] == '2' && magic[7] == 0;
            if (!compressedV1 && !rawV2)
            {
                throw new InvalidDataException("Invalid delta header.");
            }
            long expectedLength = header.ReadInt64();
            int commandLength = header.ReadInt32();

            Stream commandStream = compressedV1
                ? (Stream)new DeflateStream(patch, CompressionMode.Decompress, true)
                : patch;
            try
            {
                using (BinaryReader commands = new BinaryReader(commandStream, System.Text.Encoding.UTF8, true))
                {
                    int consumed = 0;
                    byte[] buffer = new byte[1024 * 1024];
                    while (consumed < commandLength)
                    {
                        byte command = commands.ReadByte();
                        ++consumed;
                        if (command == 0)
                        {
                            break;
                        }
                        if (command == 1)
                        {
                            int offset = commands.ReadInt32();
                            int count = commands.ReadInt32();
                            consumed += 8;
                            CopySource(source, output, offset, count, buffer);
                        }
                        else if (command == 2)
                        {
                            int count = commands.ReadInt32();
                            consumed += 4 + count;
                            CopyExact(commands.BaseStream, output, count, buffer);
                        }
                        else
                        {
                            throw new InvalidDataException("Unknown delta command.");
                        }
                    }
                }
            }
            finally
            {
                if (compressedV1)
                {
                    commandStream.Dispose();
                }
            }
            if (output.Length != expectedLength)
            {
                throw new InvalidDataException("Unexpected reconstructed file length.");
            }
        }
    }

    private static void CopySource(FileStream source, Stream output, int offset, int count, byte[] buffer)
    {
        source.Position = offset;
        CopyExact(source, output, count, buffer);
    }

    private static void CopyExact(Stream input, Stream output, int count, byte[] buffer)
    {
        while (count > 0)
        {
            int requested = Math.Min(count, buffer.Length);
            int read = input.Read(buffer, 0, requested);
            if (read <= 0)
            {
                throw new EndOfStreamException();
            }
            output.Write(buffer, 0, read);
            count -= read;
        }
    }

    private static string Sha256(string file)
    {
        using (FileStream stream = new FileStream(file, FileMode.Open, FileAccess.Read, FileShare.Read))
        using (SHA256 sha = SHA256.Create())
        {
            return BitConverter.ToString(sha.ComputeHash(stream)).Replace("-", String.Empty);
        }
    }
}
