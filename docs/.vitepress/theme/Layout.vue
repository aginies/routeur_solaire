<script setup>
import DefaultTheme from 'vitepress/theme'
import { onMounted, ref } from 'vue'
import './custom.css'

const { Layout } = DefaultTheme
const showScrollTop = ref(false)

onMounted(() => {
  window.addEventListener('scroll', () => {
    showScrollTop.value = window.scrollY > 300
  }, { passive: true })
})
</script>

<template>
  <Layout>
    <!-- Sun + particles injected INSIDE VPHero via info-before slot -->
    <template #home-hero-info-before>
      <div class="hero-bg">
        <div class="hero-sun" style="--sun-size:320px;"></div>
        <div class="hero-particles">
          <!-- Dots rising from bottom (ambient atmosphere) -->
          <span class="particle p1"></span><span class="particle p2"></span><span class="particle p3"></span>
          <span class="particle p4"></span><span class="particle p5"></span><span class="particle p6"></span>
          <span class="particle p7"></span><span class="particle p8"></span><span class="particle p9"></span>
          <span class="particle p10"></span><span class="particle p11"></span><span class="particle p12"></span>
          <span class="particle p13"></span><span class="particle p14"></span><span class="particle p15"></span>

          <!-- Cross-fire dots — horizontal drift -->
          <span class="cross c1" style="--dur:6s;--dy:-80px;"></span>
          <span class="cross c2" style="--dur:7s;--delay:1.5s;--dx:40px;"></span>
          <span class="cross c3" style="--dur:5.5s;--delay:3s;--dy:-60px;"></span>

          <!-- Spiral rays — rotate as they rise -->
          <span class="spiral s1" style="--dur:8s;--dir:1;"></span>
          <span class="spiral s2" style="--dur:9.5s;--delay:3s;--dir:-1;"></span>
        </div>
      </div>
    </template>

    <!-- Reveal-on-scroll for feature cards -->
    <template #home-features-after>
      <div class="reveal">
        <slot name="home-features-after" />
      </div>
    </template>

    <!-- Scroll to top button (outside VPHero) -->
    <button id="scroll-top" v-show="showScrollTop" @click="window.scrollTo({top:0,behavior:'smooth'})">↑</button>
  </Layout>
</template>

<style>
/* ── Hero Background (sun + particles) ─────────── */

.VPHero .container {
  position: relative; /* establishes stacking context for hero-sun */
}

/* ── Sun background wrapper ──────────────────────── */

.hero-bg {
  position: absolute;
  inset: 0;
  pointer-events: none;
  z-index: 0;
  overflow: visible !important;
}

/* ── Big Sun Glow (behind text, z-index=1) ─────── */

.hero-sun {
  position: absolute;
  top: -80px;
  left: 72%;
  transform: translateX(-50%);
  width: var(--sun-size, 320px);
  height: var(--sun-size, 320px);
  border-radius: 50%;
  background: radial-gradient(
    circle at center,
    #f0c040 18%,
    rgba(240, 192, 64, 0.5) 40%,
    transparent 70%
  );
  filter: blur(3px);
  pointer-events: none;
  z-index: 1 !important;
  animation: sunPulse 70s ease-in-out infinite alternate;
}

@keyframes sunPulse {
  0%   { opacity: 0.28; transform: translateX(-50%) scale(0.96); }
  14%  { opacity: 0.58; transform: translateX(-50%) scale(1.01); }
  27%  { opacity: 0.18; transform: translateX(-50%) scale(0.94); }
  44%  { opacity: 0.78; transform: translateX(-50%) scale(1.03); }
  56%  { opacity: 0.12; transform: translateX(-50%) scale(0.93); }
  72%  { opacity: 0.82; transform: translateX(-50%) scale(1.04); }
  86%  { opacity: 0.20; transform: translateX(-50%) scale(0.95); }
  100% { opacity: 0.34; transform: translateX(-50%) scale(0.97); }
}

/* ── Particles Container (above sun, below text) ─── */

.hero-particles {
  position: absolute;
  inset: 0;
  pointer-events: none;
  overflow: visible !important;
  z-index: 2 !important;
}

/* ── Dots rising from bottom (ambient atmosphere) ─── */

.particle {
  position: absolute;
  width: 6px;
  height: 6px;
  background: radial-gradient(circle, #f0c040, transparent);
  border-radius: 50%;
  opacity: 0.5;
  animation: floatUp linear infinite;
}

.p1  { left: 8%;   bottom: -12px; width: 7px; height: 7px; animation-duration: 13s; }
.p2  { left: 16%;  bottom: -12px; animation-duration: 10s; animation-delay: 1.5s; }
.p3  { left: 24%;  bottom: -12px; width: 9px; height: 9px;animation-duration: 15s; animation-delay: 3s;   }
.p4  { left: 36%;  bottom: -12px; animation-duration: 11s; animation-delay: 0.8s; }
.p5  { left: 48%;  bottom: -12px; width: 8px; height: 8px;animation-duration: 14s; animation-delay: 2.5s; }
.p6  { left: 60%;  bottom: -12px; animation-duration: 9.5s; animation-delay: 4s;   }
.p7  { left: 72%;  bottom: -12px; width: 6px; height: 6px;animation-duration: 12s; animation-delay: 0.5s; }
.p8  { left: 82%;  bottom: -12px; animation-duration: 16s; animation-delay: 3.5s; }
.p9  { left: 90%;  bottom: -12px; width: 7px; height: 7px;animation-duration: 11s; animation-delay: 2s;   }
.p10 { left: 96%;  bottom: -12px; animation-duration: 13.5s; animation-delay: 5s; }
.p11 { left: 4%;   bottom: -12px; width: 8px; height: 8px;animation-duration: 10.5s; animation-delay: 1.2s; }
.p12 { left: 30%;  bottom: -12px; animation-duration: 14s;   animation-delay: 4.5s; }
.p13 { left: 54%;  bottom: -12px; width: 6px; height: 6px;animation-duration: 9s;    animation-delay: 0.3s; }
.p14 { left: 76%;  bottom: -12px; animation-duration: 15.5s; animation-delay: 2.8s; }
.p15 { left: 88%;  bottom: -12px; width: 9px; height: 9px;animation-duration: 12s;   animation-delay: 3.6s; }

@keyframes floatUp {
  0%   { transform: translateY(0) scale(1); opacity: 0.4; }
  30%  { opacity: 0.7; }
  60%  { opacity: 0.5; }
  100% { transform: translateY(-120vh) scale(0.3); opacity: 0; }
}

/* ── Spiral rays — rotate as they rise (hidden, kept for future use) ─── */

.spiral {
  position: absolute;
  left: 50%;
  bottom: -10px;
  width: 4px;
  height: 300px;
  opacity: 0;
  animation: spiralRise var(--dur) ease-in-out infinite;
  animation-delay: var(--delay, 0s);
  display: none !important;
}

@keyframes spiralSpin {
  from { transform: rotate(0deg); }
  to   { transform: rotate(360deg * var(--dir, 1)); }
}

/* ── Reveal-on-scroll fade-in wrapper ─────────── */

.reveal {
  animation: fadeIn 0.6s ease both;
}

@keyframes fadeIn {
  from { opacity: 0; transform: translateY(20px); }
  to   { opacity: 1; transform: translateY(0); }
}

/* ── Scroll-to-top button (visible on scroll) ─── */

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
</style>
