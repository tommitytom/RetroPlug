export function createLoaderProject(entries: Record<string, string>): any {
	// Transform entries from Record<string, string> to the required format
	const formattedEntries: Record<string, { path: string }> = {};
	for (const [key, path] of Object.entries(entries)) {
		formattedEntries[key] = { path };
	}

	return {
		retroPlugVersion: '0.6.0',
		projectVersion: '2.0.0',
		systems: [
			{
				components: [
					{
						name: 'rp::SystemLoadComponent',
						data: {
							entries: formattedEntries,
						},
					},
				],
			},
		],
	};
}
