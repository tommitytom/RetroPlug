import { useEffect, useState } from "react";
import { useRetroPlug } from "../../contexts/RetroPlugContext";

export const RomSelectDialog: React.FC<{ savName: string; onSelect: (path: string) => void; onClose: () => void }> = ({
	savName,
	onSelect,
	onClose,
}) => {
	const { fileSystem } = useRetroPlug();
	const [romList, setRomList] = useState<string[]>([]);
	const [selectedRom, setSelectedRom] = useState<string | null>(null);

	useEffect(() => {
		fileSystem.listPath('/roms').then((res) => {
			setRomList(res.children?.map((f) => f.name) || []);
		});
	}, []);

	const handleSubmit = (e: React.FormEvent) => {
		e.preventDefault();
		if (!selectedRom) {
			onClose();
		} else {
			onSelect(selectedRom);
		}
	};

	return (
		<div>
			<form onSubmit={handleSubmit} className="space-y-6">
				<div>
					<label className="mb-3 block text-sm font-medium text-white">Choose a rom for {savName}:</label>
					<select
						name="rom"
						value={selectedRom || ''}
						onChange={(e) => setSelectedRom(e.target.value)}
						className="w-full rounded-lg border border-gray-600 bg-gray-700 px-3 py-2 text-white placeholder-gray-400 transition-colors focus:border-blue-500 focus:ring-1 focus:ring-blue-500 focus:outline-none"
					>
						<option value="" className="bg-gray-700 text-gray-400">
							Select a ROM...
						</option>
						{romList.map((rom) => (
							<option key={rom} value={rom} className="bg-gray-700 text-white">
								{rom}
							</option>
						))}
					</select>
				</div>
				<div className="flex gap-3">
					<button
						type="button"
						onClick={onClose}
						className="flex-1 rounded-lg bg-gray-700 px-4 py-2 text-gray-200 transition-colors hover:bg-gray-600"
					>
						Cancel
					</button>
					<button
						type="submit"
						disabled={!selectedRom}
						className="flex-1 rounded-lg bg-blue-600 px-4 py-2 text-white transition-colors hover:bg-blue-700 disabled:cursor-not-allowed disabled:bg-gray-600 disabled:text-gray-400"
					>
						Select
					</button>
				</div>
			</form>
		</div>
	);
};