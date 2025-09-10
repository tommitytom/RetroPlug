import { sentryVitePlugin } from '@sentry/vite-plugin';
import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';
import tailwindcss from '@tailwindcss/vite';

export default defineConfig({
	server: {
		headers: {
			'Cross-Origin-Embedder-Policy': 'require-corp',
			'Cross-Origin-Opener-Policy': 'same-origin',
		},
	},

	plugins: [
		tailwindcss(),
		react(),
		sentryVitePlugin({
			org: 'retroplug',
			project: 'retroplugweb',
		}),
	],

	resolve: {
		preserveSymlinks: true,
	},

	worker: {
		format: 'es',
	},

	build: {
		sourcemap: true,
	},
});
