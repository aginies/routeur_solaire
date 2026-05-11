import { defineAsyncComponent } from 'vue'

export default {
  Layout: defineAsyncComponent(() => import('./Layout.vue')),
}
