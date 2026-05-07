import * as Sentry from '@sentry/react';
import { wasmIntegration } from '@sentry/wasm';
Sentry.init({
	dsn: 'https://830f98726756b63e91d3fbc1d14488a8@o4509994869719040.ingest.de.sentry.io/4509994879418448',
	sendDefaultPii: true,
	integrations: [wasmIntegration()],
	enabled: process.env.NODE_ENV !== 'development',
});

console.log(`Environment: ${process.env.NODE_ENV}`);

import ReactDOM from 'react-dom/client';
import App from './App';
import './styles/index.css';

ReactDOM.createRoot(document.getElementById('root')!).render(<App />);
