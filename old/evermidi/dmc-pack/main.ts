#!/usr/bin/env node
/**
 * dmc-pack — Pack a folder of WAV files into NES DMC sample bank files.
 *
 * Usage:
 *   npx tsx dmc-pack.ts <input-folder> [options]
 *
 * Options:
 *   --output, -o <dir>    Output directory (default: ./MIDI)
 *   --rate, -r <0-15>     DMC rate index (default: 15 = ~33 kHz)
 *   --max-samples <n>     Max samples per bank (default: 64)
 *
 * Output:
 *   BANK01.DMC, BANK02.DMC — ready to copy to SD card /MIDI/ folder
 *
 * Bank file format:
 *   Header (8 bytes):
 *     [0-3]  magic "NDMC"
 *     [4]    version (1)
 *     [5]    sample count (1-64)
 *     [6-7]  reserved
 *   Directory (sample_count × 4 bytes):
 *     [0]    $4012 value (addr = $C000 + val×64)
 *     [1]    $4013 value (len  = val×16 + 1)
 *     [2]    default rate index (0-15)
 *     [3]    flags (bit 0 = loop)
 *   DPCM data (exactly 16384 bytes, zero-padded)
 */

import { readFileSync, writeFileSync, mkdirSync, readdirSync, existsSync } from "fs";
import { join, extname, basename } from "path";

/* ------------------------------------------------------------------ */
/*  Constants                                                          */
/* ------------------------------------------------------------------ */

const BANK_SIZE = 16384;
const BANK_USABLE = 16378; /* leave 6 bytes for vectors at $FFFA-$FFFF */
const MAX_DMC_LEN = 4081;
const ADDR_ALIGN = 64;
const LEN_GRAIN = 16;
const MAGIC = Buffer.from("NDMC");
const VERSION = 1;

/** NTSC DMC playback rates in Hz (index 0-15). */
const DMC_RATES: readonly number[] = [
	4181.71, 4709.93, 5264.04, 5593.04,
	6257.95, 7046.35, 7919.35, 8363.42,
	9419.86, 11186.1, 12604.0, 13982.6,
	16884.6, 21306.8, 24858.0, 33143.9,
];

/* ------------------------------------------------------------------ */
/*  WAV reader                                                         */
/* ------------------------------------------------------------------ */

interface WavData {
	sampleRate: number;
	channels: number;
	bitsPerSample: number;
	samples: Float64Array; /* mono, -1..+1 */
}

function parseWav(buf: Buffer): WavData {
	if (buf.toString("ascii", 0, 4) !== "RIFF") throw new Error("Not a RIFF file");
	if (buf.toString("ascii", 8, 12) !== "WAVE") throw new Error("Not a WAVE file");

	let pos = 12;
	let fmt: { audioFormat: number; channels: number; sampleRate: number; bitsPerSample: number } | null = null;
	let dataChunk: Buffer | null = null;

	while (pos < buf.length - 8) {
		const id = buf.toString("ascii", pos, pos + 4);
		const size = buf.readUInt32LE(pos + 4);
		const chunkData = buf.subarray(pos + 8, pos + 8 + size);

		if (id === "fmt ") {
			fmt = {
				audioFormat: chunkData.readUInt16LE(0),
				channels: chunkData.readUInt16LE(2),
				sampleRate: chunkData.readUInt32LE(4),
				bitsPerSample: chunkData.readUInt16LE(14),
			};
		} else if (id === "data") {
			dataChunk = chunkData;
		}

		pos += 8 + size;
		if (size % 2 !== 0) pos++; /* RIFF chunks are word-aligned */
	}

	if (!fmt) throw new Error("Missing fmt chunk");
	if (!dataChunk) throw new Error("Missing data chunk");
	if (fmt.audioFormat !== 1 && fmt.audioFormat !== 3) {
		throw new Error(`Unsupported audio format ${fmt.audioFormat} (need PCM=1 or float=3)`);
	}

	const bytesPerSample = fmt.bitsPerSample / 8;
	const frameCount = Math.floor(dataChunk.length / (bytesPerSample * fmt.channels));
	const mono = new Float64Array(frameCount);

	for (let i = 0; i < frameCount; i++) {
		let sum = 0;
		for (let ch = 0; ch < fmt.channels; ch++) {
			const off = (i * fmt.channels + ch) * bytesPerSample;
			if (fmt.audioFormat === 3) {
				/* 32-bit float */
				sum += dataChunk.readFloatLE(off);
			} else if (fmt.bitsPerSample === 8) {
				/* 8-bit unsigned */
				sum += (dataChunk[off] - 128) / 128;
			} else if (fmt.bitsPerSample === 16) {
				sum += dataChunk.readInt16LE(off) / 32768;
			} else if (fmt.bitsPerSample === 24) {
				const val = dataChunk[off] | (dataChunk[off + 1] << 8) | (dataChunk[off + 2] << 16);
				sum += ((val << 8) >> 8) / 8388608; /* sign-extend 24→32, then normalize */
			} else if (fmt.bitsPerSample === 32) {
				sum += dataChunk.readInt32LE(off) / 2147483648;
			} else {
				throw new Error(`Unsupported bit depth: ${fmt.bitsPerSample}`);
			}
		}
		mono[i] = sum / fmt.channels;
	}

	return {
		sampleRate: fmt.sampleRate,
		channels: fmt.channels,
		bitsPerSample: fmt.bitsPerSample,
		samples: mono,
	};
}

/* ------------------------------------------------------------------ */
/*  Resampler (linear interpolation)                                   */
/* ------------------------------------------------------------------ */

function resample(src: Float64Array, srcRate: number, dstRate: number): Float64Array {
	if (srcRate === dstRate) return src;

	const ratio = srcRate / dstRate;
	const dstLen = Math.floor(src.length / ratio);
	const dst = new Float64Array(dstLen);

	for (let i = 0; i < dstLen; i++) {
		const srcPos = i * ratio;
		const idx = Math.floor(srcPos);
		const frac = srcPos - idx;

		const a = src[idx] ?? 0;
		const b = src[Math.min(idx + 1, src.length - 1)] ?? 0;
		dst[i] = a + (b - a) * frac;
	}

	return dst;
}

/* ------------------------------------------------------------------ */
/*  DPCM encoder                                                       */
/* ------------------------------------------------------------------ */

function encodeDpcm(samples: Float64Array): Uint8Array {
	/*
	 * NES DMC: 1-bit delta modulation.
	 * DAC is 7-bit (0-127). Each bit:
	 *   1 → level += 2 (clamped to 127)
	 *   0 → level -= 2 (clamped to 0)
	 * Bits packed LSB-first.
	 */

	const totalBits = samples.length;
	const totalBytes = Math.ceil(totalBits / 8);
	const out = new Uint8Array(totalBytes);

	let level = 64; /* start at midpoint */

	for (let i = 0; i < totalBits; i++) {
		/* target level: map -1..+1 → 0..127 */
		const target = Math.round((samples[i] + 1) * 63.5);
		const clamped = Math.max(0, Math.min(127, target));

		if (clamped >= level) {
			/* output 1: level goes up */
			out[i >> 3] |= 1 << (i & 7);
			level = Math.min(level + 2, 127);
		} else {
			/* output 0: level goes down (bit already 0) */
			level = Math.max(level - 2, 0);
		}
	}

	return out;
}

/* ------------------------------------------------------------------ */
/*  Pad DPCM data to DMC length granularity                            */
/* ------------------------------------------------------------------ */

/**
 * DMC length register: actual bytes = val×16 + 1.
 * Valid lengths: 1, 17, 33, 49, ... 4081.
 * Round up to the next valid length.
 */
function padToValidLength(data: Uint8Array): Uint8Array {
	const raw = data.length;
	/* val = ceil((raw - 1) / 16) */
	let val = Math.ceil((raw - 1) / 16);
	if (val < 0) val = 0;

	const paddedLen = Math.min(val * 16 + 1, MAX_DMC_LEN);
	if (paddedLen <= raw) return data.subarray(0, paddedLen);

	const padded = new Uint8Array(paddedLen);
	padded.set(data.subarray(0, Math.min(raw, paddedLen)));
	return padded;
}

/* ------------------------------------------------------------------ */
/*  Bank packer                                                        */
/* ------------------------------------------------------------------ */

interface DmcSample {
	name: string;
	data: Uint8Array; /* padded DPCM bytes */
	rate: number; /* rate index 0-15 */
}

interface DirEntry {
	addrReg: number; /* $4012 value */
	lenReg: number; /* $4013 value */
	rate: number;
	flags: number;
}

function packBank(samples: DmcSample[]): { dir: DirEntry[]; data: Buffer } {
	const bankBuf = Buffer.alloc(BANK_SIZE);
	let offset = 0; /* byte offset within the 16KB bank */
	const dir: DirEntry[] = [];

	for (const smp of samples) {
		/* align to 64 bytes */
		const aligned = Math.ceil(offset / ADDR_ALIGN) * ADDR_ALIGN;

		if (aligned + smp.data.length > BANK_USABLE) {
			console.warn(`  ⚠ "${smp.name}" doesn't fit in bank, skipping`);
			continue;
		}

		offset = aligned;

		/* $4012: addr register = offset / 64 */
		const addrReg = offset / ADDR_ALIGN;
		/* $4013: len register = (byteLen - 1) / 16 */
		const lenReg = (smp.data.length - 1) / LEN_GRAIN;

		dir.push({
			addrReg,
			lenReg,
			rate: smp.rate,
			flags: 0,
		});

		bankBuf.set(smp.data, offset);
		offset += smp.data.length;
	}

	return { dir, data: bankBuf };
}

function writeBankFile(path: string, dir: DirEntry[]): void {
	/* header: 8 bytes */
	const headerSize = 8;
	const dirSize = dir.length * 4;
	const totalSize = headerSize + dirSize + BANK_SIZE;
	const out = Buffer.alloc(totalSize);

	/* header */
	MAGIC.copy(out, 0);
	out[4] = VERSION;
	out[5] = dir.length;
	out[6] = 0;
	out[7] = 0;

	/* directory */
	for (let i = 0; i < dir.length; i++) {
		const base = headerSize + i * 4;
		out[base + 0] = dir[i].addrReg;
		out[base + 1] = dir[i].lenReg;
		out[base + 2] = dir[i].rate;
		out[base + 3] = dir[i].flags;
	}

	/* the bank data was already written to bankBuf — we need to get it here */
	/* Actually, let's restructure: return bank data from packBank and write here */
	writeFileSync(path, out);
}

/* ------------------------------------------------------------------ */
/*  Main                                                               */
/* ------------------------------------------------------------------ */

function main(): void {
	const args = process.argv.slice(2);

	let inputDir = "";
	let outputDir = "./MIDI";
	let rateIndex = 15;
	let maxPerBank = 64;

	/* parse args */
	for (let i = 0; i < args.length; i++) {
		const a = args[i];
		if (a === "--output" || a === "-o") {
			outputDir = args[++i];
		} else if (a === "--rate" || a === "-r") {
			rateIndex = parseInt(args[++i], 10);
			if (rateIndex < 0 || rateIndex > 15) {
				console.error("Rate index must be 0-15");
				process.exit(1);
			}
		} else if (a === "--max-samples") {
			maxPerBank = parseInt(args[++i], 10);
		} else if (a === "--help" || a === "-h") {
			console.log("Usage: dmc-pack <input-folder> [--output <dir>] [--rate <0-15>]");
			console.log("");
			console.log("Packs WAV files into NES DMC sample bank files for EverDrive N8 MIDI.");
			console.log("");
			console.log("Options:");
			console.log("  --output, -o <dir>   Output directory (default: ./MIDI)");
			console.log("  --rate, -r <0-15>    DMC rate index (default: 15 = ~33 kHz)");
			console.log("  --max-samples <n>    Max samples per bank (default: 64)");
			console.log("");
			console.log("DMC rate table:");
			DMC_RATES.forEach((r, i) => console.log(`  ${String(i).padStart(2)}: ${r.toFixed(1)} Hz`));
			process.exit(0);
		} else if (!inputDir) {
			inputDir = a;
		} else {
			console.error(`Unknown argument: ${a}`);
			process.exit(1);
		}
	}

	if (!inputDir) {
		console.error("Usage: dmc-pack <input-folder> [--output <dir>] [--rate <0-15>]");
		process.exit(1);
	}

	const targetRate = DMC_RATES[rateIndex];
	console.log(`DMC rate index ${rateIndex} → ${targetRate.toFixed(1)} Hz`);
	console.log(`Reading WAV files from: ${inputDir}`);
	console.log(`Output directory: ${outputDir}`);
	console.log("");

	/* read and convert all WAV files */
	const wavFiles = readdirSync(inputDir)
		.filter((f) => extname(f).toLowerCase() === ".wav")
		.sort();

	if (wavFiles.length === 0) {
		console.error("No .wav files found in input directory");
		process.exit(1);
	}

	const allSamples: DmcSample[] = [];

	for (const file of wavFiles) {
		const path = join(inputDir, file);
		const name = basename(file, extname(file));

		try {
			const wav = parseWav(readFileSync(path));
			console.log(
				`  ${file}: ${wav.sampleRate} Hz, ${wav.channels}ch, ` +
				`${wav.bitsPerSample}-bit, ${wav.samples.length} samples`
			);

			/* resample to target DMC rate */
			const resampled = resample(wav.samples, wav.sampleRate, targetRate);

			/* encode to DPCM */
			const dpcm = encodeDpcm(resampled);

			/* pad to valid DMC length */
			const padded = padToValidLength(dpcm);

			if (padded.length > MAX_DMC_LEN) {
				console.warn(`    ⚠ truncated to ${MAX_DMC_LEN} bytes (${(MAX_DMC_LEN * 8 / targetRate).toFixed(2)}s)`);
			}

			const finalData = Buffer.from(padded.subarray(0, Math.min(padded.length, MAX_DMC_LEN)));

			console.log(
				`    → ${finalData.length} bytes DPCM ` +
				`(${(finalData.length * 8 / targetRate * 1000).toFixed(1)} ms)`
			);

			allSamples.push({ name, data: finalData, rate: rateIndex });
		} catch (err: any) {
			console.error(`  ✗ ${file}: ${err.message}`);
		}
	}

	if (allSamples.length === 0) {
		console.error("\nNo samples were successfully converted");
		process.exit(1);
	}

	/* distribute samples across banks */
	const banks: DmcSample[][] = [[]];
	let currentBankOffset = 0;
	let currentBankIdx = 0;

	for (const smp of allSamples) {
		const aligned = Math.ceil(currentBankOffset / ADDR_ALIGN) * ADDR_ALIGN;
		const needed = aligned + smp.data.length;

		/* check if sample fits in current bank */
		if (needed > BANK_USABLE || banks[currentBankIdx].length >= maxPerBank) {
			/* start a new bank */
			currentBankIdx++;
			if (currentBankIdx >= 2) {
				console.warn(`\n⚠ More than 2 banks needed — remaining samples dropped`);
				break;
			}
			banks.push([]);
			currentBankOffset = 0;
		}

		banks[currentBankIdx].push(smp);
		const alignedStart = Math.ceil(currentBankOffset / ADDR_ALIGN) * ADDR_ALIGN;
		currentBankOffset = alignedStart + smp.data.length;
	}

	/* write bank files */
	if (!existsSync(outputDir)) {
		mkdirSync(outputDir, { recursive: true });
	}

	console.log("");

	for (let b = 0; b < banks.length; b++) {
		const bankSamples = banks[b];
		if (bankSamples.length === 0) continue;

		const { dir, data: bankData } = packBank(bankSamples);

		const bankNum = String(b + 1).padStart(2, "0");
		const filePath = join(outputDir, `BANK${bankNum}.DMC`);

		/* build complete file */
		const headerSize = 8;
		const dirSize = dir.length * 4;
		const totalSize = headerSize + dirSize + BANK_SIZE;
		const out = Buffer.alloc(totalSize);

		/* header */
		MAGIC.copy(out, 0);
		out[4] = VERSION;
		out[5] = dir.length;
		out[6] = 0;
		out[7] = 0;

		/* directory */
		for (let i = 0; i < dir.length; i++) {
			const base = headerSize + i * 4;
			out[base + 0] = dir[i].addrReg;
			out[base + 1] = dir[i].lenReg;
			out[base + 2] = dir[i].rate;
			out[base + 3] = dir[i].flags;
		}

		/* DPCM data */
		bankData.copy(out, headerSize + dirSize);

		writeFileSync(filePath, out);

		const usedBytes = bankSamples.reduce((sum, s) => {
			return Math.ceil(sum / ADDR_ALIGN) * ADDR_ALIGN + s.data.length;
		}, 0);

		console.log(
			`Bank ${b + 1}: ${bankSamples.length} samples, ` +
			`${usedBytes} / ${BANK_USABLE} bytes used → ${filePath}`
		);

		/* print sample map */
		for (let i = 0; i < dir.length; i++) {
			const smp = bankSamples[i];
			const addr = 0xc000 + dir[i].addrReg * 64;
			const len = dir[i].lenReg * 16 + 1;
			console.log(
				`    note ${String(i).padStart(3)}: ` +
				`$${addr.toString(16).toUpperCase().padStart(4, "0")} ` +
				`${String(len).padStart(4)}B ` +
				`${smp.name}`
			);
		}
	}

	console.log("\nDone!");
}

main();