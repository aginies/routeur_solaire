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
        <!-- Dots rising from bottom (ambient atmosphere) -->
        <span class="particle p1"></span><span class="particle p2"></span><span class="particle p3"></span>
        <span class="particle p4"></span><span class="particle p5"></span><span class="particle p6"></span>
        <span class="particle p7"></span><span class="particle p8"></span><span class="particle p9"></span>
        <span class="particle p10"></span><span class="particle p11"></span><span class="particle p12"></span>
        <span class="particle p13"></span><span class="particle p14"></span><span class="particle p15"></span>

        <!-- Cross-fire dots — horizontal drift -->
        <span class="cross c1" style="--dur: 6s;  --dy: -80px;"></span>
        <span class="cross c2" style="--dur: 7s;  --delay: 1.5s; --dx: 40px;"></span>
        <span class="cross c3" style="--dur: 5.5s; --delay: 3s;   --dy: -60px;"></span>

        <!-- Sun rays — long thin streaks shooting upward -->
        <span class="ray r1"  style="--angle: -75deg; --dur: 4.2s;"></span>
        <span class="ray r2"  style="--angle: -60deg; --dur: 5.1s; --delay: 0.9s;"></span>
        <span class="ray r3"  style="--angle: -45deg; --dur: 4.7s; --delay: 1.8s;"></span>
        <span class="ray r4"  style="--angle: -30deg; --dur: 5.6s; --delay: 2.4s;"></span>
        <span class="ray r5"  style="--angle: -15deg; --dur: 4.9s; --delay: 0.5s;"></span>
        <span class="ray r6"  style="--angle:    0deg; --dur: 5.8s; --delay: 3.2s;"></span>
        <span class="ray r7"  style="--angle: +15deg; --dur: 6.0s; --delay: 1.1s;"></span>
        <span class="ray r8"  style="--angle: +30deg; --dur: 5.4s; --delay: 2.7s;"></span>
        <span class="ray r9"  style="--angle: +45deg; --dur: 6.3s; --delay: 0.8s;"></span>
        <span class="ray r10" style="--angle: +60deg; --dur: 4.6s; --delay: 2.0s;"></span>
        <span class="ray r11" style="--angle: +75deg; --dur: 5.9s; --delay: 3.8s;"></span>

        <!-- Horizontal rays — radiate outward from center -->
        <span class="ray h-ray h1"  style="--dir-h: 0deg;   --dur: 4.5s;  --x: -60%;"></span>
        <span class="ray h-ray h2"  style="--dir-h: 15deg;  --dur: 5.2s;  --delay: 1.3s;"></span>
        <span class="ray h-ray h3"  style="--dir-h: -15deg; --dur: 4.8s;  --delay: 0.6s;"></span>
        <span class="ray h-ray h4"  style="--dir-h: 30deg;  --dur: 5.7s;  --delay: 2.1s;"></span>
        <span class="ray h-ray h5"  style="--dir-h: -30deg; --dur: 6.1s;  --delay: 1.7s;"></span>

        <!-- Spiral rays — rotate as they rise -->
        <span class="spiral s1" style="--dur: 8s; --dir: 1;">
          <i></i><i></i><i></i>
        </span>
        <span class="spiral s2" style="--dur: 9.5s; --delay: 3s; --dir: -1;">
          <i></i><i></i><i></i>
        </span>

        <!-- Sparkles near the top of the hero -->
        <span class="spark k1" style="--dur: 2.5s;"></span>
        <span class="spark k2" style="--dur: 3.2s; --delay: 0.8s;"></span>
        <span class="spark k3" style="--dur: 2.8s; --delay: 1.6s;"></span>
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

/* Dots rising from bottom — ambient atmosphere */
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

/* Sun rays — thin golden streaks shooting upward */
.ray {
  position: absolute;
  left: 50%;
  bottom: -6px;
  width: 2px;
  height: 140px;
  background: linear-gradient(to top, #f0c040, transparent);
  transform-origin: center bottom;
  opacity: 0;
  animation: rayShoot var(--dur) ease-in-out infinite;
  animation-delay: var(--delay, 0s);
}

@keyframes rayShoot {
  0%   { opacity: 0; height: 20px; }
  15%  { opacity: 0.6; }
  35%  { opacity: 0.8; height: 140px; }
  55%  { opacity: 0.9; height: 150px; }
  75%  { opacity: 0.4; height: 120px; }
  100% { opacity: 0; height: 60px; transform: rotate(var(--angle)) translateY(-80px); }
}

/* Spiral rays — rotate as they rise */
.spiral {
  position: absolute;
  left: 50%;
  bottom: -10px;
  width: 4px;
  height: 300px;
  opacity: 0;
  animation: spiralRise var(--dur) ease-in-out infinite;
  animation-delay: var(--delay, 0s);
}

.spiral i {
  position: absolute;
  left: 0;
  width: 4px;
  height: 300px;
  border-radius: 50%;
  background: linear-gradient(to top, transparent 20%, #f0c040 60%, transparent);
  transform-origin: center bottom;
  animation: spiralSpin calc(var(--dur) * 1.5) linear infinite;
  animation-direction: var(--dir, 1);
}

@keyframes spiralRise {
  0%   { opacity: 0; transform: translateY(0); }
  20%  { opacity: 0.4; }
  50%  { opacity: 0.6; }
  80%  { opacity: 0.3; }
  100% { opacity: 0; transform: translateY(-150px); }
}

@keyframes spiralSpin {
  from { transform: rotate(0deg); }
  to   { transform: rotate(360deg * var(--dir, 1)); }
}

/* Sparkles — twinkling near the top of hero */
.spark {
  position: absolute;
  left: 50%;
  bottom: 40%;
  width: 8px;
  height: 8px;
  background: radial-gradient(circle, #f0c040 30%, transparent);
  border-radius: 50%;
  opacity: 0;
  animation: sparkle var(--dur) ease-in-out infinite;
  animation-delay: var(--delay, 0s);
}

@keyframes sparkle {
  0%   { transform: scale(0.3); opacity: 0; }
  25%  { transform: scale(1); opacity: 0.8; }
  50%  { transform: scale(0.5); opacity: 0; }
  75%  { transform: scale(1); opacity: 0.6; }
  100% { transform: scale(0.3); opacity: 0; }
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
