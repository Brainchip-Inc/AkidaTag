/**
 * Copy the repository's docs/ tree into Starlight's content directory.
 *
 * The Markdown under docs/ stays plain and portable: no frontmatter, links to
 * sibling files by name, a copyright line at the foot. Starlight needs a title
 * in frontmatter and site URLs, and draws its own footer band, so every page is
 * rewritten on the way in. The copy is generated on every build and never
 * committed.
 */
import { promises as fs } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const siteDir = path.dirname(path.dirname(fileURLToPath(import.meta.url)));
const sourceDir = path.resolve(siteDir, '..', 'docs');
const targetDir = path.join(siteDir, 'src', 'content', 'docs');
const siteBase = '/AkidaTag';
const repositoryBlobUrl = 'https://github.com/Brainchip-Inc/AkidaTag/blob/main';
const excludedPages = new Set([
  'BOARD_OVERLAY_CHANGES.md',
  'ble-model-transfer.md',
]);
const footerPattern =
  /\n---\n\n© 2026 BrainChip Holdings Ltd\. All rights reserved\.\s*$/;

/**
 * List every file below a directory.
 *
 * @param {string} directory Directory to walk.
 * @returns {Promise<string[]>} Paths relative to the directory, with forward slashes.
 */
async function listFiles(directory) {
  const entries = await fs.readdir(directory, {
    withFileTypes: true,
    recursive: true,
  });
  return entries
    .filter(entry => entry.isFile())
    .map(entry =>
      path.relative(directory, path.join(entry.parentPath, entry.name)),
    )
    .map(relative => relative.split(path.sep).join('/'));
}

/**
 * The site URL of a docs page.
 *
 * @param {string} docPath Path of the page relative to docs/, such as hardware/datasheet.md.
 * @param {string} anchor Fragment to keep, empty or starting with "#".
 * @returns {string} Absolute URL path on the site, including the base.
 */
function pageUrl(docPath, anchor) {
  const slug = docPath.replace(/\.md$/, '').replace(/(^|\/)index$/, '');
  return (slug ? `${siteBase}/${slug}/` : `${siteBase}/`) + anchor;
}

/**
 * Rewrite the relative links of a page for the site: other docs pages become
 * their site URLs, and files elsewhere in the repository point at GitHub.
 *
 * @param {string} body Markdown body of the page.
 * @param {string} pageDir Directory of the page relative to docs/, "." at the root.
 * @returns {string} The body with its links rewritten.
 */
function rewriteLinks(body, pageDir) {
  return body.replace(/\]\(([^)\s]+)\)/g, (match, href) => {
    if (/^[a-z]+:/i.test(href) || href.startsWith('#') || href.startsWith('/'))
      return match;
    const [target, hash = ''] = href.split(/(?=#)/);
    const resolved = path.posix.normalize(path.posix.join(pageDir, target));
    if (resolved.startsWith('../'))
      return `](${repositoryBlobUrl}/${resolved.slice(3)}${hash})`;
    if (target.endsWith('.md')) return `](${pageUrl(resolved, hash)})`;
    return match;
  });
}

/**
 * Convert one Markdown page: its first heading becomes the title, the
 * copyright line at the foot is dropped, and its links become site links.
 *
 * @param {string} relPath Path of the page relative to docs/.
 * @param {string} text The page as written under docs/.
 * @returns {string} The page as Starlight expects it.
 */
function convertPage(relPath, text) {
  const heading = text.match(/^# (.+)$/m);
  if (!heading)
    throw new Error(`${relPath} has no top-level heading to use as its title`);
  const body = text.replace(`${heading[0]}\n`, '').replace(footerPattern, '\n');
  const frontmatter = `---\ntitle: ${JSON.stringify(heading[1].trim())}\n---\n`;
  return frontmatter + rewriteLinks(body, path.posix.dirname(relPath));
}

/**
 * Rebuild the content directory from docs/: pages are converted, everything
 * else (images) is copied as it is.
 */
async function main() {
  await fs.rm(targetDir, { recursive: true, force: true });
  let pages = 0;
  for (const relPath of await listFiles(sourceDir)) {
    if (excludedPages.has(relPath)) continue;
    const from = path.join(sourceDir, relPath);
    const to = path.join(targetDir, relPath);
    await fs.mkdir(path.dirname(to), { recursive: true });
    if (relPath.endsWith('.md')) {
      await fs.writeFile(
        to,
        convertPage(relPath, await fs.readFile(from, 'utf8')),
      );
      pages += 1;
    } else {
      await fs.copyFile(from, to);
    }
  }
  console.log(
    `synced ${pages} pages from ${path.relative(siteDir, sourceDir)}`,
  );
}

await main();
