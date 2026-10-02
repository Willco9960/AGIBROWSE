import { readFile, appendFile, mkdir } from 'node:fs/promises';
import { createHash, randomUUID } from 'node:crypto';
import { dirname, resolve } from 'node:path';

export const sha256 = bytes => createHash('sha256').update(bytes).digest('hex');
const fail = message => { throw new Error(message); };
const object = x => x !== null && typeof x === 'object' && !Array.isArray(x);
const text = x => typeof x === 'string' && x.trim().length > 0;
const count = x => Number.isSafeInteger(x) && x >= 0;
const same = (a,b) => ['model','reasoning','mode'].every(k => a?.[k] === b?.[k]);
const settings = x => ({model:x.model,reasoning:x.reasoning,mode:x.mode});
export async function loadManifest(path = new URL('./routing-manifest.json', import.meta.url)) {
 const manifest=JSON.parse(await readFile(path,'utf8')); validateManifest(manifest); return manifest;
}
export function validateManifest(m) {
 if (!object(m) || m.version!==1 || !Array.isArray(m.assignments) || m.assignments.length!==90) fail('Manifest must contain all 90 assignments');
 const ids=new Set();
 for (const [index,t] of m.assignments.entries()) {
  if(t.taskId!==`AIBROWESE-${String(index+1).padStart(3,'0')}` || ids.has(t.taskId)) fail('Manifest task order/identity invalid');
  ids.add(t.taskId);
  if(!text(t.model)||!['low','medium','high'].includes(t.reasoning)||t.mode!=='standard'||!text(t.agent)||!text(t.fallback)) fail('Manifest routing invalid');
  if(!Array.isArray(t.prerequisites)||new Set(t.prerequisites).size!==t.prerequisites.length||!t.prerequisites.every(p=>ids.has(p)&&p!==t.taskId)) fail('Malformed prerequisite declaration');
  if(index>0&&!t.prerequisites.includes(m.assignments[index-1].taskId)) fail('Required sequential prerequisite missing');
  if(!Array.isArray(t.acceptanceCriteria)||!t.acceptanceCriteria.length||!t.acceptanceCriteria.every(text)) fail('Missing acceptance criteria');
  const allows=t.fallback.startsWith('After two focused failed repairs');
  if(allows ? !same(t.fallbackRoute,{model:'gpt-6.1-sol',reasoning:'high',mode:'standard'})||t.fallbackRoute.failedRepairsRequired!==2 : t.fallbackRoute!==null) fail('Malformed declared fallback');
 }
}
function adapterValid(adapter) {
 if(!object(adapter)||!text(adapter.id)||!['launch','verifyReceipt','verifyCompletion','getHistory'].every(k=>typeof adapter[k]==='function')) fail('Trusted executor adapter missing required methods');
}
async function evidenceValid(items,root) {
 if(!Array.isArray(items)||!items.length) fail('Verified evidence is missing');
 for(const item of items) {
  if(!object(item)||!text(item.path)||!/^[a-f0-9]{64}$/.test(item.sha256)||!text(item.description)) fail('Malformed evidence');
  const path=resolve(root,item.path);
  if(sha256(await readFile(path))!==item.sha256) fail(`Evidence hash mismatch: ${item.path}`);
 }
}
async function completionValid(raw,task,adapter,root,runId=null) {
 const c=await adapter.verifyCompletion(raw);
 if(!object(c)||c.trusted!==true||c.taskId!==task.taskId||c.status!=='verified'||!text(c.reviewer)||!text(c.source)||!text(c.verifiedAt)||!Number.isFinite(Date.parse(c.verifiedAt))) fail(`Prerequisite/completion unverified: ${task.taskId}`);
 if(runId!==null&&c.runId!==runId) fail('Completion belongs to a different run');
 if(!Array.isArray(c.criteria)||c.criteria.length!==task.acceptanceCriteria.length||!c.criteria.every((v,i)=>v?.criterion===task.acceptanceCriteria[i]&&v.passed===true&&Array.isArray(v.evidence)&&v.evidence.length&&v.evidence.every(n=>count(n)&&n<c.evidence?.length))) fail('Partial or malformed acceptance evidence');
 await evidenceValid(c.evidence,root); return c;
}
export async function preflight({manifest,taskId,config,completions,adapter,root=process.cwd(),history=[]}) {
 validateManifest(manifest); adapterValid(adapter);
 const task=manifest.assignments.find(t=>t.taskId===taskId); if(!task) fail('Unknown task');
 if(!object(config)||!text(config.model)||!text(config.reasoning)||!text(config.mode)) fail('Missing model/reasoning/mode configuration');
 if(!Array.isArray(config.availableRoutes)||!config.availableRoutes.some(r=>same(r,config))) fail('Configured model/reasoning combination unavailable');
 const prior=await adapter.getHistory(taskId);
 if(!Array.isArray(prior)) fail('Trusted adapter must supply authoritative attempt history');
 const attempts=[],failures=[];
 for(const raw of prior) {
  const r=await adapter.verifyReceipt(raw);
  if(!object(r)||r.trusted!==true||r.taskId!==taskId||!text(r.attemptId)||!text(r.runId)||!text(r.source)||!['completed','failed','unknown'].includes(r.status)||(!same(r.launcherObserved,task)&&!same(r.launcherObserved,task.fallbackRoute))) fail('Malformed/unverified task attempt history');
  if(attempts.includes(r.attemptId)) fail('Duplicate task attempt history'); attempts.push(r.attemptId);
  if(r.status==='failed'&&r.focusedRepair===true) {
   if(!text(r.failingCase)) fail('Focused failed repair lacks failing case'); failures.push(r);
  }
 }
 if(history.length) fail('Caller-supplied history forbidden; trusted adapter owns authoritative history');
 if(failures.length>=3) fail('Three failed focused repairs: stop for assumption review');
 const primaryFailures=failures.filter(r=>same(r.launcherObserved,task));
 let fallback=null;
 if(config.fallback!==undefined||!same(task,config)) {
  if(!task.fallbackRoute||!same(task.fallbackRoute,config)) fail('Model/reasoning/mode mismatch; no declared fallback');
  const failedIds=primaryFailures.map(r=>r.attemptId);
  if(primaryFailures.length!==2||!object(config.fallback)||config.fallback.failedAttempts?.length!==2||!config.fallback.failedAttempts.every((id,i)=>id===failedIds[i])||!text(config.fallback.failingCase)||!text(config.fallback.reason)) fail('Fallback needs exactly two verified focused failures and an explicit handoff');
  fallback={...config.fallback,route:settings(config)};
 } else {
  if(config.fallback!==undefined) fail('Unexpected fallback record on primary route');
  if(primaryFailures.length>=2&&task.fallbackRoute) fail('Two focused repairs failed: declared handoff required');
 }
 if(!object(completions)) fail('Prerequisite records missing');
 const verified=[];
 for(const id of task.prerequisites) {
  verified.push(await completionValid(completions[id],manifest.assignments.find(t=>t.taskId===id),adapter,root));
 }
 return {task,route:settings(config),fallback,prerequisites:verified.map(c=>({taskId:c.taskId,source:c.source,verifiedAt:c.verifiedAt})),adapterId:adapter.id,priorAttemptIds:attempts};
}
function usageValid(u) {
 if(u==null) return {input:null,output:null,cached:null,reasoning:null};
 if(!object(u)||!['input','output','cached','reasoning'].every(k=>u[k]===null||count(u[k]))) fail('Malformed token usage: use nonnegative integers or null');
 if(u.cached!==null&&(u.input===null||u.cached>u.input)||u.reasoning!==null&&(u.output===null||u.reasoning>u.output)) fail('Token subcounts exceed totals');
 return Object.fromEntries(['input','output','cached','reasoning'].map(k=>[k,u[k]]));
}
export function computeCost(usage,model,rates) {
 if(rates==null||model==null) return null;
 if(!object(rates)||!text(rates.version)||!text(rates.source)||!text(rates.currency)||!text(rates.asOf)||!Number.isFinite(Date.parse(rates.asOf))||!object(rates.models)) fail('Malformed versioned rates');
 const r=rates.models[model]; if(r==null) return null;
 if(!object(r)||!['input','output','cached'].every(k=>typeof r[k]==='number'&&Number.isFinite(r[k])&&r[k]>=0)) fail('Malformed rates per million tokens');
 if(usage.input===null||usage.output===null||usage.cached===null) return null;
 const amount=((usage.input-usage.cached)*r.input+usage.cached*r.cached+usage.output*r.output)/1e6;
 if(!Number.isFinite(amount)) fail('Cost overflow');
 return {amount,currency:rates.currency,source:rates.source,version:rates.version,asOf:rates.asOf,basis:'input includes cached; output includes reasoning; rates per 1M tokens'};
}
export async function appendAudit(path,record) {
 await mkdir(dirname(resolve(path)),{recursive:true});
 // O_APPEND: never rewrite earlier records. This log is not a tamper-resistant ledger.
 await appendFile(path,JSON.stringify({...record,timestamp:new Date().toISOString()})+'\n',{flag:'a'});
}
export async function runTask({manifest,taskId,config,completions,adapter,auditPath,root=process.cwd(),history=[],completion}) {
 if(!text(auditPath)) fail('Audit path required');
 const runId=randomUUID(); let gate;
 try { gate=await preflight({manifest,taskId,config,completions,adapter,root,history}); }
 catch(error) {await appendAudit(auditPath,{event:'preflight-blocked',runId,taskId,configured:object(config)?settings(config):null,error:error.message,status:'blocked'}); throw error;}
 const attemptId=randomUUID();
 await appendAudit(auditPath,{event:'launch',runId,attemptId,taskId,configured:gate.route,fallback:gate.fallback,prerequisites:gate.prerequisites,adapterId:adapter.id,priorAttemptIds:gate.priorAttemptIds,status:'started'});
 const start=performance.now(); let raw,launchError=null;
 try { raw=await adapter.launch(Object.freeze({runId,attemptId,taskId,...gate.route})); } catch(error) {launchError=error.message;}
 const measuredLatencyMs=performance.now()-start;
 let verified=null,receiptError=null;
 try {
  const r=await adapter.verifyReceipt(raw);
  if(!object(r)||r.trusted!==true||r.taskId!==taskId||r.runId!==runId||r.attemptId!==attemptId||!text(r.source)||!['completed','failed','unknown'].includes(r.status)||!same(r.launcherObserved,gate.route)) fail('Missing, mismatched or unverified executor receipt');
  if(r.providerObserved!==null&&r.providerObserved!==undefined&&(!same(r.providerObserved,gate.route)||!text(r.providerSource))) fail('Provider receipt contradicts approved route or lacks source');
  verified=r;
 } catch(error) {receiptError=error.message;}
 let usage={input:null,output:null,cached:null,reasoning:null},cost=null,accountingError=null;
 try {usage=usageValid(verified?.usage); cost=computeCost(usage,verified?.providerObserved?.model??null,config.rates);} catch(error){accountingError=error.message;}
 const actualModel=verified?.providerObserved?.model??null;
 await appendAudit(auditPath,{event:'attempt',runId,attemptId,taskId,configured:gate.route,launcherObserved:verified?.launcherObserved??null,providerObserved:verified?.providerObserved??null,actualModel,modelProvenance:actualModel?'trusted-provider-receipt':verified?'trusted-launcher-receipt':'unverified',receiptSource:verified?.source??null,providerSource:verified?.providerSource??null,usage,cost,measuredLatencyMs,executorLatencyMs:typeof verified?.latencyMs==='number'&&Number.isFinite(verified.latencyMs)&&verified.latencyMs>=0?verified.latencyMs:null,status:launchError?'failed':verified?.status??'unverified',launchError,receiptError,accountingError});
 let done=false,completionError=null;
 if(!launchError&&!receiptError&&!accountingError&&verified?.status==='completed') {
  try {await completionValid(typeof completion==='function'?await completion({runId,attemptId,taskId}):completion,gate.task,adapter,root,runId); done=true;} catch(error){completionError=error.message;}
 } else completionError='Execution failed, unknown, unverified, or accounting invalid';
 const result={runId,attemptId,taskId,status:done?'verified':'unverified',done,actualModel,launcherObserved:verified?.launcherObserved??null,usage,cost,completionError};
 await appendAudit(auditPath,{event:'completion',...result}); return result;
}
