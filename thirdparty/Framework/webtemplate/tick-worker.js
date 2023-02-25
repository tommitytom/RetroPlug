var interval;

self.addEventListener('message', (e) => {
	switch (e.data) {
		case 'start':
			interval = setInterval(() => {
				self.postMessage('tick');
			}, 1000/60);

			break;
		case 'stop':
			clearInterval(interval);
			break;
	};
}, false);
