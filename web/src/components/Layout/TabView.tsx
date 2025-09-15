import type { TabItem } from './types';

interface TabViewProps {
	tabs: TabItem[];
	activeTab: string;
	onTabChange: (tabId: string) => void;
	className?: string;
}

export const TabView: React.FC<TabViewProps> = ({
	tabs,
	activeTab,
	onTabChange,
	className = '',
}) => {
	const activeTabContent = tabs.find(tab => tab.id === activeTab)?.content;

	return (
		<div className={`flex h-full flex-col ${className}`}>
			{/* Tabs */}
			<div className="flex border-b border-gray-700">
				{tabs.map((tab) => (
					<button
						key={tab.id}
						onClick={() => onTabChange(tab.id)}
						className={`px-3 py-1.5 text-sm font-medium transition-colors ${
							activeTab === tab.id
								? 'border-b-2 border-blue-400 text-blue-400'
								: 'text-gray-400 hover:text-gray-200'
						}`}
					>
						{tab.label}
					</button>
				))}
			</div>

			{/* Tab Content */}
			<div className="flex-1 overflow-auto p-3">
				{activeTabContent}
			</div>
		</div>
	);
};