interface RomStore {
  roms: Map<string, RomData>  // Multiple ROMs can be loaded
  openEditors: Map<string, KitEditorInstance>  // Active kit editors

  loadRomFromFile: (file: File) => string  // Returns ROM ID
  loadRomFromGBS: () => string  // Fetches from emulator
  openKitEditor: (romId: string, kitIndex: number) => string  // Returns editor ID
  closeKitEditor: (editorId: string) => void
  createComparison: (leftRomId: string, rightRomId: string) => string
}