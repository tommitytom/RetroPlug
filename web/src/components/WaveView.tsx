import React, { useEffect, useRef, useState, useCallback } from "react";
import type { SliceInfo, ZoomState } from './WaveViewTypes';

function renderWaveForm(
	canvas: HTMLCanvasElement,
	sampleData: Float32Array,
	markers: number[],
	zoom: ZoomState,
	hoveredSlice: number | null,
) {
	const ctx = canvas.getContext("2d");
	if (!ctx) return;

	const width = canvas.clientWidth;
	const height = canvas.clientHeight;

	ctx.clearRect(0, 0, width, height);

	// Calculate the visible range of samples based on zoom
	const visibleSamples = sampleData.length / zoom.scale;
	const startSample = Math.max(0, zoom.offset);
	const endSample = Math.min(sampleData.length, zoom.offset + visibleSamples);

	// Highlight hovered slice if any
	if (hoveredSlice !== null && markers.length > 0) {
		ctx.save();
		ctx.fillStyle = "rgba(255, 255, 255, 0.1)";

		const sortedMarkers = [...markers].sort((a, b) => a - b);
		let sliceStart = 0;
		let sliceEnd = sampleData.length;

		if (hoveredSlice === 0) {
			// Before first marker
			sliceEnd = sortedMarkers[0];
		} else if (hoveredSlice <= sortedMarkers.length) {
			// Between markers or after last marker
			sliceStart = sortedMarkers[hoveredSlice - 1];
			if (hoveredSlice < sortedMarkers.length) {
				sliceEnd = sortedMarkers[hoveredSlice];
			}
		}

		// Convert to screen coordinates
		if (sliceEnd > startSample && sliceStart < endSample) {
			const screenStart = Math.max(0, ((sliceStart - startSample) / visibleSamples) * width);
			const screenEnd = Math.min(width, ((sliceEnd - startSample) / visibleSamples) * width);
			ctx.fillRect(screenStart, 0, screenEnd - screenStart, height);
		}
		ctx.restore();
	}

	ctx.save();
	ctx.strokeStyle = "white";
	ctx.lineWidth = 1;

	// Calculate samples per pixel for the visible range
	const samplesPerPixel = visibleSamples / width;

	// Linear interpolation helper
	const lerp = (a: number, b: number, t: number): number => {
		return a + (b - a) * t;
	};

	// Get sample value with interpolation if needed
	const getSampleValue = (position: number): number => {
		if (position < 0) return sampleData[0];
		if (position >= sampleData.length - 1) return sampleData[sampleData.length - 1];

		const index = Math.floor(position);
		const fraction = position - index;

		if (fraction === 0 || index >= sampleData.length - 1) {
			return sampleData[index];
		}

		// Linear interpolation between samples
		return lerp(sampleData[index], sampleData[index + 1], fraction);
	};

	if (samplesPerPixel < 1) {
		// Interpolation mode - when zoomed in close
		ctx.beginPath();

		for (let x = 0; x < width; x++) {
			const samplePosition = startSample + (x * samplesPerPixel);
			const value = getSampleValue(samplePosition);
			const y = ((1 + value) * height) / 2;

			if (x === 0) {
				ctx.moveTo(x + 0.5, y);
			} else {
				ctx.lineTo(x + 0.5, y);
			}
		}

		ctx.stroke();
	} else if (samplesPerPixel < 4) {
		// Hybrid mode - for intermediate zoom levels
		// Draw connected lines through actual sample points with optional min/max fills
		ctx.beginPath();

		let lastY: number | null = null;

		for (let x = 0; x < width; x++) {
			const pixelStartSample = startSample + (x * samplesPerPixel);
			const pixelEndSample = startSample + ((x + 1) * samplesPerPixel);

			const startIndex = Math.floor(pixelStartSample);
			const endIndex = Math.min(Math.ceil(pixelEndSample), sampleData.length);

			if (startIndex >= sampleData.length) continue;

			// If we have very few samples in this pixel, interpolate
			if (endIndex - startIndex <= 2) {
				const value = getSampleValue(pixelStartSample + samplesPerPixel * 0.5);
				const y = ((1 + value) * height) / 2;

				if (lastY === null) {
					ctx.moveTo(x + 0.5, y);
				} else {
					ctx.lineTo(x + 0.5, y);
				}
				lastY = y;
			} else {
				// Find min/max for this pixel
				let min = Infinity;
				let max = -Infinity;

				for (let i = startIndex; i < endIndex; i++) {
					const value = sampleData[i];
					if (value < min) min = value;
					if (value > max) max = value;
				}

				const minY = ((1 + min) * height) / 2;
				const maxY = ((1 + max) * height) / 2;

				// Connect from last point if exists
				if (lastY !== null) {
					// Connect to whichever is closer
					const toMin = Math.abs(lastY - minY);
					const toMax = Math.abs(lastY - maxY);
					if (toMin < toMax) {
						ctx.lineTo(x + 0.5, minY);
						ctx.lineTo(x + 0.5, maxY);
					} else {
						ctx.lineTo(x + 0.5, maxY);
						ctx.lineTo(x + 0.5, minY);
					}
					lastY = (minY + maxY) / 2;
				} else {
					ctx.moveTo(x + 0.5, minY);
					ctx.lineTo(x + 0.5, maxY);
					lastY = (minY + maxY) / 2;
				}
			}
		}

		ctx.stroke();
	} else {
		// Full decimation mode - when zoomed out significantly
		ctx.beginPath();

		for (let x = 0; x < width; x++) {
			// Calculate the exact sample range for this pixel
			const pixelStartSample = startSample + (x * samplesPerPixel);
			const pixelEndSample = startSample + ((x + 1) * samplesPerPixel);

			const startIndex = Math.floor(pixelStartSample);
			const endIndex = Math.min(Math.ceil(pixelEndSample), sampleData.length);

			if (startIndex >= endIndex || startIndex >= sampleData.length) continue;

			// Find min/max in the sample range
			let min = Infinity;
			let max = -Infinity;

			for (let i = startIndex; i < endIndex; i++) {
				const value = sampleData[i];
				if (value < min) min = value;
				if (value > max) max = value;
			}

			// Draw vertical line from min to max
			const minY = ((1 + min) * height) / 2;
			const maxY = ((1 + max) * height) / 2;

			ctx.moveTo(x + 0.5, minY);
			ctx.lineTo(x + 0.5, maxY);
		}

		ctx.stroke();
	}

	ctx.restore();

	// Render markers
	ctx.save();
	ctx.strokeStyle = "red";
	ctx.lineWidth = 1;
	ctx.beginPath();
	for (const marker of markers) {
		// Check if marker is in visible range
		if (marker >= startSample && marker <= endSample) {
			const markerPosition = ((marker - startSample) / visibleSamples) * width;
			ctx.moveTo(markerPosition + 0.5, 0);
			ctx.lineTo(markerPosition + 0.5, height);
		}
	}
	ctx.stroke();
	ctx.restore();
}

function getSliceAtSample(sample: number, markers: number[], totalSamples: number): SliceInfo | null {
	if (!markers || markers.length === 0) {
		return null;
	}

	const sortedMarkers = [...markers].sort((a, b) => a - b);

	// Find which slice the sample falls into
	if (sample < sortedMarkers[0]) {
		// Before first marker
		return {
			index: 0,
			startSample: 0,
			endSample: sortedMarkers[0],
			startMarkerIndex: null,
			endMarkerIndex: 0,
		};
	}

	for (let i = 0; i < sortedMarkers.length - 1; i++) {
		if (sample >= sortedMarkers[i] && sample < sortedMarkers[i + 1]) {
			// Between two markers
			return {
				index: i + 1,
				startSample: sortedMarkers[i],
				endSample: sortedMarkers[i + 1],
				startMarkerIndex: i,
				endMarkerIndex: i + 1,
			};
		}
	}

	// After last marker
	if (sample >= sortedMarkers[sortedMarkers.length - 1]) {
		return {
			index: sortedMarkers.length,
			startSample: sortedMarkers[sortedMarkers.length - 1],
			endSample: totalSamples,
			startMarkerIndex: sortedMarkers.length - 1,
			endMarkerIndex: null,
		};
	}

	return null;
}

type WaveViewProps = {
	sampleData?: Float32Array | null;
	markers?: number[];
	className?: string;
	minZoom?: number;
	maxZoom?: number;
	zoomSensitivity?: number;
	onSliceClick?: (slice: SliceInfo) => void;
	onSliceMouseMove?: (slice: SliceInfo | null) => void;
};

export const WaveView: React.FC<WaveViewProps> = ({
	sampleData,
	markers,
	className,
	minZoom = 1,
	maxZoom = 100,
	zoomSensitivity = 0.001,
	onSliceClick,
	onSliceMouseMove,
}) => {
	const canvasRef = useRef<HTMLCanvasElement | null>(null);
	const [canvasSize, setCanvasSize] = useState<{ width: number; height: number }>({ width: 0, height: 0 });
	const [zoom, setZoom] = useState<ZoomState>({ scale: 1, offset: 0 });
	const [hoveredSlice, setHoveredSlice] = useState<number | null>(null);
	const lastReportedSliceRef = useRef<number | null>(null);

	// Convert mouse position to sample position considering zoom
	const mouseToSample = useCallback((mouseX: number, canvas: HTMLCanvasElement): number => {
		if (!sampleData) return 0;

		const normalizedX = mouseX / canvas.clientWidth;
		const visibleSamples = sampleData.length / zoom.scale;
		return zoom.offset + normalizedX * visibleSamples;
	}, [sampleData, zoom]);

	// Handle mouse click
	const handleClick = useCallback((e: MouseEvent) => {
		if (!sampleData || !canvasRef.current || !markers || markers.length === 0) return;

		const canvas = canvasRef.current;
		const rect = canvas.getBoundingClientRect();
		const mouseX = e.clientX - rect.left;
		const sample = mouseToSample(mouseX, canvas);

		const slice = getSliceAtSample(sample, markers, sampleData.length);
		if (slice && onSliceClick) {
			onSliceClick(slice);
		}
	}, [sampleData, markers, mouseToSample, onSliceClick]);

	// Handle mouse move
	const handleMouseMove = useCallback((e: MouseEvent) => {
		if (!sampleData || !canvasRef.current || !markers || markers.length === 0) {
			if (hoveredSlice !== null) {
				setHoveredSlice(null);
				if (onSliceMouseMove && lastReportedSliceRef.current !== null) {
					onSliceMouseMove(null);
					lastReportedSliceRef.current = null;
				}
			}
			return;
		}

		const canvas = canvasRef.current;
		const rect = canvas.getBoundingClientRect();
		const mouseX = e.clientX - rect.left;
		const sample = mouseToSample(mouseX, canvas);

		const slice = getSliceAtSample(sample, markers, sampleData.length);
		const currentSliceIndex = slice?.index ?? null;

		// Update visual hover state
		setHoveredSlice(currentSliceIndex);

		// Only fire onSliceMouseMove when moving to a different slice
		if (onSliceMouseMove && currentSliceIndex !== lastReportedSliceRef.current) {
			onSliceMouseMove(slice);
			lastReportedSliceRef.current = currentSliceIndex;
		}
	}, [sampleData, markers, mouseToSample, onSliceMouseMove, hoveredSlice]);

	// Handle mouse leave
	const handleMouseLeave = useCallback(() => {
		setHoveredSlice(null);
		if (onSliceMouseMove && lastReportedSliceRef.current !== null) {
			onSliceMouseMove(null);
			lastReportedSliceRef.current = null;
		}
	}, [onSliceMouseMove]);

	// Handle mouse wheel zoom
	const handleWheel = useCallback((e: WheelEvent) => {
		e.preventDefault();

		if (!sampleData || !canvasRef.current) return;

		const canvas = canvasRef.current;
		const rect = canvas.getBoundingClientRect();
		const mouseX = e.clientX - rect.left;
		const normalizedMouseX = mouseX / canvas.clientWidth;

		setZoom(prevZoom => {
			// Calculate new scale
			const delta = e.deltaY * zoomSensitivity;
			const scaleFactor = Math.exp(-delta);
			const newScale = Math.max(minZoom, Math.min(maxZoom, prevZoom.scale * scaleFactor));

			// Calculate the sample position under the mouse cursor
			const visibleSamples = sampleData.length / prevZoom.scale;
			const mousePosSample = prevZoom.offset + normalizedMouseX * visibleSamples;

			// Calculate new offset to keep the same sample under the mouse cursor
			const newVisibleSamples = sampleData.length / newScale;
			const newOffset = mousePosSample - normalizedMouseX * newVisibleSamples;

			// Clamp offset to valid range
			const maxOffset = Math.max(0, sampleData.length - newVisibleSamples);
			const clampedOffset = Math.max(0, Math.min(maxOffset, newOffset));

			return {
				scale: newScale,
				offset: clampedOffset,
			};
		});
	}, [sampleData, minZoom, maxZoom, zoomSensitivity]);

	// Reset zoom on double click
	const handleDoubleClick = useCallback(() => {
		//setZoom({ scale: 1, offset: 0 });
	}, []);

	// Add event listeners
	useEffect(() => {
		const canvas = canvasRef.current;
		if (!canvas) return;

		canvas.addEventListener("wheel", handleWheel, { passive: false });
		canvas.addEventListener("dblclick", handleDoubleClick);
		canvas.addEventListener("click", handleClick);
		canvas.addEventListener("mousemove", handleMouseMove);
		canvas.addEventListener("mouseleave", handleMouseLeave);

		return () => {
			canvas.removeEventListener("wheel", handleWheel);
			canvas.removeEventListener("dblclick", handleDoubleClick);
			canvas.removeEventListener("click", handleClick);
			canvas.removeEventListener("mousemove", handleMouseMove);
			canvas.removeEventListener("mouseleave", handleMouseLeave);
		};
	}, [handleWheel, handleDoubleClick, handleClick, handleMouseMove, handleMouseLeave]);

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

	// Render waveform
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

		renderWaveForm(canvas, sampleData, markers || [], zoom, hoveredSlice);
	}, [sampleData, markers, canvasSize, zoom, hoveredSlice]);

	return (
		<canvas
			ref={canvasRef}
			className={className}
			style={{ cursor: zoom.scale > 1 ? "grab" : "default" }}
		/>
	);
};