import { defineConfig } from 'vitepress'

export default defineConfig({
  title: "Routeur Solaire",
  description: "Routeur PV haute performance pour ESP32",
  base: '/routeur_solaire/',
  appearance: 'dark',
  ignoreDeadLinks: true,
  theme: {
    default: 'custom'
  },
  head: [
    ['link', { rel: 'preconnect', href: 'https://fonts.googleapis.com' }],
    ['link', { rel: 'preconnect', href: 'https://fonts.gstatic.com', crossorigin: '' }],
    ['link', { rel: 'stylesheet', href: 'https://fonts.googleapis.com/css2?family=Inter:wght@400;500;600;700&display=swap' }],
  ],
  themeConfig: {
    nav: [
      { text: 'Accueil', link: '/' },
      { text: 'Guide', link: '/guide/introduction' }
    ],
    sidebar: [
      {
        text: 'Guide d\'utilisation',
        items: [
          { text: 'Introduction', link: '/guide/introduction' },
          { text: 'Matériel', link: '/guide/hardware' },
          { text: 'Configuration', link: '/guide/configuration' },
          { text: "Captures d'écran", link: '/guide/screenshots' },
          { text: 'Installation', link: '/guide/installation' }
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
    }
  }
})