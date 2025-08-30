export const LSDJ_KIT_COUNT = 51;
export const LSDJ_KIT_SAMPLE_COUNT = 15;
export const GAMEBOY_SAMPLE_RATE = 11468;

// Utility function to play an audio sample using Web Audio API
export function playSample(audioContext: AudioContext, sampleData: Float32Array, volume: number, sampleRate: number) {
	if (!audioContext || !sampleData || sampleData.length === 0) return;

	// Create an audio buffer
	const buffer = audioContext.createBuffer(1, sampleData.length, sampleRate);
	const channelData = buffer.getChannelData(0);

	// Copy the sample data to the buffer
	for (let i = 0; i < sampleData.length; i++) {
		channelData[i] = sampleData[i] * volume;
	}

	// Create and configure buffer source
	const source = audioContext.createBufferSource();
	source.buffer = buffer;
	source.connect(audioContext.destination);

	// Play the sample
	source.start();
}
