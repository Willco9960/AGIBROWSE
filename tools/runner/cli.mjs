import { readFile } from 'node:fs/promises';
import { resolve } from 'node:path';
import { pathToFileURL } from 'node:url';
import { loadManifest, preflight, runTask, sha256, appendAudit } from './runner.mjs';

// Operator-owned config must pin the adapter bytes. Never load task/page-selected code.
export async function loadAdapter(config,root) {
 const pin=config?.trustedAdapter;
 if(!pin||typeof pin.path!=='string'||!/^[a-f0-9]{64}$/.test(pin.sha256)) throw new Error('Operator-pinned trustedAdapter path/SHA256 required');
 const path=resolve(root,pin.path);
 if(sha256(await readFile(path))!==pin.sha256) throw new Error('Trusted adapter SHA256 mismatch');
 return (await import(pathToFileURL(path).href)).default;
}
export async function main(argv=process.argv.slice(2)) {
 const [command,requestPath]=argv;
 if(!['preflight','run'].includes(command)||!requestPath||argv.length!==2) throw new Error('Usage: node tools/runner/cli.mjs preflight|run REQUEST.json');
 const root=process.cwd(),request=JSON.parse(await readFile(resolve(root,requestPath),'utf8'));
 const auditPath=request.auditPath;
 if(typeof auditPath!=='string'||!auditPath.trim()) throw new Error('auditPath required');
 try {
  const manifest=await loadManifest();
  const adapter=await loadAdapter(request.config,root);
  const args={...request,manifest,adapter,root};
  const result=command==='run'?await runTask(args):await preflight(args);
  if(command==='preflight') await appendAudit(auditPath,{event:'preflight',taskId:request.taskId,configured:result.route,adapterId:result.adapterId,status:'passed'});
  console.log(JSON.stringify(result,null,2));
  if(command==='run'&&!result.done) process.exitCode=2;
 } catch(error) {
  await appendAudit(auditPath,{event:'cli-blocked',taskId:request.taskId??null,status:'blocked',error:error.message}); throw error;
 }
}
if(process.argv[1]&&import.meta.url===pathToFileURL(resolve(process.argv[1])).href) main().catch(error=>{console.error(error.message);process.exitCode=1;});
