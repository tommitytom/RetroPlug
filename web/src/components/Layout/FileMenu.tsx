import { useState } from "react";
import { useDocument } from "../../contexts/DocumentContext";
import { File, FolderOpen, Save, X } from "lucide-react";

export const FileMenu: React.FC = () => {
	const { currentDocument, saveDocument, isSaving } = useDocument();
	const [isOpen, setIsOpen] = useState(false);

	return (
		<div className="relative">
			<button
				onClick={() => setIsOpen(!isOpen)}
				className="px-3 py-1.5 text-sm hover:bg-gray-700 rounded transition-colors"
			>
				File
			</button>
			{isOpen && (
				<>
					<div
						className="fixed inset-0 z-40"
						onClick={() => setIsOpen(false)}
					/>
					<div className="absolute top-full left-0 mt-1 w-48 bg-gray-800 border border-gray-700 rounded-md shadow-lg z-50">
						<button className="w-full px-3 py-2 text-left text-sm hover:bg-gray-700 flex items-center gap-2 transition-colors">
							<File className="w-3 h-3" />
							New
						</button>
						<button className="w-full px-3 py-2 text-left text-sm hover:bg-gray-700 flex items-center gap-2 transition-colors">
							<FolderOpen className="w-3 h-3" />
							Open
						</button>
						<button
							onClick={async () => {
								const result = await saveDocument();
								if (result.success) {
									setIsOpen(false);
								}
							}}
							className="w-full px-3 py-2 text-left text-sm hover:bg-gray-700 flex items-center gap-2 transition-colors disabled:opacity-50"
							disabled={!currentDocument?.isDirty || isSaving}
						>
							<Save className="w-3 h-3" />
							{isSaving ? 'Saving...' : 'Save'}
						</button>
						<div className="border-t border-gray-700 my-1" />
						<button className="w-full px-3 py-2 text-left text-sm hover:bg-gray-700 flex items-center gap-2 transition-colors">
							<X className="w-3 h-3" />
							Exit
						</button>
					</div>
				</>
			)}
		</div>
	);
};
