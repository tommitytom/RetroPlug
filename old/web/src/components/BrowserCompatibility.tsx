import React from 'react';

interface BrowserCompatibilityProps {
	children: React.ReactNode;
}

export function BrowserCompatibility({ children }: BrowserCompatibilityProps) {
	const [isSupported, setIsSupported] = React.useState<boolean | null>(null);

	React.useEffect(() => {
		checkBrowserSupport().then(setIsSupported);
	}, []);

	if (isSupported === null) {
		// Still checking compatibility
		return (
			<div className="min-h-screen bg-gray-900 flex items-center justify-center">
				<div className="text-white text-lg">Checking browser compatibility...</div>
			</div>
		);
	}

	if (!isSupported) {
		return <UnsupportedBrowserMessage />;
	}

	return <>{children}</>;
}

function UnsupportedBrowserMessage() {
	const isChrome = isChromeBased();

	return (
		<div className="min-h-screen bg-gray-900 text-white flex items-center justify-center p-4">
			<div className="max-w-2xl mx-auto text-center">
				{isChrome ? <ChromeJSPIMessage /> : <UnsupportedBrowserTypeMessage />}

				<div className="text-gray-400 mt-8">
					<p className="mb-2">Current browser: {getBrowserInfo()}</p>
					<p>JSPI Support: {hasJSPISupport() ? '✅ Detected' : '❌ Not detected'}</p>
				</div>
			</div>
		</div>
	);
}

function ChromeJSPIMessage() {
	return (
		<>
			<div className="mb-8">
				<h1 className="text-4xl font-bold text-yellow-400 mb-4">JSPI Not Enabled</h1>
				<div className="text-lg text-gray-300 mb-6">
					This application requires JavaScript Promise Integration (JSPI) to be enabled in your Chrome-based browser.
				</div>
			</div>

			<div className="bg-gray-800 rounded-lg p-6 mb-6 text-left">
				<h2 className="text-xl font-semibold text-blue-400 mb-4">To enable JSPI:</h2>
				<ol className="list-decimal list-inside space-y-3 text-gray-300">
					<li>
						Open a new tab and go to:
						<div className="mt-2 p-3 bg-gray-900 rounded text-sm font-mono">
							chrome://flags/#enable-experimental-webassembly-jspi
						</div>
					</li>
					<li>Set the flag to "Enabled"</li>
					<li>Restart your browser</li>
					<li>Return to this page</li>
				</ol>
			</div>
		</>
	);
}

function UnsupportedBrowserTypeMessage() {
	return (
		<>
			<div className="mb-8">
				<h1 className="text-4xl font-bold text-red-400 mb-4">Unsupported Browser</h1>
				<div className="text-lg text-gray-300 mb-6">
					This application requires a Chrome-based browser to run.
				</div>
			</div>

			<div className="bg-gray-800 rounded-lg p-6 mb-6 text-left">
				<h2 className="text-xl font-semibold text-green-400 mb-4">Supported browsers:</h2>
				<ul className="list-disc list-inside space-y-2 text-gray-300">
					<li>Google Chrome</li>
					<li>Microsoft Edge</li>
					<li>Brave Browser</li>
					<li>Opera</li>
					<li>Other Chromium-based browsers</li>
				</ul>
			</div>

			<div className="bg-red-900 border border-red-600 rounded-lg p-4 mb-6">
				<h3 className="font-semibold text-red-300 mb-2">Why Chrome-based only?</h3>
				<p className="text-red-100 text-sm">
					This application uses WebAssembly with JavaScript Promise Integration (JSPI). JSPI is supported in Firefox, but it
					tends to cause this application to lock up or crash, so it is not recommended. Safari does not support JSPI at all.
				</p>
			</div>
		</>
	);
}

async function checkBrowserSupport(): Promise<boolean> {
	// Check if we're in a Chrome-based browser
	if (!isChromeBased()) {
		return false;
	}

	// Check if JSPI is supported
	return hasJSPISupport();
}

function isChromeBased(): boolean {
	const userAgent = navigator.userAgent;

	// Chrome, Edge, Brave, Opera, and other Chromium-based browsers
	const isChrome = /Chrome\//.test(userAgent);
	const isEdge = /Edg\//.test(userAgent);

	// Firefox and Safari don't support JSPI yet
	const isFirefox = /Firefox\//.test(userAgent);
	const isSafari = /Safari\//.test(userAgent) && !/Chrome\//.test(userAgent);

	return (isChrome || isEdge) && !isFirefox && !isSafari;
}

function hasJSPISupport(): boolean {
	try {
		// Check if WebAssembly.Function exists (more reliable JSPI indicator in newer Chrome)
		if (typeof WebAssembly !== 'undefined' && typeof (WebAssembly as any).Function === 'function') {
			return true;
		}

		// Alternative check for older Chrome versions: WebAssembly.promising
		if (typeof WebAssembly !== 'undefined' && 'promising' in WebAssembly) {
			return true;
		}

		return false;
	} catch (error) {
		return false;
	}
}

function getBrowserInfo(): string {
	const userAgent = navigator.userAgent;

	if (/Edg\//.test(userAgent)) {
		return 'Microsoft Edge';
	} else if (/Chrome\//.test(userAgent) && /Brave\//.test(userAgent)) {
		return 'Brave';
	} else if (/OPR\//.test(userAgent) || /Opera\//.test(userAgent)) {
		return 'Opera';
	} else if (/Chrome\//.test(userAgent)) {
		return 'Google Chrome';
	} else if (/Firefox\//.test(userAgent)) {
		return 'Mozilla Firefox';
	} else if (/Safari\//.test(userAgent)) {
		return 'Safari';
	} else {
		return 'Unknown browser';
	}
}