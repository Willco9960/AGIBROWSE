import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
const [portFile, stopFile, resultFile] = process.argv.slice(2);
if (!portFile || !stopFile || !resultFile) process.exit(64);
const html = fs.readFileSync(path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../tests/fixtures/profiles.html'));
let requests = 0;
const server = http.createServer((request, response) => {
  const url = new URL(request.url, 'http://127.0.0.1');
  if (url.pathname === '/profile.html') {
    response.writeHead(200, { 'Content-Type': 'text/html', 'Cache-Control': 'no-store' }); response.end(html);
  } else if (url.pathname === '/http-cache') {
    requests++;
    response.writeHead(200, { 'Content-Type': 'text/plain', 'Cache-Control': 'public, max-age=86400', 'Content-Length': '1' });
    response.end(String(requests));
  } else { response.writeHead(404, { 'Cache-Control': 'no-store' }); response.end(); }
});
server.listen(0, '127.0.0.1', () => fs.writeFileSync(portFile, String(server.address().port)));
const timer = setInterval(() => {
  if (!fs.existsSync(stopFile)) return;
  clearInterval(timer);
  server.close(() => { fs.writeFileSync(resultFile, JSON.stringify({ httpCacheRequests: requests })); process.exit(0); });
}, 100);
setTimeout(() => { fs.writeFileSync(resultFile, JSON.stringify({ httpCacheRequests: requests, timeout: true })); process.exit(1); }, 120000).unref();
