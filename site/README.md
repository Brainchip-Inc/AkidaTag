# The AkidaTag documentation site

The public documentation at https://brainchip-inc.github.io/AkidaTag/. It is a
[Starlight](https://starlight.astro.build) site whose pages are the Markdown
under `../docs/`, and `.github/workflows/pages.yml` builds and publishes it on
every push to `main` that touches the docs or the site.

## Working on it

Edit the pages under `docs/`; they stay plain Markdown that reads on GitHub as
it is. The site is its own npm project and needs Node.js 22 or later:

```sh
cd site
npm ci
npm run dev
```

`npm run build` writes the site to `dist/`, and `npm run preview` serves that
build at the same path GitHub Pages uses.

## How the pages get in

`scripts/sync-docs.mjs` runs before every build and copies `docs/` into
`src/content/docs/`, which is generated and never committed. On the way it
turns each page's first heading into the Starlight title, drops the copyright
line that the site's own footer band carries, and rewrites links: a link to
another page under `docs/` becomes that page's site URL, and a link to a file
elsewhere in the repository points at GitHub. `docs/BOARD_OVERLAY_CHANGES.md`
is left out of the site.

The sidebar is in `astro.config.mjs`. The hardware group is built from the
files under `docs/hardware/` when that directory exists.

## The look

`src/styles/brainchip.css`, `src/fonts/` and the logo files in `src/assets/`
are copied as they are from the BrainChip Connect documentation site, where
the look was approved: black text on white, white on black in dark mode, blue
links, Sora headings and Inter body text. Keep the two copies the same; change
the look there first.
