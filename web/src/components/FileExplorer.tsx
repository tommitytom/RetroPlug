import { useEffect } from 'react'
import { useOPFSStore } from '../stores/FileSystemStore';

export function FileExplorer() {
	const {
		rootNode,
		selectedNodes,
		expandedNodes,
		loading,
		error,
		initialize,
		registerArchiveHandler,
		toggleNode,
		selectNode,
		listPath,
		readPath,
		writePath,
		copyPath,
		movePath
	} = useOPFSStore()

	useEffect(() => {
		// Initialize store and register handlers
		initialize().then(() => {
			// Register other archive handlers as needed
		})
	}, [])

	const handleOperations = async () => {
		const paths = await listPath('/');
		console.log(paths);


		//console.log('write');
		//await writePath('/test.json', JSON.stringify({ key: 'value' }));
		//console.log('writedone');
	}

	// Render your treeview component here
	return (
		<div>
			<button onClick={handleOperations}>Perform Operations</button>
		</div>
	)
}
