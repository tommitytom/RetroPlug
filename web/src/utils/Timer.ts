export class Timer {
	private _startTime: number | null = null;

	start() {
		this._startTime = performance.now();
	}

	stop(): number {
		if (this._startTime !== null) {
			const duration = performance.now() - this._startTime;
			this._startTime = null;
			return duration;
		}
		return 0;
	}
}
