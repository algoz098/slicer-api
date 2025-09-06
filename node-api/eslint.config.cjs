const tsParser = require('@typescript-eslint/parser')
const tsPlugin = require('@typescript-eslint/eslint-plugin')
const importPlugin = require('eslint-plugin-import')
const unusedImports = require('eslint-plugin-unused-imports')
const prettierConfig = require('eslint-config-prettier')

module.exports = [
  {
    ignores: ['lib/**', 'node_modules/**']
  },
  {
    files: ['**/*.ts', '**/*.tsx'],
    languageOptions: {
      parser: tsParser,
      parserOptions: {
        project: ['./tsconfig.json'],
        tsconfigRootDir: __dirname,
        sourceType: 'module'
      }
    },
    plugins: {
      '@typescript-eslint': tsPlugin,
      import: importPlugin,
      'unused-imports': unusedImports
    },
    rules: {
      'no-console': ['warn', { allow: ['warn', 'error'] }],
      'unused-imports/no-unused-imports': 'error',
      'import/order': [
        'warn',
        {
          groups: [
            ['builtin', 'external'],
            ['internal', 'parent', 'sibling', 'index']
          ],
          'newlines-between': 'always'
        }
      ],
      '@typescript-eslint/no-explicit-any': 'off'
    }
  },
  // turn off rules that may conflict with Prettier
  prettierConfig
]

