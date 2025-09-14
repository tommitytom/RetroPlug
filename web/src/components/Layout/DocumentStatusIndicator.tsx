import { Save } from "lucide-react";
import { useDocument } from "../../contexts/DocumentContext";

export const DocumentStatusIndicator: React.FC = () => {
	const { currentDocument, saveDocument, isSaving, lastSaveResult } = useDocument();

	if (!currentDocument) return null;

	return (
		<div className="flex items-center gap-4 text-gray-500">
			{/* Save Status */}
			{lastSaveResult && (
				<span className={`text-xs ${lastSaveResult.success ? 'text-green-500' : 'text-red-500'}`}>
					{lastSaveResult.message}
					{lastSaveResult.savedFiles && (
						<span className="ml-1 text-gray-600">
							({lastSaveResult.savedFiles.length} file{lastSaveResult.savedFiles.length > 1 ? 's' : ''})
						</span>
					)}
				</span>
			)}

			{/* Save Button */}
			{currentDocument.isDirty && !isSaving && (
				<button
					onClick={saveDocument}
					className="flex items-center gap-1 hover:text-gray-300 transition-colors"
				>
					<Save className="w-3 h-3" />
					<span>Save</span>
				</button>
			)}

			{/* Saving Indicator */}
			{isSaving && (
				<span className="flex items-center gap-1 text-blue-400">
					<span className="w-3 h-3 border-2 border-blue-400 border-t-transparent rounded-full animate-spin" />
					<span>Saving...</span>
				</span>
			)}
		</div>
	);
};
