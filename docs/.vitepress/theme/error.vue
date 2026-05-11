<script setup lang="ts">
import { withBase } from 'vitepress'
</script>

<template>
  <div class="custom-404">
    <!-- Sun icon -->
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
}

.sun-icon {
  width: 120px;
  height: 120px;
  margin-bottom: 32px;
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
