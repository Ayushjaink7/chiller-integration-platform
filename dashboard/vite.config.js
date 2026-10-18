import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';

// During development (npm run dev) API calls are forwarded to the C++ server.
// In production the C++ server serves the built files from dist/ itself.
export default defineConfig({
  plugins: [react()],
  server: {
    port: 5173,
    proxy: {
      '/api': 'http://127.0.0.1:8080',
    },
  },
  build: {
    outDir: 'dist',
    emptyOutDir: true,
    chunkSizeWarningLimit: 800,
  },
});
