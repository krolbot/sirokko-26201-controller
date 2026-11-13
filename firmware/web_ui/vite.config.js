import { defineConfig } from 'vite';
import { svelte } from '@sveltejs/vite-plugin-svelte';
import { viteSingleFile } from 'vite-plugin-singlefile';
export default defineConfig({base:'./',plugins:[svelte(),viteSingleFile()],build:{assetsInlineLimit:1000000,cssCodeSplit:false,target:'es2022'}});
