import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdtemp, readFile, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { loadManifest, preflight, runTask, computeCost, sha256 } from './runner.mjs';
import { loadAdapter } from './cli.mjs';
const manifest=await loadManifest();
async function setup(taskId='AIBROWESE-005') {
 const root=await mkdtemp(join(tmpdir(),'runner-test-'));
 await writeFile(join(root,'proof.txt'),'passing integration evidence');
 const proof={path:'proof.txt',sha256:sha256('passing integration evidence'),description:'test proof'};
 const task=manifest.assignments.find(t=>t.taskId===taskId);
 const config={model:task.model,reasoning:task.reasoning,mode:task.mode,availableRoutes:[{model:task.model,reasoning:task.reasoning,mode:task.mode},{model:'gpt-6.1-sol',reasoning:'high',mode:'standard'}]};
 // Test trust boundary uses object identity; JSON with trusted:true is insufficient.
 const trusted=new WeakSet(); const trust=o=>{trusted.add(o);return o;};
 const complete=(t,runId)=>trust({taskId:t.taskId,runId,status:'verified',trusted:true,source:'independent-test-reviewer',reviewer:'test reviewer',verifiedAt:new Date().toISOString(),evidence:[proof],criteria:t.acceptanceCriteria.map(criterion=>({criterion,passed:true,evidence:[0]}))});
 const completions=Object.fromEntries(task.prerequisites.map(id=>[id,complete(manifest.assignments.find(t=>t.taskId===id))]));
 const prior=[]; const adapter={id:'test-only',async getHistory(){return prior;},async launch(r){return trust({...r,trusted:true,source:'authenticated-test-executor',launcherObserved:{model:r.model,reasoning:r.reasoning,mode:r.mode},providerObserved:null,status:'completed',usage:null});},async verifyReceipt(r){return trusted.has(r)?r:null;},async verifyCompletion(r){return trusted.has(r)?r:null;}};
 return {manifest,taskId,config,completions,adapter,root,auditPath:join(root,'audit.jsonl'),complete,task,trust,prior};
}
test('exact route and verified prerequisite pass; rejected settings never launch',async()=>{
 const a=await setup(); assert.equal((await preflight(a)).route.reasoning,'medium');
 for(const change of [{model:undefined},{reasoning:undefined},{mode:undefined},{model:'gpt-6-astra'},{reasoning:'high'},{mode:'premium'},{availableRoutes:[]}]) {
  let launches=0; a.adapter.launch=async()=>{launches++;};
  await assert.rejects(()=>runTask({...a,config:{...a.config,...change}})); assert.equal(launches,0);
 }
});
test('missing, forged, partial and changed prerequisite evidence blocks',async()=>{
 for(const kind of ['missing','forged','partial','hash','empty','timestamp']) {
  const a=await setup(),c=a.completions['AIBROWESE-004'];
  if(kind==='missing') delete a.completions['AIBROWESE-004'];
  if(kind==='forged') a.completions['AIBROWESE-004']=structuredClone(c);
  if(kind==='partial') c.criteria.pop();
  if(kind==='hash') await writeFile(join(a.root,'proof.txt'),'changed');
  if(kind==='empty') c.evidence=[];
  if(kind==='timestamp') c.verifiedAt='tomorrow';
  await assert.rejects(()=>preflight(a));
 }
});
test('manifest cannot remove prerequisites, reorder tasks, or invent fallback',async()=>{
 for(const alter of [m=>delete m.assignments[4].prerequisites,m=>m.assignments[4].prerequisites=[],m=>m.assignments[4].prerequisites=['AIBROWESE-999'],m=>m.assignments.reverse(),m=>m.assignments[4].fallbackRoute={model:'gpt-6-astra',reasoning:'high',mode:'standard',failedRepairsRequired:2},m=>delete m.assignments[4].acceptanceCriteria]) {
  const a=await setup(),m=structuredClone(manifest);alter(m);await assert.rejects(()=>preflight({...a,manifest:m}));
 }
});
test('fallback requires two distinct authenticated focused failures and explicit handoff',async()=>{
 const a=await setup();
 const failures=[1,2].map(n=>a.trust({trusted:true,taskId:a.taskId,attemptId:`repair${n}`,runId:`run${n}`,launcherObserved:a.config,status:'failed',focusedRepair:true,failingCase:'bounded regression',source:'test executor'}));
 const config={...a.config,reasoning:'high',fallback:{failedAttempts:['repair1','repair2'],failingCase:'bounded regression',reason:'two repairs failed'}};
 a.prior.push(...failures); assert.equal((await preflight({...a,config})).fallback.route.reasoning,'high');
 for(const history of [[],[failures[0]],[failures[0],failures[0]],[...failures,failures[0]],failures.map(r=>structuredClone(r))]) {a.prior.splice(0,a.prior.length,...history);await assert.rejects(()=>preflight({...a,config}));} a.prior.splice(0,a.prior.length,...failures);
 await assert.rejects(()=>preflight({...a,config:{...config,fallback:{}}}));
 const b=await setup('AIBROWESE-006'); await assert.rejects(()=>preflight({...b,config:{...b.config,model:'gpt-6.1-sol'}}));
});
test('trusted launcher settings and provider identity remain separate',async()=>{
 const a=await setup(); const result=await runTask({...a,completion:({runId})=>a.complete(a.task,runId)});
 assert.equal(result.done,true);assert.equal(result.actualModel,null);assert.deepEqual(result.usage,{input:null,output:null,cached:null,reasoning:null});assert.equal(result.cost,null);
 const records=(await readFile(a.auditPath,'utf8')).trim().split('\n').map(JSON.parse);
 assert.deepEqual(records.map(r=>r.event),['launch','attempt','completion']);assert.equal(records[1].modelProvenance,'trusted-launcher-receipt');assert.ok(records[1].measuredLatencyMs>=0);assert.ok(records.every(r=>r.timestamp));
});
test('missing, mismatched, contradictory or forged receipts cannot become Done',async()=>{
 for(const kind of ['missing','forged','model','provider','run','attempt','unknown','failed','usage','launch']) {
  const a=await setup(),launch=a.adapter.launch;
  a.adapter.launch=async r=>{
   if(kind==='launch') throw new Error('executor unavailable');
   const receipt=await launch(r);
   if(kind==='missing') return null;
   if(kind==='forged') return structuredClone(receipt);
   if(kind==='model') receipt.launcherObserved.reasoning='high';
   if(kind==='provider') {receipt.providerObserved={...receipt.launcherObserved,model:'other'};receipt.providerSource='provider';}
   if(kind==='run') receipt.runId='other';
   if(kind==='attempt') receipt.attemptId='other';
   if(kind==='unknown'||kind==='failed') receipt.status=kind;
   if(kind==='usage') receipt.usage={input:2,output:1,cached:3,reasoning:0};
   return receipt;
  };
  const r=await runTask({...a,completion:({runId})=>a.complete(a.task,runId)});assert.equal(r.done,false,kind);
  assert.equal((await readFile(a.auditPath,'utf8')).trim().split('\n').length,3);
 }
});
test('current-task completion must cover every criterion and same run',async()=>{
 for(const kind of ['missing','forged','partial','failed','run','evidence']) {
  const a=await setup(); const result=await runTask({...a,completion:({runId})=>{
   const c=a.complete(a.task,runId);
   if(kind==='missing') return undefined;
   if(kind==='forged') return structuredClone(c);
   if(kind==='partial') c.criteria.pop();
   if(kind==='failed') c.criteria[0].passed=false;
   if(kind==='run') c.runId='old-run';
   if(kind==='evidence') c.criteria[0].evidence=[99];return c;
  }}); assert.equal(result.done,false,kind);
 }
});
test('provider usage cost computes only supplied known rates, cached/reasoning are not double billed',async()=>{
 const rates={version:'test-v1',source:'test-only supplied rates',currency:'USD',asOf:'2026-10-02',models:{'test-model':{input:2,output:10,cached:1}}};
 const u={input:100,output:20,cached:40,reasoning:5};
 assert.equal(computeCost(u,'test-model',rates).amount,0.00036);
 assert.equal(computeCost({...u,cached:null},'test-model',rates),null);
 assert.equal(computeCost(u,null,rates),null);assert.equal(computeCost(u,'unknown',rates),null);
 assert.throws(()=>computeCost(u,'test-model',{...rates,source:''}));
 const a=await setup();a.config.rates=rates;const launch=a.adapter.launch;
 a.adapter.launch=async r=>{const receipt=await launch(r);receipt.providerObserved={...receipt.launcherObserved};receipt.providerSource='trusted provider receipt';receipt.usage={input:100,output:20,cached:40,reasoning:5};return receipt;};
 const result=await runTask({...a,completion:({runId})=>a.complete(a.task,runId)});assert.equal(result.actualModel,'gpt-6.1-sol');assert.equal(result.cost,null);
});
test('append-only audit preserves earlier records on another run',async()=>{
 const a=await setup();await runTask(a);const previous=await readFile(a.auditPath,'utf8');await runTask(a);assert.ok((await readFile(a.auditPath,'utf8')).startsWith(previous));
});
test('CLI rejects changed adapter pin before module import',async()=>{
 const a=await setup();await assert.rejects(()=>loadAdapter({trustedAdapter:{path:'proof.txt',sha256:'0'.repeat(64)}},a.root),/SHA256 mismatch/);
 await assert.rejects(()=>loadAdapter({},a.root),/Operator-pinned/);
});

test('authoritative history permits one primary repair and stops after failed fallback third repair',async()=>{
 const a=await setup();
 const failure=(id,route)=>a.trust({trusted:true,taskId:a.taskId,runId:id,attemptId:id,source:'test executor',launcherObserved:route,status:'failed',focusedRepair:true,failingCase:'regression'});
 a.prior.push(failure('one',a.config)); await preflight(a);
 a.prior.push(failure('two',a.config)); await assert.rejects(()=>preflight(a),/handoff required/);
 const config={...a.config,reasoning:'high',fallback:{failedAttempts:['one','two'],failingCase:'regression',reason:'two failed repairs'}};
 await preflight({...a,config}); a.prior.push(failure('three',config));
 await assert.rejects(()=>preflight({...a,config}),/Three failed/); await assert.rejects(()=>preflight(a),/Three failed/);
 await assert.rejects(()=>preflight({...a,config,history:[]}),/Three failed/);
});

test('same-model same-reasoning handoff still requires explicit fallback record',async()=>{
 const a=await setup();
 a.manifest=structuredClone(a.manifest);a.manifest.assignments[4].reasoning='high';a.config.reasoning='high';
 a.prior.push(...['one','two'].map(id=>a.trust({trusted:true,taskId:a.taskId,runId:id,attemptId:id,source:'test executor',launcherObserved:a.config,status:'failed',focusedRepair:true,failingCase:'regression'})));
 await assert.rejects(()=>preflight(a),/handoff required/);
 const config={...a.config,fallback:{failedAttempts:['one','two'],failingCase:'regression',reason:'scoped same-route handoff'}};
 assert.equal((await preflight({...a,config})).fallback.route.reasoning,'high');
});
