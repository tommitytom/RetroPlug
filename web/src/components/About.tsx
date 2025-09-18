export const About: React.FC = () => {
	return (
		<div className="h-full w-full p-4 text-white overflow-y-auto">
			<div className="space-y-6">
				{/* Header */}
				<div className="text-center border-b border-gray-700 pb-4">
					<h1 className="text-xl font-bold text-white mb-1">RetroPlug</h1>
					<p className="text-sm text-gray-300">v0.6.0</p>
					<p className="text-xs text-gray-400 mt-1">by tommitytom</p>
				</div>

				{/* GitHub Link */}
				<div className="bg-gray-900/50 rounded-md p-3 border border-gray-700">
					<h3 className="text-sm font-semibold text-blue-300 mb-2">Source Code</h3>
					<a
						href="https://github.com/tommitytom/RetroPlug/tree/ecs/web"
						target="_blank"
						rel="noopener noreferrer"
						className="text-blue-400 hover:text-blue-300 text-xs break-all transition-colors duration-200 underline decoration-dotted"
					>
						github.com/tommitytom/RetroPlug
					</a>
				</div>

				{/* Dependencies */}
				<div className="bg-gray-900/50 rounded-md p-3 border border-gray-700">
					<h3 className="text-sm font-semibold text-green-300 mb-3">Dependencies</h3>
					<div className="space-y-3 text-xs">
						<div>
							<div className="font-medium text-white mb-1">LSDj</div>
							<a
								href="https://www.littlesounddj.com/"
								target="_blank"
								rel="noopener noreferrer"
								className="text-blue-400 hover:text-blue-300 transition-colors duration-200 underline decoration-dotted break-all"
							>
								littlesounddj.com
							</a>
						</div>

						<div className="border-t border-gray-700 pt-3">
							<div className="font-medium text-white mb-1">SameBoy</div>
							<a
								href="https://sameboy.github.io/"
								target="_blank"
								rel="noopener noreferrer"
								className="text-blue-400 hover:text-blue-300 transition-colors duration-200 underline decoration-dotted break-all"
							>
								sameboy.github.io
							</a>
						</div>

						<div className="border-t border-gray-700 pt-3">
							<div className="font-medium text-white mb-1">liblsdj</div>
							<a
								href="https://github.com/stijnfrishert/liblsdj"
								target="_blank"
								rel="noopener noreferrer"
								className="text-blue-400 hover:text-blue-300 transition-colors duration-200 underline decoration-dotted break-all"
							>
								github.com/stijnfrishert/liblsdj
							</a>
						</div>

						<div className="border-t border-gray-700 pt-3">
							<div className="font-medium text-white mb-1">r8brain</div>
							<a
								href="https://github.com/avaneev/r8brain-free-src"
								target="_blank"
								rel="noopener noreferrer"
								className="text-blue-400 hover:text-blue-300 transition-colors duration-200 underline decoration-dotted break-all"
							>
								github.com/avaneev/r8brain-free-src
							</a>
						</div>
					</div>
				</div>

				{/* Support */}
				<div className="bg-gray-900/50 rounded-md p-3 border border-gray-700">
					<h3 className="text-sm font-semibold text-purple-300 mb-2">Support</h3>
					<div className="flex items-center gap-2">
						<div className="w-2 h-2 bg-purple-400 rounded-full"></div>
						<a
							href="https://discord.gg/V3GyA5dtqB"
							target="_blank"
							rel="noopener noreferrer"
							className="text-purple-400 hover:text-purple-300 text-xs transition-colors duration-200 underline decoration-dotted"
						>
							Join Discord Server
						</a>
					</div>
				</div>
			</div>
		</div>
	);
};
