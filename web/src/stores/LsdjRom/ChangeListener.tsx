import { useKitChanges, useKitListChanges } from "./hooks";

interface ChangeListenerProps {

}

export const ChangeListener: React.FC<ChangeListenerProps> = () => {
	useKitChanges(['kit1', 'kit2'], (kitKey, kit) => {
		console.log('Kit changed:', kitKey, kit);
	});

	useKitListChanges((kitId, kit) => {
		if (kit) {
			console.log('Kit added:', kit);
		} else {
			console.log('Kit removed:', kitId);
		}
	});

	return <></>;
};