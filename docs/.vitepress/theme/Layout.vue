<script setup>
import DefaultTheme from 'vitepress/theme'
import { onMounted } from 'vue'
import './custom.css'

const { Layout } = DefaultTheme

// Inject sun + particles into hero container at runtime (after hydration)
onMounted(() => {
  const heroContainer = document.querySelector('.VPHero .container')
  if (!heroContainer) return

  const wrapper = document.createElement('div')
  wrapper.className = 'hero-bg'
  wrapper.innerHTML = `
    <div class="hero-sun" style="--sun-size:320px;"></div>
    <canvas id="particleCanvas"></canvas>
  `
  heroContainer.insertBefore(wrapper, heroContainer.firstChild)

  // --- Particle canvas — mini circles spreading from sun center outward, synced with sun brightness ---
  const canvasEl = document.getElementById('particleCanvas')
  if (!canvasEl) return

  function resize() {
    const cw = Math.max(heroContainer.clientWidth, window.innerWidth * 0.6, 400)
    const ch = Math.max(heroContainer.clientHeight, window.innerHeight * 0.8, 350)
    canvasEl.width = cw; canvasEl.height = ch
    canvasEl.style.width = cw + 'px'; canvasEl.style.height = ch + 'px'
  }

  setTimeout(resize, 80); resize()
  window.addEventListener('resize', resize)

  const ctx = canvasEl.getContext('2d')
  if (!ctx) return

  let rafId = null
  const particles = []
  // Sync: the sun's opacity oscillates ~70s cycle with specific peaks
  function sunAlpha(now) {
    const t = (now / 1000) % 70   /* match sunPulse 70s period */
    if (t < 14) return 0.28 + (0.58 - 0.28) * (t / 14)
    if (t < 27) return 0.58 - (0.58 - 0.18) * ((t - 14) / 13)
    if (t < 44) return 0.18 + (0.78 - 0.18) * ((t - 27) / 17)
    if (t < 56) return 0.78 - (0.78 - 0.12) * ((t - 44) / 12)
    if (t < 72) return 0.12 + (0.82 - 0.12) * ((t - 56) / 16)
    /* t >= 72: from 0.82 back to 0.34 */
    return 0.82 - (0.82 - 0.34) * ((t - 72) / (70 - 72 + 14))   // wraps smoothly
  }

  function initP(p) {
    // Spawn right at the sun center (x=72% of canvas width, y=-80px)
    const sx = canvasEl.width * 0.72
    const sy = -80 + 260
    p.x = sx; p.y = sy
    // Random direction — full circle
    const a = Math.random() * Math.PI * 2
    // Speed outward from center (not purely radial, just directional)
    const spd = 0.15 + Math.random() * 0.3
    p.vx = Math.cos(a) * spd
    p.vy = Math.sin(a) * spd
    p.life = 0
    p.ml = 600 + Math.random() * 400 | 0   // ~10-17 seconds at 60fps — slower particles live longer
  }

  function drawP(ctx, p) {
    if (p.life >= p.ml) return
    const t = p.life / p.ml   // 0→1 over lifetime
    const sunBright = sunAlpha(Date.now())
    const alpha = sunBright * (1 - t * 0.45)   // particles breathe with the sun, fade as they travel outward
    ctx.save()
    ctx.globalAlpha = alpha
    // Mini circle — constant radius, no glow
    ctx.fillStyle = '#f0c040'
    ctx.beginPath()
    ctx.arc(p.x, p.y, 3.5, 0, Math.PI * 2)
    ctx.fill()
    ctx.restore()
  }

  function loop() {
    ctx.clearRect(0, 0, canvasEl.width, canvasEl.height)   // wipe before each frame
    for (const p of particles) {
      if (p.life >= p.ml) initP(p)   // recycle: send back to sun center
      drawP(ctx, p); p.x += p.vx; p.y += p.vy; p.life++
    }
    rafId = requestAnimationFrame(loop)
  }

  // Start with a full batch so we see them moving outwards immediately
  for (let i = 0; i < 120; i++) { const p = {}; initP(p); particles.push(p); p.life = Math.random() * 700 | 0 }
  rafId = requestAnimationFrame(loop)
})

// Inject license badge after features section (inside VPHome)
onMounted(() => {
  const homeFeatures = document.querySelector('.VPHomeFeatures')
  if (!homeFeatures || !homeFeatures.parentNode) return

  const container = document.createElement('div')
  container.className = 'license-container'
  container.innerHTML = `
    <p class="license-badge" style="display: block; text-align: center; margin-top: 32px; padding: 8px 16px; background: rgba(240, 192, 64, 0.06); border: 1px solid rgba(240, 192, 64, 0.2); border-radius: 8px; font-size: 13px !important; color: #a89b70 !important;">
      Open source — GPLv3 &middot; <a href="https://github.com/aginies/routeur_solaire" target="_blank" rel="noopener" style="color: #f0c040; text-decoration: none;">GitHub</a>
    </p>
  `
  homeFeatures.parentNode.insertBefore(container, homeFeatures.nextSibling)

  // Scroll-to-top handler (update display directly)
  const updateScrollTop = () => { const btn = document.getElementById('scrollToTop'); if (btn) btn.style.display = window.scrollY > 300 ? 'block' : 'none' }
  window.addEventListener('scroll', updateScrollTop, { passive: true })
  updateScrollTop()

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
    <!-- Sun + particles injected INSIDE VPHero via info-before slot (content added at runtime by JS) -->
    <template #home-hero-info-before></template>

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
  background: transparent !important;
}

.VPHero {
  background: transparent !important;
}

/* ── Sun background wrapper (high z-index to cover VitePress elements) ─────── */

.hero-bg {
  position: absolute;
  inset: 0;
  pointer-events: none;
  z-index: 98 !important;
  overflow: visible !important;
}

/* ── Big Sun Glow (behind text, z-index=99) ─────── */

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
  z-index: 99 !important;
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

/* ── Particle Canvas ─── */

#particleCanvas {
  position: absolute;
  inset: 0;
  pointer-events: none;
  overflow: visible !important;
  z-index: 100 !important;
  width: 100% !important;
  height: 100% !important;
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
