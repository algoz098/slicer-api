module.exports = {
  root: true,
  env: { node: true, es2021: true },
  parser: '@typescript-eslint/parser',
  parserOptions: { project: ['./tsconfig.json'], tsconfigRootDir: __dirname },
  plugins: ['@typescript-eslint', 'import', 'unused-imports'],
  extends: [
    'eslint:recommended',
    'plugin:@typescript-eslint/recommended',
    'plugin:import/recommended',
    'plugin:import/typescript',
    'prettier'
  ],
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
  },
  ignorePatterns: ['lib/', 'node_modules/']
}

