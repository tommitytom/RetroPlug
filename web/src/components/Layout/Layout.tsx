import { useState } from 'react';

import { DocumentProvider } from '../../contexts/DocumentProvider';
import { useRetroPlug } from '../../contexts/RetroPlugContext';
import { LsdjRomMemoryEditor } from '../Lsdj/LsdjRomMemoryEditor';
import { LsdjSavMemoryEditor } from '../Lsdj/LsdjSavMemoryEditor';
import { ProjectExplorer } from '../ProjectExplorer';
import { ProjectSettings } from '../ProjectSettings';
import { DocumentDisplay } from './DocumentDisplay';
import { DocumentStatusIndicator } from './DocumentStatusIndicator';
import { ResizablePanel } from './ResizablePanel';
import { TabView } from './TabView';
import type { TabItem } from './types';
import { About } from '../About';

export const Layout: React.FC = () => {
	const { audioContext, module } = useRetroPlug();
	const [leftPanelCollapsed, setLeftPanelCollapsed] = useState(false);
	const [rightPanelCollapsed, setRightPanelCollapsed] = useState(false);
	const [leftPanelWidth, setLeftPanelWidth] = useState(350);
	const [rightPanelWidth, setRightPanelWidth] = useState(650);
	const [activeLeftTab, setActiveLeftTab] = useState('explorer');
	const [activeRightTab, setActiveRightTab] = useState('kits');

	const leftTabs: TabItem[] = [
		{
			id: 'explorer',
			label: 'Explorer',
			content: <ProjectExplorer />
		},
		{
			id: 'settings',
			label: 'Settings',
			content: <ProjectSettings />
		},
		{
			id: 'about',
			label: 'About',
			content: <About />
		}
	];

	const rightTabs: TabItem[] = [
		{
			id: 'kits',
			label: 'Kits',
			content: <LsdjRomMemoryEditor />
		},
		{
			id: 'songs',
			label: 'Songs',
			content: <LsdjSavMemoryEditor /> // You can replace this with a different component for Songs
		}
	];

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
						<TabView
							tabs={leftTabs}
							activeTab={activeLeftTab}
							onTabChange={setActiveLeftTab}
						/>
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
						<TabView
							tabs={rightTabs}
							activeTab={activeRightTab}
							onTabChange={setActiveRightTab}
						/>
					</ResizablePanel>
				</div>

				{/* Status Bar */}
				<div className="flex h-6 items-center border-t border-gray-700 bg-gray-900 px-2 text-xs">
					<span className="text-gray-500">RetroPlug 0.5.0 • SameBoy v1.0.0 • {audioContext?.state === 'running' ? `${audioContext.sampleRate} Hz` : 'Audio disabled'}</span>
					<div className="flex-1" />
					<DocumentStatusIndicator />
				</div>
			</div>
		</DocumentProvider>
	);
};
