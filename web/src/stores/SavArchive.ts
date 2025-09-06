import type { MainModule, NativeLsdjProject, NativeLsdjSav } from '../native/RetroPlug';
import { fromArrayBuffer, toArrayBuffer } from '../utils/NativeUtil';
import type { ArchiveHandler, ArchiveInstance, FileSystemNode } from './types';

export class SavArchiveHandler implements ArchiveHandler {
	type = 'sav';
	extensions = ['.sav'];

	constructor(private _module: MainModule) {}

	canHandle(buffer: ArrayBuffer): boolean {
		try {
			// Check for ZIP signature (PK)
			const view = new DataView(buffer);
			return view.getUint16(0) === 0x504b;
		} catch {
			return false;
		}
	}

	async open(buffer: ArrayBuffer): Promise<ArchiveInstance> {
		const sav = new this._module.NativeLsdjSav(fromArrayBuffer(this._module, buffer));
		return new SavArchiveInstance(this._module, sav);
	}

	create(): ArchiveInstance {
		return new SavArchiveInstance(this._module, new this._module.NativeLsdjSav());
	}
}

class SavArchiveInstance implements ArchiveInstance {
	constructor(
		private _module: MainModule,
		private _sav: NativeLsdjSav,
	) {}

	private findProject(name: string): NativeLsdjProject | null {
		const projectCount = this._sav.projectCount;
		name = name.substring(0, name.lastIndexOf('.lsdsng'));

		let found: NativeLsdjProject | null = null;
		for (let i = 0; i < projectCount; i++) {
			const project = this._sav.getProject(i);
			if (project.isValid && project.getName() === name) {
				found = project;
				break;
			}

			project.delete();
		}

		return found;
	}

	list(): FileSystemNode[] {
		const nodes: FileSystemNode[] = [];
		const projectCount = this._sav.projectCount;

		for (let i = 0; i < projectCount; i++) {
			const project = this._sav.getProject(i);
			if (project.isValid) {
				const name = project.getName();
				const path = `${name}.lsdsng`;

				nodes.push({
					id: name,
					name: path,
					path: path,
					type: 'file',
					size: 0,
				});
			}

			project.delete();
		}

		return nodes;
	}

	read(path: string): ArrayBuffer {
		const project = this.findProject(path);
		if (!project) {
			throw new Error('Project not found');
		}

		const song = project.song;
		const buffer = project.song.getBuffer();
		const result = toArrayBuffer(buffer);

		buffer.delete();
		song.delete();
		project.delete();

		return result;
	}

	write(path: string, content: ArrayBuffer): void {
		const project = new this._module.NativeLsdjProject(fromArrayBuffer(this._module, content));
		project.setName(path.substring(0, 8));
		if (!this._sav.writeProject(project, 255)) {
			throw new Error('Failed to write project');

		}
		project.delete();
	}

	delete(path: string): void {
		const project = this.findProject(path);
		if (!project) {
			throw new Error('Project not found');
		}

		const id = project.index;
		project.delete();

		this._sav.eraseProject(id);
	}

	extract(path?: string): Map<string, ArrayBuffer> {
		const files = new Map<string, ArrayBuffer>();

		const projectCount = this._sav.projectCount;

		for (let i = 0; i < projectCount; i++) {
			const project = this._sav.getProject(i);
			if (project.isValid) {
				const name = project.getName();
				const song = project.song;
				const buffer = project.song.getBuffer();
				const result = toArrayBuffer(buffer);

				buffer.delete();
				song.delete();
				project.delete();

				files.set(`${name}.lsdsng`, result);
			}

			project.delete();
		}

		return files;
	}

	serialize(): ArrayBuffer {
		const buffer = this._sav.save();
		const arrayBuffer = toArrayBuffer(buffer);
		buffer.delete();
		return arrayBuffer;
	}

	close(): void {
		this._sav.delete();
	}
}
