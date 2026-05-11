<script setup>
import DefaultTheme from 'vitepress/theme'
import { onMounted } from 'vue'
import './custom.css'

const { Layout } = DefaultTheme

// Inject sun + particles into hero container at runtime (after hydration)
onMounted(() => {
  const heroContainer = document.querySelector('.VPHero.has-image .container')
  if (heroContainer) {
    const wrapper = document.createElement('div')
    wrapper.className = 'hero-bg'
    wrapper.innerHTML = `
      <div class="hero-sun" style="--sun-size:320px;"></div>
      <div class="hero-particles">
        <span class="particle p1"></span><span class="particle p2"></span><span class="particle p3"></span>
        <span class="particle p4"></span><span class="particle p5"></span><span class="particle p6"></span>
        <span class="particle p7"></span><span class="particle p8"></span><span class="particle p9"></span>
        <span class="particle p10"></span><span class="particle p11"></span><span class="particle p12"></span>
        <span class="particle p13"></span><span class="particle p14"></span><span class="particle p15"></span>
        <span class="spiral s1" style="--dur:8s;--dir:1;"></span>
        <span class="spiral s2" style="--dur:9.5s;--delay:3s;--dir:-1;"></span>
      </div>
    `
    heroContainer.insertBefore(wrapper, heroContainer.firstChild)
  }

  // Inject license badge after features section (inside VPHome)
  const homeFeatures = document.querySelector('.VPHomeFeatures')
  if (homeFeatures && homeFeatures.parentNode) {
    const container = document.createElement('div')
    container.className = 'license-container'
    container.innerHTML = `
      <p class="license-badge" style="display: block; text-align: center; margin-top: 32px; padding: 8px 16px; background: rgba(240, 192, 64, 0.06); border: 1px solid rgba(240, 192, 64, 0.2); border-radius: 8px; font-size: 13px !important; color: #a89b70 !important;">
        Open source — GPLv3 &middot; <a href="https://github.com/aginies/routeur_solaire" target="_blank" rel="noopener" style="color: #f0c040; text-decoration: none;">GitHub</a>
      </p>
    `
    homeFeatures.parentNode.insertBefore(container, homeFeatures.nextSibling)
  }

  // Scroll-to-top handler (update display directly)
  const updateScrollTop = () => { document.getElementById('scrollToTop').style.display = window.scrollY > 300 ? 'block' : 'none' }
  window.addEventListener('scroll', updateScrollTop, { passive: true })
  updateScrollTop() // initial state

  // Inject scroll-to-top button into body (after all content) with hover style inline
  const btn = document.createElement('button')
  btn.id = 'scrollToTop'
  btn.setAttribute('style', `position: fixed; right: 24px; bottom: 28px; width: 46px; height: 46px; border-radius: 50%; background: #1a1a2e; color: #f0c040; font-size: 20px; border: none; cursor: pointer; transition: opacity 0.3s ease, transform 0.3s ease; box-shadow: 0 4px 15px rgba(0, 0, 0, 0.4);`)
  btn.textContent = '↑'
  btn.addEventListener('click', () => window.scrollTo({ top: 0, behavior: 'smooth' }))
  // hover effect via CSS (injected after DOM ready to avoid Vue parser)
  const hoverStyle = document.createElement('style')
  hoverStyle.textContent = '#scrollToTop:hover { transform: scale(1.1); }'
  document.head.appendChild(hoverStyle)
  document.body.appendChild(btn)
})
</script>

<template>
  <Layout>
    <!-- Sun + particles injected INSIDE VPHero via info-before slot -->
    <template #home-hero-info-before>
      <div class="hero-bg">
        <div class="hero-sun" style="--sun-size:320px;"></div>
        <div class="hero-particles">
          <!-- Dots radiating outward from sun (ambient atmosphere) -->
          <span class="particle p1"></span><span class="particle p2"></span><span class="particle p3"></span>
          <span class="particle p4"></span><span class="particle p5"></span><span class="particle p6"></span>
          <span class="particle p7"></span><span class="particle p8"></span><span class="particle p9"></span>
          <span class="particle p10"></span><span class="particle p11"></span><span class="particle p12"></span>
          <span class="particle p13"></span><span class="particle p14"></span><span class="particle p15"></span>

          <!-- Spiral rays — rotate as they rise (hidden, kept for future use) -->
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

/* ── Dots radiating outward from sun (ambient atmosphere) ─── */

.particle {
  position: absolute;
  width: 6px;
  height: 6px;
  background: radial-gradient(circle, #f0c040, transparent);
  border-radius: 50%;
  opacity: 0.5;
  animation: floatUp linear infinite;
}

.p1  { top: -20%; left: 58%; --drift: -80px; animation-duration: 13s; }
.p2  { top: -10%; left: 74%; --drift: -40px; animation-duration: 10s; animation-delay: 1.5s; }
.p3  { top:   0%; left: 66%; width: 8px; height: 8px;--drift: 30px;  animation-duration: 15s; animation-delay: 3s;   }
.p4  { top: -25%; left: 78%; --drift: 70px;  animation-duration: 11s; animation-delay: 0.8s; }
.p5  { top:    5%; left: 62%; width: 7px; height: 7px;--drift: -90px; animation-duration: 14s; animation-delay: 2.5s; }
.p6  { top: -35%; left: 80%; --drift: 50px;  animation-duration: 9.5s; animation-delay: 4s;   }
.p7  { top:   10%; left: 70%; width: 6px; height: 6px;--drift: -20px; animation-duration: 12s; animation-delay: 0.5s; }
.p8  { top: -45%; left: 68%; --drift: 100px; animation-duration: 16s; animation-delay: 3.5s; }
.p9  { top:   15%; left: 84%; width: 7px; height: 7px;--drift: -60px; animation-duration: 11s; animation-delay: 2s;   }
.p10 { top: -50%; left: 60%; --drift: 60px;  animation-duration: 13.5s; animation-delay: 5s; }
.p11 { top:    8%; left: 76%; width: 8px; height: 8px;--drift: -45px; animation-duration: 10.5s; animation-delay: 1.2s; }
.p12 { top: -30%; left: 84%; --drift: 80px;  animation-duration: 14s;   animation-delay: 4.5s; }
.p13 { top: -55%; left: 76%; width: 5px; height: 5px;--drift: -100px;animation-duration: 9s;    animation-delay: 0.3s; }
.p14 { top:   20%; left: 64%; --drift: 40px; animation-duration: 15.5s; animation-delay: 2.8s; }
.p15 { top: -15%; left: 90%; width: 7px; height: 7px;--drift: -35px; animation-duration: 12s;   animation-delay: 3.6s; }

@keyframes floatUp {
  0%   { transform: translateY(0) scale(1); opacity: 0.45; }
  20%  { opacity: 0.7; }
  50%  { opacity: 0.3; }
  100% { transform: translateY(-90vh) translateX(var(--drift, -60px)) scale(0.2); opacity: 0; }
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

/* ── License badge — at bottom of page ───────── */

.reveal .license-badge {
  display: inline-block;
  margin-top: 32px;
  padding: 8px 16px;
  background: rgba(240, 192, 64, 0.06);
  border: 1px solid rgba(240, 192, 64, 0.2);
  border-radius: 8px;
  font-size: 13px;
  color: #a89b70 !important;
}

.reveal .license-badge a {
  color: var(--rp-accent-1, #f0c040);
  text-decoration: none;
  transition: opacity 0.2s ease;
}

.reveal .license-badge a:hover {
  opacity: 0.8;
}

/* ── Reveal-on-scroll fade-in wrapper ─────────── */

.reveal {
  animation: fadeIn 0.6s ease both;
}

@keyframes fadeIn {
  from { opacity: 0; transform: translateY(20px); }
  to   { opacity: 1; transform: translateY(0); }
}

/* ── License badge container (same width as feature cards) ─── */

.license-container {
  max-width: 1152px;
  margin: 0 auto;
  padding: 0 48px; /* matches VPHomeFeatures .container */
}
</style>
