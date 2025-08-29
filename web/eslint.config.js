import js from '@eslint/js';
import globals from 'globals';
import tseslint from 'typescript-eslint';

export default tseslint.config(
	{ ignores: ['dist'] },
	{
		extends: [js.configs.recommended, ...tseslint.configs.recommended],
		files: ['**/*.ts'],
		ignores: ['dist', 'node_modules'],
		languageOptions: {
			ecmaVersion: 2020,
			globals: globals.node,
		},
		rules: {
			indent: ['error', 'tab', { SwitchCase: 1 }],
			//curly: 'error'
		},
		settings: {
			'import/resolver': {
				typescript: {}, // for handling tsconfig paths
			},
		},
	}
);
