import { defineAsyncComponent } from 'vue'
import NotFound from './error.vue'
import DefaultTheme from 'vitepress/theme'

export default {
  ...DefaultTheme,
  Layout: defineAsyncComponent(() => import('./Layout.vue')),
  NotFound,
}
