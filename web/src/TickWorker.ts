let interval: NodeJS.Timeout;

self.addEventListener('message', (e) => {
	switch (e.data.type) {
		case 'start':
			interval = setInterval(() => self.postMessage('tick'), e.data.interval);
			break;
		case 'stop':
			clearInterval(interval);
			break;
	};
}, false);
