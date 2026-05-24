<script setup lang="ts">
import { withBase } from 'vitepress'
import { ref } from 'vue'

const searchQuery = ref('')
const searchResults = ref<any[]>([])
const showResults = ref(false)

const quickLinks = [
  { text: 'Accueil', link: '/' },
  { text: 'Guide de démarrage', link: '/guide/quick-start' },
  { text: 'Installation', link: '/guide/installation' },
  { text: 'Configuration', link: '/guide/configuration' },
  { text: 'Dépannage', link: '/guide/troubleshooting' },
]

function handleSearch() {
  if (searchQuery.value.trim().length < 2) {
    showResults.value = false
    return
  }
  // Use VitePress's built-in search
  const event = new CustomEvent('vitepress/search', {
    detail: { query: searchQuery.value, open: true },
  })
  document.dispatchEvent(event)
  showResults.value = false
}
</script>

<template>
  <div class="custom-404">
    <!-- Animated sun icon -->
    <svg class="sun-icon" viewBox="0 0 120 120" xmlns="http://www.w3.org/2000/svg">
      <defs>
        <radialGradient id="sunGrad" cx="50%" cy="50%" r="50%">
          <stop offset="0%" stop-color="#f0c040"/>
          <stop offset="70%" stop-color="#d4a800"/>
          <stop offset="100%" stop-color="#b39200"/>
        </radialGradient>
      </defs>
      <!-- Sun body -->
      <circle cx="60" cy="60" r="24" fill="url(#sunGrad)"/>
      <!-- Rays -->
      <g stroke="#f0c040" stroke-width="3" stroke-linecap="round">
        <line x1="60" y1="8" x2="60" y2="22"/>
        <line x1="60" y1="98" x2="60" y2="112"/>
        <line x1="8" y1="60" x2="22" y2="60"/>
        <line x1="98" y1="60" x2="112" y2="60"/>
        <line x1="23.5" y1="23.5" x2="33.4" y2="33.4"/>
        <line x1="86.6" y1="23.5" x2="96.5" y2="33.4"/>
        <line x1="23.5" y1="96.5" x2="33.4" y2="86.6"/>
        <line x1="86.6" y1="96.5" x2="96.5" y2="86.6"/>
      </g>
    </svg>

    <h1>404</h1>
    <p>Pas grand chose ici… Le soleil est peut-être passé par là sans laisser de trace.</p>

    <!-- Quick links -->
    <div class="quick-links">
      <a v-for="link in quickLinks" :key="link.link" :href="withBase(link.link)" class="quick-link">
        {{ link.text }}
      </a>
    </div>

    <!-- Search box -->
    <div class="search-box">
      <input
        v-model="searchQuery"
        type="text"
        placeholder="Rechercher dans la documentation…"
        @input="handleSearch"
        class="search-input"
      />
    </div>

    <a :href="withBase('/')" class="back-link">
      ← Retour à l'accueil
    </a>
  </div>
</template>

<style scoped>
.custom-404 {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  min-height: 60vh;
  text-align: center;
  padding: 40px 20px;
}

.sun-icon {
  width: 120px;
  height: 120px;
  margin-bottom: 32px;
  animation: sunFloat 6s ease-in-out infinite;
}

@keyframes sunFloat {
  0%, 100% { transform: translateY(0); }
  50% { transform: translateY(-10px); }
}

h1 {
  font-size: 72px;
  color: #f0c040;
  margin: 0 0 8px;
}

p {
  font-size: 16px;
  opacity: 0.5;
  margin-bottom: 32px;
}

.quick-links {
  display: flex;
  flex-wrap: wrap;
  gap: 8px;
  justify-content: center;
  margin-bottom: 24px;
  max-width: 500px;
}

.quick-link {
  display: inline-block;
  padding: 6px 14px;
  background: rgba(240, 192, 64, 0.06);
  border: 1px solid rgba(240, 192, 64, 0.15);
  border-radius: 6px;
  color: #a89b70;
  font-size: 13px;
  text-decoration: none;
  transition: all 0.2s ease;
}

.quick-link:hover {
  background: rgba(240, 192, 64, 0.12);
  color: #f0c040;
  border-color: rgba(240, 192, 64, 0.3);
}

.search-box {
  margin-bottom: 32px;
  width: 100%;
  max-width: 400px;
}

.search-input {
  width: 100%;
  padding: 10px 16px;
  background: rgba(240, 192, 64, 0.04);
  border: 1px solid rgba(240, 192, 64, 0.2);
  border-radius: 8px;
  color: #c9d1d9;
  font-size: 14px;
  outline: none;
  transition: all 0.2s ease;
}

.search-input::placeholder {
  color: rgba(201, 209, 217, 0.4);
}

.search-input:focus {
  border-color: rgba(240, 192, 64, 0.5);
  box-shadow: 0 0 0 3px rgba(240, 192, 64, 0.1);
}

.back-link {
  display: inline-flex;
  align-items: center;
  gap: 8px;
  padding: 10px 24px;
  background: linear-gradient(135deg, rgba(240, 192, 64, 0.1), rgba(240, 192, 64, 0.05));
  border: 1px solid rgba(240, 192, 64, 0.3);
  border-radius: 8px;
  color: #f0c040;
  font-size: 14px;
  transition: all 0.3s ease;
}

.back-link:hover {
  background: linear-gradient(135deg, rgba(240, 192, 64, 0.2), rgba(240, 192, 64, 0.1));
  transform: translateY(-1px);
  box-shadow: 0 4px 12px rgba(240, 192, 64, 0.15);
}
</style>
