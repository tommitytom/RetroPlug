import { Line } from "lvgljs-ui";
import { useEffect, useState } from "react";
import { on, off } from "lvgljs";

const WAVEFORM_WIDTH = 750;
const WAVEFORM_HALF_HEIGHT = 150;
const WAVEFORM_CENTER_Y = 200;

export function Waveform() {
	const [points, setPoints] = useState<[number, number][]>([
		[0, WAVEFORM_CENTER_Y],
		[WAVEFORM_WIDTH, WAVEFORM_CENTER_Y],
	]);

	useEffect(() => {
		const handler = (buf: ArrayBuffer) => {
			const samples = new Float32Array(buf);
			const step = WAVEFORM_WIDTH / Math.max(1, samples.length - 1);
			const next: [number, number][] = new Array(samples.length);
			for (let i = 0; i < samples.length; i++) {
				next[i] = [i * step, WAVEFORM_CENTER_Y - samples[i] * WAVEFORM_HALF_HEIGHT];
			}
			setPoints(next);
		};
		on("waveform", handler);
		return () => off("waveform", handler);
	}, []);

	return (
		<Line
			points={points}
			style={{
				width: "100%",
				height: 300,
				"line-color": "#4fc3f7",
				"line-width": 2,
			}}
		/>
	);
}
