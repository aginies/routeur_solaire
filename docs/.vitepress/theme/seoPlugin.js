// SEO Plugin for VitePress — injects per-page OG and Twitter meta tags
// Usage: add frontmatter to any page:
// ---
// description: "My page description"
// title: "My Page Title"
// ---

const DEFAULT_TITLE = 'Routeur Solaire'
const DEFAULT_DESC = 'Optimisez votre autoconsommation avec un routeur PV intelligent pour ESP32'
const DEFAULT_IMAGE = '/routeur_solaire/config.png'
const SITE_URL = 'https://aginies.github.io/routeur_solaire/'

export default function SeoPlugin() {
  return {
    name: 'vitepress-plugin-seo',
    enforce: 'pre',
    transformHtml(code, id) {
      // Only transform markdown pages
      if (!id.endsWith('.md')) return

      const frontmatterMatch = code.match(/^---\n([\s\S]*?)\n---/)
      if (!frontmatterMatch) return

      const fm = frontmatterMatch[1]
      let title = DEFAULT_TITLE
      let desc = DEFAULT_DESC
      let image = DEFAULT_IMAGE

      const titleMatch = fm.match(/title\s*:\s*["']([^"']+)["']/)
      if (titleMatch) title = titleMatch[1]

      const descMatch = fm.match(/description\s*:\s*["']([^"']+)["']/)
      if (descMatch) desc = descMatch[1]

      const imageMatch = fm.match(/image\s*:\s*["']([^"']+)["']/)
      if (imageMatch) image = imageMatch[1]

      const pagePath = id.replace(/\/home\/aginies\/devel\/github\/agines\/routeur_solaire\/docs\//, '/')
      const url = SITE_URL + pagePath.replace(/\.md$/, '')

      const metaTags = [
        `<meta property="og:title" content="${title}" />`,
        `<meta property="og:description" content="${desc}" />`,
        `<meta property="og:url" content="${url}" />`,
        `<meta property="og:image" content="${url.replace(/\/$/, '')}/${image.replace(/^\//, '')}" />`,
        `<meta name="twitter:card" content="summary_large_image" />`,
        `<meta name="twitter:title" content="${title}" />`,
        `<meta name="twitter:description" content="${desc}" />`,
      ]

      const metaBlock = metaTags.join('\n')
      const headTag = '<head>'
      const headIndex = code.indexOf(headTag)
      if (headIndex !== -1) {
        return code.slice(0, headIndex + headTag.length) + '\n' + metaBlock + '\n' + code.slice(headIndex + headTag.length)
      }

      // Fallback: inject before </head>
      return code.replace('</head>', metaBlock + '\n</head>')
    }
  }
}
