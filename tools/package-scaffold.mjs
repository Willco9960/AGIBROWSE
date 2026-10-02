import { cp, mkdir, rm } from 'node:fs/promises';
import { resolve } from 'node:path';

const root = resolve(import.meta.dirname, '..');
const kind = process.argv[2];
const sources = {
  schema: ['schemas/README.md', 'docs/contracts/agent-tools.reference.json'],
  fixtures: ['tests/fixtures/README.md'],
  docs: ['README.md', 'LICENSE', 'THIRD-PARTY-NOTICES.md', 'docs']
};
if (!Object.hasOwn(sources, kind)) throw new Error('Usage: node tools/package-scaffold.mjs schema|fixtures|docs');
const packagesRoot = resolve(root, 'build/packages');
const destination = resolve(packagesRoot, kind);
if (!destination.startsWith(`${packagesRoot}/`) && !destination.startsWith(`${packagesRoot}\\`)) {
  throw new Error('Refusing to package outside build/packages');
}
await rm(destination, { recursive: true, force: true });
await mkdir(destination, { recursive: true });
for (const source of sources[kind]) {
  const target = resolve(destination, source);
  await mkdir(resolve(target, '..'), { recursive: true });
  await cp(resolve(root, source), target, { recursive: true });
}
console.log(`Packaged ${kind} scaffold into build/packages/${kind}`);
