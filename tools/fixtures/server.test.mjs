import test from 'node:test';
import assert from 'node:assert/strict';
import http from 'node:http';
import { readFile } from 'node:fs/promises';
import { dataset, startFixtures } from './server.mjs';

const expected = JSON.parse(await readFile(new URL('../../tests/fixtures/expected.json', import.meta.url), 'utf8'));
test('same seeds repeat data; different seeds change it', () => {
  assert.deepEqual(dataset('004'), dataset('004'));
  assert.notDeepEqual(dataset('004').items, dataset('005').items);
  assert.deepEqual(dataset('004'), expected.seed004);
  assert.throws(() => dataset(''));
  assert.throws(() => dataset('x'.repeat(65)));
});
test('loopback routing, form validation, steps, origins, reset and restart', async () => {
  const run = await startFixtures({ seed: '004' });
  const [primary, secondary] = run.origins;
  const request = (path, options) => fetch(primary + path, options);
  const state = async () => (await request('/api/state')).json();
  try {
    assert.notEqual(primary, secondary);
    assert.match(primary, /^http:\/\/127\.0\.0\.1:/);
    assert.equal((await request('/missing')).status, 404);
    assert.equal((await fetch(`${secondary}/api/state`)).status, 404);
    const foreign = await fetch(`${secondary}/foreign`);
    assert.equal(foreign.headers.get('access-control-allow-origin'), null);
    assert.match(await foreign.text(), new RegExp(primary.replaceAll('.', '\\.')));
    assert.equal((await request('/api/reset', { method: 'POST', headers: { Origin: secondary } })).status, 403);
    const badHostStatus = await new Promise((resolve, reject) => {
      http.get(`${primary}/api/state`, { headers: { Host: 'evil.test' } }, response => { response.resume(); resolve(response.statusCode); }).once('error', reject);
    });
    assert.equal(badHostStatus, 403);
    assert.equal((await request('/submit', { method: 'POST', body: 'name=Ada&email=bad&choice=alpha' })).status, 422);
    assert.equal((await state()).submissions.length, 0);
    const submit = await request('/submit', { method: 'POST', body: new URLSearchParams({ name: 'Ada', email: 'ada@example.test', choice: 'alpha' }), redirect: 'manual' });
    assert.equal(submit.status, 303); assert.equal(submit.headers.get('location'), '/receipt?id=1');
    assert.match(await (await request('/receipt?id=1')).text(), /Submitted/);
    assert.equal((await state()).submissions[0].email, 'ada@example.test');
    const redirect = await request('/redirect', { redirect: 'manual' });
    assert.equal(redirect.status, 302); assert.equal(redirect.headers.get('location'), '/navigation?arrived=redirect');
    assert.equal((await request('/cross-redirect', { redirect: 'manual' })).headers.get('location'), `${secondary}/foreign`);
    for (const item of dataset('004').items) assert.deepEqual(await (await request('/api/step', { method: 'POST' })).json(), item);
    assert.equal((await request('/api/step', { method: 'POST' })).status, 409);
    const before = await state(); assert.deepEqual(before.steps, [1, 2, 3]);
    assert.equal((await request('/api/report', { method: 'POST', body: '{}' })).status, 400);
    assert.equal((await request('/api/report', { method: 'POST', body: 'x'.repeat(8193) })).status, 413);
    await request('/api/reset', { method: 'POST' });
    assert.deepEqual(await state(), { data: dataset('004'), submissions: [], steps: [], reports: [] });
    assert.deepEqual(await (await request('/api/step', { method: 'POST' })).json(), dataset('004').items[0]);
  } finally { await run.close(); }
  const restarted = await startFixtures({ seed: '004' });
  try { assert.deepEqual(await (await fetch(`${restarted.origins[0]}/api/state`)).json(), { data: dataset('004'), submissions: [], steps: [], reports: [] }); } finally { await restarted.close(); }
});
