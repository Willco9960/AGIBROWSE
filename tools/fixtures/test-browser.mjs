import { spawn } from 'node:child_process';
import { mkdir, readFile, writeFile } from 'node:fs/promises';
import assert from 'node:assert/strict';
import { fileURLToPath } from 'node:url';
import { startFixtures } from './server.mjs';

if (process.platform !== 'win32') throw new Error('Native CEF lifecycle proof currently requires Windows');
const run = await startFixtures({ seed: process.argv[2] ?? '004' });
const root = fileURLToPath(new URL('../../', import.meta.url));
const evidence = fileURLToPath(new URL('../../build/fixture-browser/', import.meta.url));
let exitCode = null;
try {
  await mkdir(evidence, { recursive: true });
  const child = spawn('powershell.exe', ['-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', 'tools/cef/test-lifecycle.ps1', '-BuildDirectory', 'build/windows-cef-normalized', '-EvidenceDirectory', evidence, '-FixtureUrl', `${run.origins[0]}/proof`], { cwd: root, stdio: 'inherit', windowsHide: true });
  exitCode = await new Promise((resolve, reject) => { child.once('error', reject); child.once('exit', resolve); });
  const state = await (await fetch(`${run.origins[0]}/api/state`)).json();
  const report = { seed: state.data.seed, origins: run.origins, nativeHarnessExitCode: exitCode, submissions: state.submissions, steps: state.steps, reports: state.reports };
  await writeFile(`${evidence}/browser-report.json`, JSON.stringify(report, null, 2) + '\n');
  const expected = JSON.parse(await readFile(new URL('../../tests/fixtures/expected.json', import.meta.url), 'utf8'));
  if (exitCode !== 0 || state.reports.length !== 1 || !state.reports[0].every(x => x.passed)) throw new Error('Browser conformance proof failed; inspect build/fixture-browser evidence');
  assert.deepEqual(state.reports[0].map(x => x.name), expected.browserAssertions);
  assert.deepEqual(state.steps, expected.steps);
  assert.deepEqual(state.submissions, [{ ...expected.submission, code: state.data.code }]);
  console.log('PASS: 14 real CEF fixture assertions; sandbox and clean shutdown verified by lifecycle harness');
} finally { await run.close(); }
