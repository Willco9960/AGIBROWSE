// Demonstration trust is deliberately restricted to the previously reviewed 004 record.
// No current-task executor or completion receipt is invented.
export default {
 id:'local-demonstration-no-executor-receipts',
 async getHistory() { return []; },
 async launch(route) { console.error(`Demo consumed ${route.taskId}: ${route.model}/${route.reasoning}/${route.mode}`); return null; },
 async verifyReceipt() { return null; },
 async verifyCompletion(record) {
  if(record?.taskId!=='AIBROWESE-004'||record.source!=='coordinator-reviewed-004-foundation') return null;
  return {...record,trusted:true};
 }
};
