// Design checks only: no production policy evaluator, browser or attack execution.
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, existsSync } from 'node:fs';
import { resolve, dirname } from 'node:path';
const root = resolve(import.meta.dirname, '../..');
const read = p => readFileSync(resolve(root, p), 'utf8');
const vectors = JSON.parse(read('docs/security/decision-vectors.json'));
const manifest = JSON.parse(read('docs/contracts/agent-tools.reference.json'));
const closed = (v, keys) => v && typeof v === 'object' && !Array.isArray(v) && Object.keys(v).sort().join() === [...keys].sort().join();
const id = v => typeof v === 'string' && v.length > 0 && v.length <= 128 && !v.includes('*');
const list = (v, max, check) => Array.isArray(v) && v.length > 0 && v.length <= max && new Set(v.map(x => JSON.stringify(x))).size === v.length && v.every(check);
function context(v) {
  if (v?.kind === 'host_blank') return closed(v, ['kind', 'frame']) && v.frame === 'top';
  if (!closed(v, ['kind', 'origin', 'frame']) || v.kind !== 'origin' || !['top', 'same_origin_descendants'].includes(v.frame)) return false;
  try {
    const u = new URL(v.origin);
    return typeof v.origin === 'string' && !/[\s\\*]/u.test(v.origin) && ['http:', 'https:'].includes(u.protocol) && !u.username && !u.password && u.origin === v.origin;
  } catch { return false; }
}
// This intentionally validates documented record examples, not URL dispatch semantics.
function grant(v) {
  return closed(v, ['version','grant_id','client_id','session_id','profile_id','tab_ids','contexts','operations','mode','generation','expires_after_ms']) &&
    v.version === 1 && ['grant_id','client_id','session_id','profile_id'].every(k => id(v[k])) &&
    list(v.tab_ids, 64, id) && list(v.contexts, 32, context) && list(v.operations, 32, x => vectors.operations.includes(x)) &&
    ['observe','control'].includes(v.mode) && (v.mode === 'control' || v.operations.every(x => ['observe','wait'].includes(x))) &&
    Number.isSafeInteger(v.generation) && v.generation > 0 && Number.isSafeInteger(v.expires_after_ms) && v.expires_after_ms > 0 && v.expires_after_ms <= 28800000;
}
test('closed operations match unchanged public five-tool manifest', () => {
  assert.deepEqual(manifest.tools.map(t => t.name), ['aibrowese_connect','aibrowese_observe','aibrowese_act','aibrowese_wait','aibrowese_disconnect']);
  const actions = manifest.tools[2].inputSchema.properties.action.oneOf.flatMap(v => v.properties.type.enum ?? [v.properties.type.const]);
  assert.equal(actions.length, 19);
  assert.deepEqual([...vectors.operations].sort(), ['observe','wait',...actions].sort());
});
test('valid grant and 14 adversarial closed-record examples', () => {
  assert.ok(grant(vectors.grant_example));
  assert.ok(grant({...vectors.grant_example, contexts: [{kind:'host_blank',frame:'top'}]}));
  assert.equal(vectors.invalid_grant_overrides.length, 14);
  for (const v of vectors.invalid_grant_overrides) assert.equal(grant({...vectors.grant_example,...v.override}), false, v.case);
});
test('attack families and implementation owners have concrete planned cases', () => {
  assert.equal(vectors.status, 'design_only');
  assert.equal(new Set(vectors.cases.map(v => v.id)).size, vectors.cases.length);
  const categories = ['malicious_page','rogue_client','cross_profile','confused_deputy'];
  const tasks = JSON.parse(read('docs/module-map.json')).tasks;
  const taskIds = new Set(tasks.map(t => t.id));
  for (const v of vectors.cases) {
    assert.ok(closed(v, ['id','category','owners','fixture','expected','reason','status']), v.id);
    assert.match(v.id, /^SEC-\d{3}$/);
    assert.ok(categories.includes(v.category));
    assert.equal(v.status, 'planned_runtime');
    assert.ok(v.fixture.length >= 40 && v.expected.length > 0 && /^[A-Z_]+$/.test(v.reason));
    assert.ok(v.owners.length > 0 && v.owners.every(t => taskIds.has(`AIBROWESE-${t}`)), v.id);
  }
  for (const category of categories) assert.ok(vectors.cases.some(v => v.category === category));
  for (const owner of ['007','008','009','010','015','019','026','044','050','066','067','068','069']) assert.ok(vectors.cases.some(v => v.owners.includes(owner)), owner);
});
test('policy and evidence local Markdown links resolve', () => {
  for (const path of ['docs/security/policy.md','docs/evidence/AIBROWESE-006.md']) {
    for (const m of read(path).matchAll(/\]\(([^)]+)\)/g)) {
      if (/^https?:/.test(m[1])) continue;
      assert.ok(existsSync(resolve(root, dirname(path), m[1].split('#')[0])), `${path}: ${m[1]}`);
    }
  }
});
