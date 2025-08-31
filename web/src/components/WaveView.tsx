import React, { useEffect, useRef, useState } from "react";

function renderWaveForm(
	canvas: HTMLCanvasElement,
	sampleData: Float32Array,
	markers: number[],
) {
	const ctx = canvas.getContext("2d");
	if (!ctx) return;

	const width = canvas.clientWidth;
	const height = canvas.clientHeight;

	ctx.clearRect(0, 0, width, height);

	ctx.save();
	ctx.strokeStyle = "white";
	ctx.lineWidth = 1;
	ctx.beginPath();

	const step = Math.ceil(sampleData.length / width);
	for (let x = 0; x < width; x++) {
		const min = Math.min(...sampleData.subarray(x * step, (x + 1) * step));
		const max = Math.max(...sampleData.subarray(x * step, (x + 1) * step));
		ctx.moveTo(x + 0.5, ((1 + min) * height) / 2);
		ctx.lineTo(x + 0.5, ((1 + max) * height) / 2);
	}
	ctx.stroke();
	ctx.restore();

	ctx.save();
	ctx.strokeStyle = "red";
	ctx.lineWidth = 1;
	ctx.beginPath();
	for (const marker of markers) {
		const scaledMarker = (marker / sampleData.length) * width;
		ctx.moveTo(scaledMarker + 0.5, 0);
		ctx.lineTo(scaledMarker + 0.5, height);
	}
	ctx.stroke();
	ctx.restore();
}

type WaveViewProps = {
	sampleData?: Float32Array | null;
	markers?: number[];
	className?: string;
};
export const WaveView: React.FC<WaveViewProps> = ({
	sampleData,
	markers,
	className,
}) => {
	const canvasRef = useRef<HTMLCanvasElement | null>(null);
	const [canvasSize, setCanvasSize] = useState<{ width: number; height: number }>({ width: 0, height: 0 });

	// Use ResizeObserver to track canvas size changes
	useEffect(() => {
		const canvas = canvasRef.current;
		if (!canvas) return;

		const updateSize = () => {
			setCanvasSize({ width: canvas.clientWidth, height: canvas.clientHeight });
		};

		updateSize();

		const resizeObserver = new ResizeObserver(updateSize);
		resizeObserver.observe(canvas);

		return () => {
			resizeObserver.disconnect();
		};
	}, []);

	useEffect(() => {
		const canvas = canvasRef.current;
		if (!canvas || !sampleData) return;

		const dpr = window.devicePixelRatio || 1;
		const width = canvas.clientWidth;
		const height = canvas.clientHeight;
		if (width === 0 || height === 0) return;

		canvas.width = width * dpr;
		canvas.height = height * dpr;

		const ctx = canvas.getContext("2d");
		if (ctx) {
			ctx.setTransform(1, 0, 0, 1, 0, 0);
			ctx.scale(dpr, dpr);
		}

		renderWaveForm(canvas, sampleData, markers || []);
	}, [sampleData, markers, canvasSize]);

	return <canvas ref={canvasRef} className={className} />;
};
