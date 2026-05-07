export interface ZoomState {
	scale: number;
	offset: number;
}

export interface SliceInfo {
	index: number;
	startSample: number;
	endSample: number;
	startMarkerIndex: number | null;
	endMarkerIndex: number | null;
}
