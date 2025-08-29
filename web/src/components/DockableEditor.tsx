/* eslint-disable react/forbid-dom-props */
import React, { useState, useRef, useEffect, useCallback } from 'react';
import { X, Maximize2, Minimize2, Move, GripVertical, Plus } from 'lucide-react';
import '../styles/DockableEditor.css';
//import '../styles/globals.css';

export interface Panel {
	id: string;
	title: string;
	content: React.ReactNode;
	minWidth?: number;
	minHeight?: number;
}

interface DockZone {
	id: string;
	panels: string[];
	activePanel: string;
	size: number;
}

interface FloatingWindow {
	id: string;
	panelId: string;
	x: number;
	y: number;
	width: number;
	height: number;
}

interface DockLayout {
	left: DockZone | null;
	center: DockZone;
	right: DockZone | null;
	bottom: DockZone | null;
}

interface DropZoneIndicator {
	zone: string;
	active: boolean;
}

export interface DockableEditorProps {
	panels: Panel[];
	initialLayout?: Partial<DockLayout>;
	onLayoutChange?: (layout: DockLayout) => void;
}

export const DockableEditor: React.FC<DockableEditorProps> = ({
	panels,
	initialLayout = {},
	onLayoutChange
}) => {
	const defaultLayout: DockLayout = {
		left: null,
		center: { id: 'center', panels: [], activePanel: '', size: 0 },
		right: null,
		bottom: null,
		...initialLayout
	};

	const [layout, setLayout] = useState<DockLayout>(defaultLayout);

	const [floatingWindows, setFloatingWindows] = useState<FloatingWindow[]>([]);
	const [draggedPanel, setDraggedPanel] = useState<string | null>(null);
	const [draggedFloatingWindow, setDraggedFloatingWindow] = useState<string | null>(null);
	const [resizing, setResizing] = useState<{ zone: string; startX: number; startY: number; startSize: number } | null>(null);
	const [windowDrag, setWindowDrag] = useState<{ id: string; startX: number; startY: number; offsetX: number; offsetY: number } | null>(null);
	const [dropZoneIndicators, setDropZoneIndicators] = useState<DropZoneIndicator[]>([
		{ zone: 'left', active: false },
		{ zone: 'center', active: false },
		{ zone: 'right', active: false },
		{ zone: 'bottom', active: false },
	]);
	const containerRef = useRef<HTMLDivElement>(null);
	const dropZoneRefs = useRef<{ [key: string]: HTMLDivElement | null }>({});
	const resizingRef = useRef<{ zone: string; startX: number; startY: number; startSize: number; currentSize: number } | null>(null);
	const animationFrameRef = useRef<number | null>(null);

	const getPanelById = (id: string) => panels.find(p => p.id === id);

	// Call onLayoutChange when layout changes
	useEffect(() => {
		if (onLayoutChange) {
			onLayoutChange(layout);
		}
	}, [layout, onLayoutChange]);

	const handleTabDragStart = (e: React.DragEvent, panelId: string, fromZone: string) => {
		e.dataTransfer.effectAllowed = 'move';
		setDraggedPanel(panelId);
	};

	const handleDragOver = (e: React.DragEvent) => {
		e.preventDefault();
		e.dataTransfer.dropEffect = 'move';
	};

	const handleDrop = (e: React.DragEvent, targetZone: string) => {
		e.preventDefault();

		// Handle tab dragging
		if (draggedPanel) {
			setLayout(prev => {
				const newLayout = { ...prev };

				// Remove panel from all zones
				Object.keys(newLayout).forEach(key => {
					const zone = newLayout[key as keyof DockLayout];
					if (zone) {
						zone.panels = zone.panels.filter(p => p !== draggedPanel);
						if (zone.activePanel === draggedPanel && zone.panels.length > 0) {
							zone.activePanel = zone.panels[0];
						}
					}
				});

				// Remove from floating windows
				setFloatingWindows(prev => prev.filter(w => w.panelId !== draggedPanel));

				// Add to target zone
				const target = newLayout[targetZone as keyof DockLayout];
				if (target) {
					target.panels.push(draggedPanel);
					target.activePanel = draggedPanel;
				}

				return newLayout;
			});

			setDraggedPanel(null);
		}

		// Reset drop zone indicators
		setDropZoneIndicators(prev => prev.map(dz => ({ ...dz, active: false })));
	};

	const handleUndock = (panelId: string, fromZone: string) => {
		const rect = containerRef.current?.getBoundingClientRect();
		if (!rect) return;

		setLayout(prev => {
			const newLayout = { ...prev };
			const zone = newLayout[fromZone as keyof DockLayout];
			if (zone) {
				zone.panels = zone.panels.filter(p => p !== panelId);
				if (zone.activePanel === panelId && zone.panels.length > 0) {
					zone.activePanel = zone.panels[0];
				}
			}
			return newLayout;
		});

		setFloatingWindows(prev => [...prev, {
			id: `floating-${Date.now()}`,
			panelId,
			x: rect.width / 2 - 200,
			y: rect.height / 2 - 150,
			width: 400,
			height: 300,
		}]);
	};

	const handleCloseFloating = (windowId: string) => {
		setFloatingWindows(prev => prev.filter(w => w.id !== windowId));
	};

	const handleResizeStart = (e: React.MouseEvent, zone: string) => {
	e.preventDefault();
	e.stopPropagation();
	const zoneData = layout[zone as keyof DockLayout];
	if (zoneData) {
		resizingRef.current = {
			zone,
			startX: e.clientX,
			startY: e.clientY,
			startSize: zoneData.size,
			currentSize: zoneData.size,
		};
		setResizing({
			zone,
			startX: e.clientX,
			startY: e.clientY,
			startSize: zoneData.size,
		});
	}
};

	const handleWindowDragStart = (e: React.MouseEvent, windowId: string) => {
		const window = floatingWindows.find(w => w.id === windowId);
		if (!window) return;

		setDraggedFloatingWindow(window.panelId);
		setWindowDrag({
			id: windowId,
			startX: window.x,
			startY: window.y,
			offsetX: e.clientX - window.x,
			offsetY: e.clientY - window.y,
		});
	};

	const checkDropZones = useCallback((x: number, y: number) => {
		if (!draggedFloatingWindow) return;

		setDropZoneIndicators(prev => prev.map(indicator => {
			const ref = dropZoneRefs.current[indicator.zone];
			if (ref) {
				const rect = ref.getBoundingClientRect();
				const isOver = x >= rect.left && x <= rect.right && y >= rect.top && y <= rect.bottom;
				return { ...indicator, active: isOver };
			}
			return { ...indicator, active: false };
		}));
	}, [draggedFloatingWindow]);

	useEffect(() => {
		if (!resizing && !windowDrag) return;

		const handleMouseMove = (e: MouseEvent) => {
			if (resizingRef.current) {
				e.preventDefault();
				// Use clientY for bottom panel, clientX for left/right panels
				const delta = resizingRef.current.zone === 'bottom'
					? e.clientY - resizingRef.current.startY
					: e.clientX - resizingRef.current.startX;

				const newSize = resizingRef.current.zone === 'right'
					? Math.max(200, resizingRef.current.startSize - delta)
					: resizingRef.current.zone === 'bottom'
					? Math.max(100, resizingRef.current.startSize - delta)  // Invert for bottom
					: Math.max(200, resizingRef.current.startSize + delta);

				resizingRef.current.currentSize = newSize;

				// Use requestAnimationFrame for smooth updates
				if (animationFrameRef.current) {
					cancelAnimationFrame(animationFrameRef.current);
				}

				animationFrameRef.current = requestAnimationFrame(() => {
					const currentResizing = resizingRef.current;
					if (currentResizing) {
						setLayout(prev => {
							const zone = prev[currentResizing.zone as keyof DockLayout];
							if (!zone) return prev;
							if (zone.size === newSize) return prev; // Skip if no change

							return {
								...prev,
								[currentResizing.zone]: {
									...zone,
									size: newSize,
								},
							};
						});
					}
				});
			}

			if (windowDrag) {
				setFloatingWindows(prev => prev.map(w =>
					w.id === windowDrag.id
						? { ...w, x: e.clientX - windowDrag.offsetX, y: e.clientY - windowDrag.offsetY }
						: w
				));

				// Check for drop zones while dragging
				checkDropZones(e.clientX, e.clientY);
			}
		};

		const handleMouseUp = (e: MouseEvent) => {
			if (animationFrameRef.current) {
				cancelAnimationFrame(animationFrameRef.current);
				animationFrameRef.current = null;
			}

			if (windowDrag && draggedFloatingWindow) {
				// Check if we're over a drop zone by checking refs directly
				let activeZone: string | null = null;
				for (const [zone, ref] of Object.entries(dropZoneRefs.current)) {
					if (ref) {
						const rect = ref.getBoundingClientRect();
						if (e.clientX >= rect.left && e.clientX <= rect.right &&
							e.clientY >= rect.top && e.clientY <= rect.bottom) {
							activeZone = zone;
							break;
						}
					}
				}

				if (activeZone) {
					// Redock the window
					setLayout(prev => {
						const newLayout = { ...prev };

						// First, remove the panel from all zones to prevent duplicates
						Object.keys(newLayout).forEach(key => {
							const zone = newLayout[key as keyof DockLayout];
							if (zone) {
								zone.panels = zone.panels.filter(p => p !== draggedFloatingWindow);
								if (zone.activePanel === draggedFloatingWindow && zone.panels.length > 0) {
									zone.activePanel = zone.panels[0];
								}
							}
						});

						// Then add to target zone
						const target = newLayout[activeZone as keyof DockLayout];
						if (target) {
							target.panels.push(draggedFloatingWindow);
							target.activePanel = draggedFloatingWindow;
						}
						return newLayout;
					});

					// Remove the floating window
					setFloatingWindows(prev => prev.filter(w => w.id !== windowDrag.id));
				}
			}

			// Final update with the current size
			if (resizingRef.current) {
				const finalSize = resizingRef.current.currentSize;
				const zone = resizingRef.current.zone;
				setLayout(prev => ({
					...prev,
					[zone]: {
						...prev[zone as keyof DockLayout]!,
						size: finalSize,
					},
				}));
			}

			resizingRef.current = null;
			setResizing(null);
			setWindowDrag(null);
			setDraggedFloatingWindow(null);
			setDropZoneIndicators(prev => prev.map(dz => ({ ...dz, active: false })));
		};

		document.addEventListener('mousemove', handleMouseMove);
		document.addEventListener('mouseup', handleMouseUp);

		return () => {
			document.removeEventListener('mousemove', handleMouseMove);
			document.removeEventListener('mouseup', handleMouseUp);
			if (animationFrameRef.current) {
				cancelAnimationFrame(animationFrameRef.current);
			}
		};
	}, [resizing, windowDrag, draggedFloatingWindow]);

	const renderDockZone = (zone: DockZone | null, position: string) => {
		const isActive = dropZoneIndicators.find(dz => dz.zone === position)?.active;

		// Show empty drop zone if no panels but dragging
		if ((!zone || zone.panels.length === 0) && !draggedFloatingWindow) return null;

		const isHorizontal = position === 'bottom';
		const hasContent = zone && zone.panels.length > 0;
		const sizeStyle = isHorizontal
			? { height: hasContent ? `${zone.size}px` : '200px' }
			: position === 'center'
			? { flex: 1 }
			: { width: hasContent ? `${zone.size}px` : '250px' };

		return (
			<div
				ref={el => { dropZoneRefs.current[position] = el; }}
				className={`dock-zone-${position} relative flex flex-col bg-gray-900 border-gray-700 ${
					position === 'left' ? 'border-r' :
					position === 'right' ? 'border-l' :
					position === 'bottom' ? 'border-t' : ''
				} ${isActive ? 'ring-2 ring-blue-500 bg-blue-500/10' : ''}`}
				style={sizeStyle}
				onDragOver={handleDragOver}
				onDrop={(e) => handleDrop(e, position)}
			>
				{hasContent ? (
					<>
						{/* Tabs */}
						<div className="flex bg-gray-800 border-b border-gray-700 min-h-[32px]">
							{zone.panels.map(panelId => {
								const panel = getPanelById(panelId);
								if (!panel) return null;

								return (
									<div
										key={panelId}
										draggable
										onDragStart={(e) => handleTabDragStart(e, panelId, position)}
										className={`px-3 py-1 cursor-move select-none text-sm flex items-center gap-2 border-r border-gray-700 ${
											zone.activePanel === panelId
												? 'bg-gray-700 text-white'
												: 'bg-gray-800 text-gray-400 hover:bg-gray-750'
										}`}
										onClick={() => setLayout(prev => ({
											...prev,
											[position]: { ...zone, activePanel: panelId }
										}))}
									>
										<Move className="w-3 h-3" />
										{panel.title}
										<button
											onClick={(e) => {
												e.stopPropagation();
												handleUndock(panelId, position);
											}}
											className="ml-1 hover:text-white"
											title="Undock panel"
											aria-label="Undock panel"
										>
											<Maximize2 className="w-3 h-3" />
										</button>
									</div>
								);
							})}
						</div>

						{/* Content */}
						<div className="flex-1 overflow-auto">
							{zone.activePanel && (() => {
								const panel = getPanelById(zone.activePanel);
								if (!panel) return null;

								return (
									<div key={zone.activePanel} className="h-full">
										{panel.content}
									</div>
								);
							})()}
						</div>

						{/* Resize handle */}
						{position !== 'center' && (
							<div
								className={`resize-handle ${
									position === 'left' ? 'resize-handle-left' :
									position === 'right' ? 'resize-handle-right' :
									'resize-handle-bottom'
								}`}
								onMouseDown={(e) => handleResizeStart(e, position)}
							/>
						)}
					</>
				) : (
					/* Empty drop zone indicator when dragging */
					draggedFloatingWindow && (
						<div className="flex-1 flex items-center justify-center">
							<div className="text-center text-gray-500">
								<Plus className="w-12 h-12 mx-auto mb-2 opacity-50" />
								<p className="text-sm">Drop here to dock</p>
							</div>
						</div>
					)
				)}
			</div>
		);
	};

	return (
		<div ref={containerRef} className={`relative w-full h-screen bg-gray-950 text-gray-100 overflow-hidden ${resizing ? 'select-none' : ''}`}>
			<div className="flex h-full">
				{/* Left dock */}
				{(layout.left?.panels.length || draggedFloatingWindow) && renderDockZone(layout.left, 'left')}

				{/* Center and bottom */}
				<div className="flex-1 flex flex-col">
					{/* Center dock */}
					{renderDockZone(layout.center, 'center')}

					{/* Bottom dock */}
					{(layout.bottom?.panels.length || draggedFloatingWindow) && renderDockZone(layout.bottom, 'bottom')}
				</div>

				{/* Right dock */}
				{(layout.right?.panels.length || draggedFloatingWindow) && renderDockZone(layout.right, 'right')}
			</div>

			{/* Floating windows */}
			{floatingWindows.map(window => {
				const panel = getPanelById(window.panelId);
				if (!panel) return null;

				const isDragging = windowDrag?.id === window.id;

				// Dynamic positioning styles - necessary for floating windows
				const windowStyles: React.CSSProperties = {
					left: `${window.x}px`,
					top: `${window.y}px`,
					width: `${window.width}px`,
					height: `${window.height}px`,
					cursor: isDragging ? 'move' : 'default',
				};

				return (
					<div
						key={window.id}
						className={`floating-window ${isDragging ? 'dragging' : 'not-dragging'}`}
						style={windowStyles}
					>
						<div
							className="bg-gray-800 px-3 py-2 flex items-center justify-between cursor-move select-none"
							onMouseDown={(e) => handleWindowDragStart(e, window.id)}
						>
							<div className="flex items-center gap-2">
								<Move className="w-3 h-3 text-gray-400" />
								<span className="text-sm font-medium">{panel.title}</span>
							</div>
							<button
								onClick={() => handleCloseFloating(window.id)}
								className="text-gray-400 hover:text-white"
								title="Close floating window"
								aria-label="Close floating window"
							>
								<X className="w-4 h-4" />
							</button>
						</div>
						<div className="p-4 h-[calc(100%-40px)] overflow-auto">
							{panel.content}
						</div>
					</div>
				);
			})}

			{/* Overlay hint when dragging */}
			{draggedFloatingWindow && windowDrag && (
				<div className="absolute top-4 left-1/2 transform -translate-x-1/2 bg-blue-600 text-white px-4 py-2 rounded-lg shadow-lg pointer-events-none z-50">
					Drag to a dock zone to redock this panel
				</div>
			)}
		</div>
	);
};
