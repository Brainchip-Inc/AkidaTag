// @ts-check
import { existsSync, readdirSync } from 'node:fs';
import { defineConfig } from 'astro/config';
import starlight from '@astrojs/starlight';

/**
 * Sidebar entries for the hardware pages, in reading order, taken from the
 * files under docs/hardware. Empty while that directory does not exist, so the
 * site builds before the hardware pages land. The labels are short, like the
 * other groups' entries; the page titles carry the product name.
 *
 * @returns {Array<{ label: string, slug: string }>} One entry per hardware page.
 */
function hardwarePages() {
  const directory = new URL('../docs/hardware/', import.meta.url);
  if (!existsSync(directory)) return [];
  const preferred = ['datasheet', 'technical-specifications', 'block-diagram'];
  const rank = slug => {
    const index = preferred.indexOf(slug);
    return index === -1 ? preferred.length : index;
  };
  const label = slug =>
    slug.charAt(0).toUpperCase() + slug.slice(1).replace(/-/g, ' ');
  return readdirSync(directory)
    .filter(name => name.endsWith('.md'))
    .map(name => name.replace(/\.md$/, ''))
    .sort((a, b) => rank(a) - rank(b) || a.localeCompare(b))
    .map(slug => ({ label: label(slug), slug: `hardware/${slug}` }));
}

const hardware = hardwarePages();

// The site is served under the repository name on GitHub Pages. Its pages are
// generated from ../docs by scripts/sync-docs.mjs before every build.
export default defineConfig({
  site: 'https://brainchip-inc.github.io',
  base: '/AkidaTag',
  integrations: [
    starlight({
      title: 'AkidaTag',
      description:
        "How to use and build on AkidaTag, BrainChip's ultra-low-power AIoT platform.",
      logo: {
        src: './src/assets/brainchip-wordmark-white.svg',
        alt: 'BrainChip',
        replacesTitle: true,
      },
      customCss: ['./src/styles/brainchip.css'],
      components: {
        Footer: './src/components/Footer.astro',
        Sidebar: './src/components/Sidebar.astro',
      },
      social: [
        {
          icon: 'github',
          label: 'GitHub',
          href: 'https://github.com/Brainchip-Inc/AkidaTag',
        },
      ],
      sidebar: [
        {
          label: 'Start here',
          items: [
            { label: 'Quick start', slug: 'quick-start' },
            { label: 'User guide', slug: 'user-guide' },
            { label: 'Developer guide', slug: 'developer-guide' },
          ],
        },
        ...(hardware.length ? [{ label: 'Hardware', items: hardware }] : []),
        {
          label: 'Firmware reference',
          items: [
            { label: 'Environment setup', slug: 'setup' },
            {
              label: 'Firmware update over USB-C',
              slug: 'firmware-update-over-usb',
            },
            {
              label: 'BLE model transfer protocol',
              slug: 'ble-model-transfer',
            },
          ],
        },
        {
          label: 'Help',
          items: [
            { label: 'FAQ', slug: 'faq' },
            { label: 'Support', slug: 'support' },
            { label: 'Release notes', slug: 'release-notes' },
            { label: 'Open-source licences', slug: 'licences' },
          ],
        },
      ],
    }),
  ],
});
