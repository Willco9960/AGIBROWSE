import http from 'node:http';
import { createHash } from 'node:crypto';
import { readFile } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';

export function dataset(seed = '004') {
  if (typeof seed !== 'string' || seed.length < 1 || seed.length > 64) throw new Error('Seed must contain 1–64 characters');
  const hash = createHash('sha256').update(seed).digest();
  return { seed, code: hash.toString('hex').slice(0, 12), items: Array.from({ length: 3 }, (_, i) => ({ id: i + 1, label: `Item ${hash.subarray(i * 3, i * 3 + 3).toString('hex')}` })) };
}
const fixtures = new URL('../../tests/fixtures/', import.meta.url);
const files = new Map([['/', 'index.html'], ['/forms', 'forms.html'], ['/dynamic', 'dynamic.html'], ['/navigation', 'navigation.html'], ['/proof', 'proof.html'], ['/foreign', 'foreign.html']]);
const escape = value => String(value).replaceAll('&', '&amp;').replaceAll('<', '&lt;').replaceAll('>', '&gt;').replaceAll('"', '&quot;');

export async function startFixtures({ seed = '004', ports = [0, 0] } = {}) {
  let data = dataset(seed);
  let state = { submissions: [], steps: [], reports: [] };
  const servers = [];
  const origins = [];
  async function handle(req, res, index) {
    const json = (status, value) => { res.writeHead(status, { 'Content-Type': 'application/json; charset=utf-8' }); res.end(JSON.stringify(value)); };
    res.setHeader('Cache-Control', 'no-store');
    res.setHeader('X-Content-Type-Options', 'nosniff');
    if (req.headers.host !== new URL(origins[index]).host) return json(403, { error: 'Unexpected Host' });
    if (req.headers.origin && req.headers.origin !== origins[index]) return json(403, { error: 'Cross-origin mutation denied' });
    const url = new URL(req.url, origins[index]);
    let body = '';
    for await (const chunk of req) { body += chunk; if (Buffer.byteLength(body) > 8192) return json(413, { error: 'Body too large' }); }
    if (index === 1) {
      if (req.method !== 'GET' || url.pathname !== '/foreign') return json(404, { error: 'Not found' });
    } else {
      if (req.method === 'GET' && url.pathname === '/api/state') return json(200, { data, ...state });
      if (req.method === 'POST' && url.pathname === '/api/reset') { state = { submissions: [], steps: [], reports: [] }; return json(200, { data, ...state }); }
      if (req.method === 'POST' && url.pathname === '/api/step') {
        if (state.steps.length >= data.items.length) return json(409, { error: 'No more steps' });
        const item = data.items[state.steps.length]; state.steps.push(item.id); return json(200, item);
      }
      if (req.method === 'POST' && url.pathname === '/api/report') {
        let report; try { report = JSON.parse(body); } catch { return json(400, { error: 'Invalid JSON' }); }
        if (!Array.isArray(report) || report.length > 20 || report.some(x => typeof x.name !== 'string' || typeof x.passed !== 'boolean')) return json(400, { error: 'Invalid report' });
        state.reports.push(report); return json(200, { accepted: true });
      }
      if (req.method === 'POST' && url.pathname === '/submit') {
        const fields = new URLSearchParams(body);
        const name = fields.get('name')?.trim(); const email = fields.get('email');
        if (!name || name.length > 40 || !/^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(email ?? '') || fields.get('choice') !== 'alpha') return json(422, { error: 'Invalid form' });
        const submission = { id: state.submissions.length + 1, name, email, choice: 'alpha', code: data.code };
        state.submissions.push(submission);
        res.writeHead(303, { Location: `/receipt?id=${submission.id}` }); return res.end();
      }
      if (req.method === 'GET' && url.pathname === '/receipt') {
        const submission = state.submissions.find(x => String(x.id) === url.searchParams.get('id'));
        if (!submission) return json(404, { error: 'Unknown receipt' });
        res.writeHead(200, { 'Content-Type': 'text/html; charset=utf-8' }); return res.end(`<title>Receipt</title><h1>Submitted</h1><output id="receipt">${escape(JSON.stringify(submission))}</output>`);
      }
      if (req.method === 'GET' && url.pathname === '/redirect') { res.writeHead(302, { Location: '/navigation?arrived=redirect' }); return res.end(); }
      if (req.method === 'GET' && url.pathname === '/cross-redirect') { res.writeHead(302, { Location: `${origins[1]}/foreign` }); return res.end(); }
    }
    if (req.method !== 'GET' || !files.has(url.pathname)) return json(404, { error: 'Not found' });
    const page = (await readFile(new URL(files.get(url.pathname), fixtures), 'utf8'))
      .replaceAll('{{PRIMARY}}', origins[0]).replaceAll('{{SECONDARY}}', origins[1]);
    res.writeHead(200, { 'Content-Type': 'text/html; charset=utf-8' }); res.end(page);
  }
  try {
    for (let i = 0; i < 2; i++) {
      const server = http.createServer((req, res) => handle(req, res, i).catch(() => { res.writeHead(500); res.end('Fixture error'); }));
      servers.push(server);
      await new Promise((resolve, reject) => { server.once('error', reject); server.listen(ports[i], '127.0.0.1', resolve); });
      origins.push(`http://127.0.0.1:${server.address().port}`);
    }
  } catch (error) { await Promise.all(servers.map(s => new Promise(r => s.close(r)))); throw error; }
  return { origins, close: () => Promise.all(servers.map(s => new Promise(r => { s.close(r); s.closeAllConnections(); }))) };
}
if (process.argv[1] && fileURLToPath(import.meta.url) === process.argv[1]) {
  const seed = process.argv[2] ?? '004';
  const run = await startFixtures({ seed, ports: [43110, 43111] });
  console.log(JSON.stringify({ seed, origins: run.origins, proof: `${run.origins[0]}/proof` }));
  for (const signal of ['SIGINT', 'SIGTERM']) process.on(signal, async () => { await run.close(); process.exit(0); });
}
