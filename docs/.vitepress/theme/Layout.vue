<script setup>
import DefaultTheme from 'vitepress/theme'
import { onMounted, ref } from 'vue'
import './custom.css'

const { Layout } = DefaultTheme
const showScrollTop = ref(false)

onMounted(() => {
  const handler = () => {
    showScrollTop.value = window.scrollY > 300
  }
  window.addEventListener('scroll', handler, { passive: true })
})
</script>

<template>
  <Layout>
    <template #home-hero-after>
      <!-- Floating sun particles rendered behind hero content -->
      <div class="hero-particles">
        <span class="particle p1"></span>
        <span class="particle p2"></span>
        <span class="particle p3"></span>
        <span class="particle p4"></span>
        <span class="particle p5"></span>
      </div>

      <!-- Scroll to top button -->
      <button id="scroll-top" v-show="showScrollTop" @click="window.scrollTo({top:0,behavior:'smooth'})">↑</button>
    </template>

    <!-- Reveal-on-scroll for feature cards -->
    <template #home-features-after>
      <div class="reveal">
        <slot name="home-features-after" />
      </div>
    </template>
  </Layout>
</template>

<style scoped>
/* ── Hero Particles ─────────────────────────── */
.hero-particles {
  position: absolute;
  inset: 0;
  pointer-events: none;
  z-index: 0;
  overflow: hidden;
}

.particle {
  position: absolute;
  width: 6px;
  height: 6px;
  background: radial-gradient(circle, #f0c040, transparent);
  border-radius: 50%;
  opacity: 0.5;
  animation: floatUp linear infinite;
}

.p1 { left: 10%;  bottom: -10px; width: 8px; height: 8px; animation-duration: 12s; }
.p2 { left: 30%;  bottom: -10px; animation-duration: 9s;  animation-delay: 2s; }
.p3 { left: 55%;  bottom: -10px; width: 10px;height: 10px;animation-duration: 14s; animation-delay: 4s; }
.p4 { left: 75%;  bottom: -10px; animation-duration: 11s; animation-delay: 1s; }
.p5 { left: 90%;  bottom: -10px; width: 7px; height: 7px;animation-duration: 13s; animation-delay: 6s; }

@keyframes floatUp {
  0%   { transform: translateY(0) scale(1); opacity: 0.5; }
  40%  { opacity: 0.8; }
  100% { transform: translateY(-120vh) scale(0.3); opacity: 0; }
}

/* ── Scroll-to-top Button (visible on scroll) ── */
#scroll-top {
  position: fixed;
  right: 24px;
  bottom: 28px;
  width: 46px;
  height: 46px;
  border-radius: 50%;
  background: #1a1a2e;
  color: #f0c040;
  font-size: 20px;
  border: none;
  cursor: pointer;
  transition: opacity 0.3s ease, transform 0.3s ease;
  box-shadow: 0 4px 15px rgba(0, 0, 0, 0.4);
}

#scroll-top:hover {
  transform: scale(1.1);
}

/* ── Reveal-on-scroll fade-in wrapper ───────── */
.reveal {
  animation: fadeIn 0.6s ease both;
}

@keyframes fadeIn {
  from { opacity: 0; transform: translateY(20px); }
  to   { opacity: 1; transform: translateY(0); }
}
</style>
