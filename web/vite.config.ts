import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";
import tailwindcss from '@tailwindcss/vite';

export default defineConfig({
	server: {
		headers: {
			"Cross-Origin-Embedder-Policy": "require-corp",
			"Cross-Origin-Opener-Policy": "same-origin",
		},
	},
	plugins: [tailwindcss(), react()],
	resolve: {
		preserveSymlinks: true,
	},
	worker: {
		format: 'es',
	},
});
