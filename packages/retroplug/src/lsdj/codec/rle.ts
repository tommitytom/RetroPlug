// LSDj's RLE block compression for stored-project songs — the pure-TS port of
// Compression.cpp (a faithful liblsdj compression.c port). Tokens: 0xC0 (RLE),
// 0xE0 (special action), with 0xF0/0xF1 stamping a default wave/instrument, and
// block jumps (1-based jump byte -> 0-based block) terminated by 0xFF (EOF).
// Version-INDEPENDENT: the working song is stored raw; only stored projects are
// compressed. Throws on malformed input / size violations (the C++ returns an
// rfl::Error).

export const kBlockSize = 0x200; // 512
export const kBlockCount = 191;
export const kEofBlock = 0xff;
export const kEmptyBlock = 0xff;

const kSongBytes = 0x8000;
const RLE = 0xc0; // RUN_LENGTH_ENCODING_BYTE
const SA = 0xe0; // SPECIAL_ACTION_BYTE
const DEFAULT_WAVE_BYTE = 0xf0;
const DEFAULT_INSTR_BYTE = 0xf1;

const DEFAULT_WAVE = [0x8e, 0xcd, 0xcc, 0xbb, 0xaa, 0xa9, 0x99, 0x88, 0x87, 0x76, 0x66, 0x55, 0x54, 0x43, 0x32, 0x31];
const DEFAULT_INSTRUMENT = [0xa8, 0x00, 0x00, 0xff, 0x00, 0x00, 0x03, 0x00, 0x00, 0xd0, 0x00, 0x00, 0x00, 0xf3, 0x00, 0x00];

export interface Compressed {
  bytes: Uint8Array; // contiguous, block-padded (multiple of kBlockSize)
  blockCount: number;
}

/**
 * Decompress one project's song from the block area (kBlockCount*kBlockSize
 * bytes), starting at 0-based `startBlock`, following jumps. Returns the
 * 0x8000-byte song. Throws if malformed / wrong size.
 */
export function decompressProject(blockArea: Uint8Array, startBlock: number): Uint8Array {
  const out = new Uint8Array(kSongBytes);
  let o = 0;
  const push = (v: number): void => {
    if (o >= kSongBytes) throw new Error("decompress overflowed 0x8000 bytes");
    out[o++] = v & 0xff;
  };

  let pos = 0;
  const rd = (): number => {
    if (pos >= blockArea.length) throw new Error("decompress read past end of block area");
    return blockArea[pos++];
  };

  let curBlock = startBlock;
  for (let guard = 0; guard <= kBlockCount; guard++) {
    pos = curBlock * kBlockSize;
    let nextJump = -1; // set when a jump/EOF step is read
    while (nextJump < 0) {
      const byte = rd();
      if (byte === RLE) {
        const b = rd();
        if (b === RLE) {
          push(RLE);
        } else {
          const c = rd();
          for (let k = 0; k < c; k++) push(b);
        }
      } else if (byte === SA) {
        const a = rd();
        if (a === SA) {
          push(SA);
        } else if (a === DEFAULT_WAVE_BYTE) {
          const c = rd();
          for (let k = 0; k < c; k++) for (let j = 0; j < 16; j++) push(DEFAULT_WAVE[j]);
        } else if (a === DEFAULT_INSTR_BYTE) {
          const c = rd();
          for (let k = 0; k < c; k++) for (let j = 0; j < 16; j++) push(DEFAULT_INSTRUMENT[j]);
        } else {
          nextJump = a; // block jump or EOF
        }
      } else {
        push(byte);
      }
    }
    if ((nextJump & 0xff) === kEofBlock) break;
    const target = nextJump - 1; // 1-based -> 0-based
    if (target >= kBlockCount) throw new Error("decompress jump out of range");
    curBlock = target;
  }

  if (o !== kSongBytes) throw new Error("decompressed song is not 0x8000 bytes");
  return out;
}

/**
 * Compress a 0x8000-byte song into a block stream whose jumps are numbered from
 * 1-based `startBlock` (mirrors liblsdj compress_projects' currentBlock). Throws
 * if it would exceed kBlockCount.
 */
export function compressProject(song: Uint8Array, startBlock: number): Compressed {
  if (song.length < kSongBytes) throw new Error("song smaller than 0x8000 bytes");

  const out: number[] = [];
  let currentBlock = startBlock; // 1-based
  let currentBlockSize = 0;
  let read = 0;

  const matchesRun = (pat: number[]): boolean => {
    if (read + 16 >= kSongBytes) return false;
    for (let k = 0; k < 16; k++) if (song[read + k] !== pat[k]) return false;
    return true;
  };

  while (read < kSongBytes) {
    let event: number[];

    let dwCount = 0;
    while (matchesRun(DEFAULT_WAVE) && dwCount !== 0xff) {
      read += 16;
      ++dwCount;
    }
    if (dwCount > 0) {
      event = [SA, DEFAULT_WAVE_BYTE, dwCount];
    } else {
      let diCount = 0;
      while (matchesRun(DEFAULT_INSTRUMENT) && diCount !== 0xff) {
        read += 16;
        ++diCount;
      }
      if (diCount > 0) {
        event = [SA, DEFAULT_INSTR_BYTE, diCount];
      } else {
        const c = song[read];
        if (c === RLE) {
          event = [RLE, RLE];
          ++read;
        } else if (c === SA) {
          event = [SA, SA];
          ++read;
        } else if (read + 3 < kSongBytes && song[read + 1] === c && song[read + 2] === c && song[read + 3] === c) {
          let count = 0;
          while (read < kSongBytes && song[read] === c && count !== 0xff) {
            ++count;
            ++read;
          }
          event = [RLE, c, count];
        } else {
          event = [song[read++]];
        }
      }
    }

    if (currentBlockSize + event.length + 2 >= kBlockSize) {
      out.push(SA, (currentBlock + 1) & 0xff);
      currentBlockSize += 2;
      while (currentBlockSize < kBlockSize) {
        out.push(0);
        ++currentBlockSize;
      }
      ++currentBlock;
      currentBlockSize = 0;
      if (currentBlock === kBlockCount + 1) throw new Error("compressed song exceeds block count");
      // fall through: write the event in the new block
    }

    for (const b of event) out.push(b);
    currentBlockSize += event.length;
  }

  out.push(SA, kEofBlock);
  if (currentBlockSize > 0) {
    currentBlockSize += 2;
    while (currentBlockSize < kBlockSize) {
      out.push(0);
      ++currentBlockSize;
    }
  }

  return { bytes: Uint8Array.from(out), blockCount: out.length / kBlockSize };
}
