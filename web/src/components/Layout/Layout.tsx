import { Settings } from 'lucide-react';
import { useState } from 'react';

import { DocumentProvider } from '../../contexts/DocumentProvider';
import { DocumentDisplay } from './DocumentDisplay';
import { DocumentStatusIndicator } from './DocumentStatusIndicator';
import { FileMenu } from './FileMenu';
import { ResizablePanel } from './ResizablePanel';
import { FileTreePanel } from '../../panels/FileTreePanel';
import { LsdjRomMemoryEditor } from '../Lsdj/LsdjRomMemoryEditor';
import { useRetroPlug } from '../../contexts/RetroPlugContext';
import { ProjectExplorer } from '../ProjectExplorer';

export const Layout: React.FC = () => {
	const { audioContext } = useRetroPlug();
	const [leftPanelCollapsed, setLeftPanelCollapsed] = useState(false);
	const [rightPanelCollapsed, setRightPanelCollapsed] = useState(false);
	const [leftPanelWidth, setLeftPanelWidth] = useState(350);
	const [rightPanelWidth, setRightPanelWidth] = useState(650);
	const [activeLeftTab, setActiveLeftTab] = useState('explorer');
	const [activeRightTab, setActiveRightTab] = useState('kits');

	return (
		<DocumentProvider>
			<div className="flex h-screen flex-col bg-gray-950 text-gray-100">
				{/* Main Content Area */}
				<div className="flex flex-1 overflow-hidden">
					{/* Left Panel */}
					<ResizablePanel
						side="left"
						isCollapsed={leftPanelCollapsed}
						onToggle={() => setLeftPanelCollapsed(!leftPanelCollapsed)}
						width={leftPanelWidth}
						onWidthChange={setLeftPanelWidth}
						minWidth={180}
						maxWidth={400}
					>
						<div className="flex h-full flex-col">
							{/* Tabs */}
							<div className="flex border-b border-gray-700">
								{['Explorer'].map((tab) => (
									<button
										key={tab}
										onClick={() => setActiveLeftTab(tab.toLowerCase())}
										className={`px-3 py-2 text-xs font-medium transition-colors ${
											activeLeftTab === tab.toLowerCase()
												? 'border-b-2 border-blue-400 text-blue-400'
												: 'text-gray-400 hover:text-gray-200'
										}`}
									>
										{tab}
									</button>
								))}
							</div>

							{/* Tab Content */}
							<div className="flex-1 overflow-auto p-3">
								<ProjectExplorer />
								{/*<FileTreePanel />*/}
							</div>
						</div>
					</ResizablePanel>

					{/* Center Document Area */}
					<div className="flex-1 overflow-hidden bg-gray-900">
						<DocumentDisplay />
					</div>

					{/* Right Panel */}
					<ResizablePanel
						side="right"
						isCollapsed={rightPanelCollapsed}
						onToggle={() => setRightPanelCollapsed(!rightPanelCollapsed)}
						width={rightPanelWidth}
						onWidthChange={setRightPanelWidth}
						minWidth={200}
						maxWidth={1280}
					>
						<div className="flex h-full flex-col">
							{/* Tabs */}
							<div className="flex border-b border-gray-700">
								{['Kits', 'Songs'].map((tab) => (
									<button
										key={tab}
										onClick={() => setActiveRightTab(tab.toLowerCase())}
										className={`px-3 py-2 text-xs font-medium transition-colors ${
											activeRightTab === tab.toLowerCase()
												? 'border-b-2 border-blue-400 text-blue-400'
												: 'text-gray-400 hover:text-gray-200'
										}`}
									>
										{tab}
									</button>
								))}
							</div>

							{/* Tab Content */}
							<div className="flex-1 overflow-auto p-3">
								<LsdjRomMemoryEditor />
							</div>
						</div>
					</ResizablePanel>
				</div>

				{/* Status Bar */}
				<div className="flex h-6 items-center border-t border-gray-700 bg-gray-900 px-2 text-xs">
					<span className="text-gray-500">RetroPlug 0.5.0 • SameBoy 0.15.5 • {audioContext?.state === 'running' ? `${audioContext.sampleRate} Hz` : 'Audio disabled'}</span>
					<div className="flex-1" />
					<DocumentStatusIndicator />
				</div>
			</div>
		</DocumentProvider>
	);
};
