import {createRequire} from 'module';
import nextCoreWebVitals from 'eslint-config-next/core-web-vitals';

const require = createRequire(import.meta.url);

function detectInternal() {
  if (process.env.NEST_INTERNAL === '1') {
    return true;
  }
  try {
    require.resolve('@nest/next-core');
    return true;
  } catch {
    return false;
  }
}

const isInternal = detectInternal();

const eslintConfig = [
  ...nextCoreWebVitals,

  {
    files: ['**/*.{js,mjs,cjs,ts,tsx}'],
    rules: {
      'no-console': 'off',
      '@next/next/no-html-link-for-pages': 'off',
      'react/no-unescaped-entities': 'off',
      'jsx-a11y/alt-text': 'off',
      // One icon set: lucide-react.
      'no-restricted-imports': [
        'error',
        {
          patterns: [
            {
              group: ['@mui/icons-material', '@mui/icons-material/*'],
              message: 'Use lucide-react icons.',
            },
          ],
        },
      ],
    },
  },

  {
    ignores: [
      '.next/**/*',
      'node_modules/**/*',
      'next-env.d.ts',
      // External builds do not have @nest/* dependencies. Internal lint must
      // still cover the fb/ adapters because next.config.ts aliases to them.
      ...(isInternal ? [] : ['**/fb/**/*']),
    ],
  },
];

export default eslintConfig;
