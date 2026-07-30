package jp.xenia.emulator;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.RandomAccessFile;
import java.nio.charset.StandardCharsets;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/**
 * Pure-Java STFS content-package extractor: installs DLC / Title Update
 * packages (LIVE / PIRS / CON files) into the emulator's content root in the
 * exact folder layout the native {@code xam::ContentManager} reads
 * ({@code files/content/<TITLEID>/<CONTENTTYPE>/<package_name>/...}).
 *
 * <p>The block math is a faithful Java port of the tree's own
 * {@code src/xenia/vfs/devices/stfs_container_device.cc}
 * (BlockToOffsetSTFS / BlockToHashBlockNumberSTFS / directory + file block
 * chains via level-0 hash entries). Read-only packages (LIVE/PIRS - the DLC
 * and Title Update case) are fully supported; writable CON packages are
 * supported while they fit a single level-0 hash window (the common
 * save-size), larger ones are rejected with a clear message rather than
 * risking the secondary-table walk this port does not carry.
 *
 * <p>Runs entirely in the launcher process - no native library load, same as
 * {@link GamePatchManager} / {@link TrainerManager}.
 */
public final class ContentInstaller {
    private static final int BLOCK_SIZE = 0x1000;
    private static final int BLOCKS_PER_HASH_LEVEL_0 = 170;
    private static final int BLOCKS_PER_HASH_LEVEL_1 = 28900;
    private static final int END_OF_CHAIN = 0xFFFFFF;

    /** Parsed package identity, for UI display and the install destination. */
    public static final class PackageInfo {
        public String magic;        // CON / LIVE / PIRS
        public int titleId;
        public int contentType;
        public String displayName;
        public boolean readOnly;
        public int headerSize;
        public int fileTableBlockCount;
        public int fileTableBlockNumber;
        public int totalBlockCount;
        public boolean rootActiveIndex;

        public String titleIdHex() {
            return String.format(Locale.US, "%08X", titleId);
        }

        public String contentTypeHex() {
            return String.format(Locale.US, "%08X", contentType);
        }

        /** Human name for the common content types. */
        public String contentTypeName() {
            switch (contentType) {
                case 0x00000002: return "DLC / Marketplace";
                case 0x00000001: return "Saved game";
                case 0x000B0000: return "Title update";
                case 0x00007000: return "Installer / GOD";
                case 0x00080000: return "Profile";
                default: return "Content " + contentTypeHex();
            }
        }
    }

    private final RandomAccessFile file;
    private final PackageInfo info = new PackageInfo();
    private final int blocksPerHashTable;
    private final int blockStep0;

    public ContentInstaller(final File packageFile) throws IOException {
        file = new RandomAccessFile(packageFile, "r");
        final byte[] head = new byte[0x1000];
        file.seek(0);
        file.readFully(head, 0, Math.min(head.length, (int) Math.min(file.length(), head.length)));

        final int magic = be32(head, 0x000);
        if (magic == 0x434F4E20) {
            info.magic = "CON";
        } else if (magic == 0x4C495645) {
            info.magic = "LIVE";
        } else if (magic == 0x50495253) {
            info.magic = "PIRS";
        } else {
            throw new IOException("Not an Xbox 360 content package (bad magic)");
        }
        // Offsets from src/xenia/vfs/devices/stfs_xbox.h (packed structs):
        // XContentHeader.header_size @ 0x340; XContentMetadata @ 0x344.
        info.headerSize = be32(head, 0x340);
        info.contentType = be32(head, 0x344);
        final int volumeType = be32(head, 0x3A9);
        if (volumeType != 0) {
            throw new IOException(
                    "SVOD packages (full game discs) can't be installed as content");
        }
        // execution_info @ 0x354, title_id at +0xC.
        info.titleId = be32(head, 0x360);
        // StfsVolumeDescriptor @ 0x379 (layout from stfs_xbox.h):
        // descriptor_length @ +0, version @ +1, flags @ +2,
        // file_table_block_count (u16 LE) @ +3,
        // file_table_block_number (u24 LE) @ +5,
        // top_hash_table_hash[0x14] @ +8,
        // total_block_count (u32 BE) @ +0x1C, free_block_count (u32 BE).
        final int flags = head[0x37B] & 0xFF;
        info.readOnly = (flags & 0x01) != 0;
        info.rootActiveIndex = (flags & 0x02) != 0;
        info.fileTableBlockCount = le16(head, 0x37C);
        info.fileTableBlockNumber = le24(head, 0x37E);
        info.totalBlockCount = be32(head, 0x379 + 0x1C);
        // display_name_raw (language 0, UTF-16BE, 128 chars) @ 0x411.
        info.displayName = utf16be(head, 0x411, 128);

        blocksPerHashTable = info.readOnly ? 1 : 2;
        blockStep0 = BLOCKS_PER_HASH_LEVEL_0 + blocksPerHashTable;

        if (!info.readOnly && info.totalBlockCount > BLOCKS_PER_HASH_LEVEL_0) {
            throw new IOException(
                    "Large writable (CON) package - install it on a PC build or "
                            + "extract manually; only read-only LIVE/PIRS and small CON "
                            + "packages are supported here");
        }
    }

    public PackageInfo info() {
        return info;
    }

    public void close() {
        try {
            file.close();
        } catch (IOException ignored) {
        }
    }

    /** One extracted directory entry. */
    private static final class Entry {
        String name;
        boolean directory;
        int directoryIndex;  // parent entry index, 0xFFFF = root
        int startBlock;
        int length;
        int index;
    }

    /**
     * Extracts the whole package under {@code destRoot} (the package folder,
     * e.g. {@code content/<TITLEID>/<TYPE>/<name>}). Returns the number of
     * files written.
     */
    public int extractTo(final File destRoot) throws IOException {
        final List<Entry> entries = readDirectory();
        // Resolve full paths through directory_index parent links.
        final String[] paths = new String[entries.size()];
        int filesWritten = 0;
        for (final Entry entry : entries) {
            final StringBuilder path = new StringBuilder(entry.name);
            int parent = entry.directoryIndex;
            int hops = 0;
            while (parent != 0xFFFF && hops++ < 64) {
                final Entry parentEntry = findByIndex(entries, parent);
                if (parentEntry == null) {
                    break;
                }
                path.insert(0, parentEntry.name + File.separator);
                parent = parentEntry.directoryIndex;
            }
            paths[entry.index] = path.toString();
            final File out = new File(destRoot, path.toString());
            if (entry.directory) {
                //noinspection ResultOfMethodCallIgnored
                out.mkdirs();
                continue;
            }
            final File parentDir = out.getParentFile();
            if (parentDir != null) {
                //noinspection ResultOfMethodCallIgnored
                parentDir.mkdirs();
            }
            extractFile(entry, out);
            ++filesWritten;
        }
        return filesWritten;
    }

    private static Entry findByIndex(final List<Entry> entries, final int index) {
        for (final Entry e : entries) {
            if (e.index == index) {
                return e;
            }
        }
        return null;
    }

    private List<Entry> readDirectory() throws IOException {
        final List<Entry> entries = new ArrayList<>();
        final byte[] block = new byte[BLOCK_SIZE];
        int tableBlock = info.fileTableBlockNumber;
        int entryIndex = 0;
        for (int n = 0; n < info.fileTableBlockCount; ++n) {
            readBlock(tableBlock, block);
            for (int off = 0; off < BLOCK_SIZE; off += 0x40) {
                final int nameLenFlags = block[off + 40] & 0xFF;
                final int nameLength = nameLenFlags & 0x3F;
                if (nameLength == 0) {
                    continue;
                }
                final Entry entry = new Entry();
                entry.name = new String(block, off, Math.min(nameLength, 40),
                        StandardCharsets.ISO_8859_1);
                // Reject path escapes from a hostile package.
                if (entry.name.contains("..") || entry.name.contains("/")
                        || entry.name.contains("\\")) {
                    throw new IOException("Malformed entry name in package");
                }
                entry.directory = (nameLenFlags & 0x80) != 0;
                entry.startBlock = le24(block, off + 47);
                entry.directoryIndex = be16(block, off + 50);
                entry.length = be32(block, off + 52);
                entry.index = entryIndex;
                entries.add(entry);
                ++entryIndex;
            }
            tableBlock = nextBlock(tableBlock);
            if (tableBlock == END_OF_CHAIN) {
                break;
            }
        }
        return entries;
    }

    private void extractFile(final Entry entry, final File out) throws IOException {
        final byte[] block = new byte[BLOCK_SIZE];
        try (FileOutputStream stream = new FileOutputStream(out)) {
            int remaining = entry.length;
            int blockIndex = entry.startBlock;
            int guard = 0;
            while (remaining > 0) {
                if (blockIndex == END_OF_CHAIN
                        || ++guard > info.totalBlockCount + 4096) {
                    throw new IOException("Broken block chain in " + entry.name);
                }
                readBlock(blockIndex, block);
                final int chunk = Math.min(remaining, BLOCK_SIZE);
                stream.write(block, 0, chunk);
                remaining -= chunk;
                if (remaining > 0) {
                    blockIndex = nextBlock(blockIndex);
                }
            }
        }
    }

    // ---- block math (port of stfs_container_device.cc) --------------------

    private long headerBase() {
        return (info.headerSize + (BLOCK_SIZE - 1)) & ~(long) (BLOCK_SIZE - 1);
    }

    private long blockToOffset(final long blockIndex) {
        long base = BLOCKS_PER_HASH_LEVEL_0;
        long block = blockIndex;
        for (int i = 0; i < 3; i++) {
            block += ((blockIndex + base) / base) * blocksPerHashTable;
            if (blockIndex < base) {
                break;
            }
            base *= BLOCKS_PER_HASH_LEVEL_0;
        }
        return headerBase() + (block << 12);
    }

    private long hashBlockNumberLevel0(final int blockIndex) {
        if (blockIndex < BLOCKS_PER_HASH_LEVEL_0) {
            return 0;
        }
        long block = (long) (blockIndex / BLOCKS_PER_HASH_LEVEL_0) * blockStep0;
        block += ((long) (blockIndex / BLOCKS_PER_HASH_LEVEL_1) + 1)
                * blocksPerHashTable;
        if (blockIndex < BLOCKS_PER_HASH_LEVEL_1) {
            return block;
        }
        return block + blocksPerHashTable;
    }

    /** Next data block in the chain via the level-0 hash entry. */
    private int nextBlock(final int blockIndex) throws IOException {
        long tableOffset = headerBase() + (hashBlockNumberLevel0(blockIndex) << 12);
        // Writable packages with an active secondary root read the +1 block.
        if (!info.readOnly && info.rootActiveIndex) {
            tableOffset += BLOCK_SIZE;
        }
        final long entryOffset =
                tableOffset + (long) (blockIndex % BLOCKS_PER_HASH_LEVEL_0) * 0x18;
        final byte[] entry = new byte[0x18];
        file.seek(entryOffset);
        file.readFully(entry);
        // StfsHashEntry.info_raw (BE u32) low 24 bits = level0_next_block.
        return be32(entry, 0x14) & 0xFFFFFF;
    }

    private void readBlock(final int blockIndex, final byte[] out)
            throws IOException {
        final long offset = blockToOffset(blockIndex);
        file.seek(offset);
        final long available = file.length() - offset;
        if (available <= 0) {
            throw new IOException("Block out of range");
        }
        final int toRead = (int) Math.min(out.length, available);
        file.readFully(out, 0, toRead);
        for (int i = toRead; i < out.length; ++i) {
            out[i] = 0;
        }
    }

    // ---- byte helpers ------------------------------------------------------

    private static int be32(final byte[] b, final int off) {
        return ((b[off] & 0xFF) << 24) | ((b[off + 1] & 0xFF) << 16)
                | ((b[off + 2] & 0xFF) << 8) | (b[off + 3] & 0xFF);
    }

    private static int be16(final byte[] b, final int off) {
        return ((b[off] & 0xFF) << 8) | (b[off + 1] & 0xFF);
    }

    private static int le16(final byte[] b, final int off) {
        return (b[off] & 0xFF) | ((b[off + 1] & 0xFF) << 8);
    }

    private static int le24(final byte[] b, final int off) {
        return (b[off] & 0xFF) | ((b[off + 1] & 0xFF) << 8)
                | ((b[off + 2] & 0xFF) << 16);
    }

    private static int le32(final byte[] b, final int off) {
        return (b[off] & 0xFF) | ((b[off + 1] & 0xFF) << 8)
                | ((b[off + 2] & 0xFF) << 16) | ((b[off + 3] & 0xFF) << 24);
    }

    private static String utf16be(final byte[] b, final int off, final int maxChars) {
        final StringBuilder sb = new StringBuilder();
        for (int i = 0; i < maxChars; ++i) {
            final int c = be16(b, off + i * 2);
            if (c == 0) {
                break;
            }
            sb.append((char) c);
        }
        return sb.toString().trim();
    }

    // ---- filesystem helpers ------------------------------------------------

    /** Recursively deletes an installed package folder. */
    public static void deleteRecursively(final File root) {
        final ArrayDeque<File> stack = new ArrayDeque<>();
        final ArrayDeque<File> postOrder = new ArrayDeque<>();
        stack.push(root);
        while (!stack.isEmpty()) {
            final File f = stack.pop();
            postOrder.push(f);
            final File[] children = f.listFiles();
            if (children != null) {
                for (final File child : children) {
                    stack.push(child);
                }
            }
        }
        while (!postOrder.isEmpty()) {
            //noinspection ResultOfMethodCallIgnored
            postOrder.pop().delete();
        }
    }
}
