import { defineConfig } from 'vitepress'
import SeoPlugin from './theme/seoPlugin.js'

export default defineConfig({
  title: "Routeur Solaire",
  description: "Optimisez votre autoconsommation avec un routeur PV intelligent pour ESP32",
  base: '/routeur_solaire/',
  appearance: 'dark',
  lang: 'fr-FR',
  ignoreDeadLinks: true,
  theme: {
    default: 'custom'
  },
  head: [
    ['link', { rel: 'preconnect', href: 'https://fonts.googleapis.com' }],
    ['link', { rel: 'preconnect', href: 'https://fonts.gstatic.com', crossorigin: '' }],
    ['link', { rel: 'stylesheet', href: 'https://fonts.googleapis.com/css2?family=Inter:wght@400;500;600;700&display=swap' }],
    /* Favicon */
    ['link', { rel: 'icon', type: 'image/svg+xml', href: '/routeur_solaire/solar-icon.svg' }],
    ['link', { rel: 'icon', type: 'image/png', sizes: '32x32', href: '/routeur_solaire/favicon.png' }],

    /* Open Graph / social share */
    ['meta', { property: 'og:type', content: 'website' }],
    ['meta', { property: 'og:title', content: 'Routeur Solaire — Optimisez votre autoconsommation' }],
    ['meta', { property: 'og:description', content: 'Firmware libre pour ESP32 — routeur photovoltaïque haute performance qui optimise votre autoconsommation en redirigeant l\'énergie solaire excédentaire vers vos chauffe-eau et pompes.' }],
    ['meta', { property: 'og:image', content: 'https://aginies.github.io/routeur_solaire/config.png' }],
    ['meta', { property: 'og:url', content: 'https://aginies.github.io/routeur_solaire/' }],

    /* Twitter */
    ['meta', { name: 'twitter:card', content: 'summary_large_image' }],
    ['meta', { name: 'twitter:title', content: 'Routeur Solaire — Optimisez votre autoconsommation' }],

    /* JSON-LD structured data */
    ['script', { type: 'application/ld+json' }, `{"@context":"https://schema.org","@type":["WebSite","SoftwareApplication"],"name":"Routeur Solaire","description":"Firmware libre pour ESP32 — routeur photovoltaïque haute performance.","url":"https://aginies.github.io/routeur_solaire/","applicationCategory":"IoTApplication","operatingSystem":"ESP32","license":"https://opensource.org/licenses/GPL-3.0"}`],
  ],
  markdown: {
    config(md) {
      md.use(SeoPlugin)
    }
  },
  themeConfig: {
    nav: [
      { text: 'Accueil', link: '/' },
      { text: 'Guide', link: '/guide/quick-start' },
      { icon: 'github', link: 'https://github.com/aginies/routeur_solaire' }
    ],
    sidebar: [
      {
        text: 'Guide d\'utilisation',
        items: [
          { text: 'Démarrage rapide', link: '/guide/quick-start' },
          { text: 'Installation', link: '/guide/installation' },
          { text: 'Matériel', link: '/guide/hardware' },
          { text: 'Configuration', link: '/guide/configuration' },
          { text: 'Sécurité & Maintenance', link: '/guide/safety' },
          { text: "Captures d'écran", link: '/guide/screenshots' },
          { text: 'API & Diagnostics', link: '/guide/diagnostics' },
          { text: 'Dépannage', link: '/guide/troubleshooting' },
          { text: 'Journal des modifications', link: '/guide/changelog' },
        ]
      }
    ],
    socialLinks: [
      { icon: 'github', link: 'https://github.com/aginies/routeur_solaire' }
    ],
    search: {
      provider: 'local',
      options: {
        deep: true,
        translations: {
          button: { buttonText: 'Rechercher', buttonAriaLabel: 'Rechercher' },
          modal: {
            noResultsText: 'Aucun résultat pour',
            resetButtonTitle: 'Réinitialiser la recherche',
            footer: {
              selectText: 'pour sélectionner',
              navigateText: 'pour naviguer',
              closeText: 'pour fermer'
            }
          }
        }
      }
    },
    editLink: {
      pattern: 'https://github.com/aginies/routeur_solaire/edit/main/docs/:path',
      text: 'Modifier cette page sur GitHub'
    },
    lastUpdated: {
      text: 'Dernière mise à jour',
      formatOptions: {
        dateStyle: 'long',
        timeStyle: 'short'
      }
    }
  }
})