// Deterministic public scheduling metadata; never supplied as policy features.
const fs=require('fs'),path=require('path');
const registry=require('../families/registry.json');
let text='#ifndef WEBNAV_UNIFIED_MANIFEST_H\n#define WEBNAV_UNIFIED_MANIFEST_H\n';
text+=`#define WU_FAMILY_COUNT ${Object.keys(registry.families).length}u\n#define WU_TASK_COUNT ${registry.registered_tasks}u\n`;
text+='typedef struct { const char *name,*library; unsigned lanes,tasks; unsigned global_tasks[16]; } WUFamilySpec;\n';
text+='static const WUFamilySpec wu_families[WU_FAMILY_COUNT]={\n';
for(const [name,f] of Object.entries(registry.families)) {
 const tasks=registry.tasks.filter(t=>t.family===name).sort((a,b)=>a.local_task-b.local_task);
 if(tasks.length!==f.task_count||tasks.some((t,i)=>t.local_task!==i)||tasks.length>16)throw Error('Noncontiguous task metadata '+name);
 const lanes=['click','tree','numeric'].includes(name)?8:4;
 text+=` {${JSON.stringify(name)},${JSON.stringify(f.library)},${lanes},${f.task_count},{${tasks.map(t=>t.id).join(',')}}},\n`;
}
text+='};\nstatic const char *const wu_task_names[WU_TASK_COUNT]={\n';
if(registry.tasks.length!==registry.registered_tasks)throw Error('Task count mismatch');
for(const t of [...registry.tasks].sort((a,b)=>a.id-b.id))text+=` ${JSON.stringify(t.name)},\n`;
text+='};\n#endif\n';
fs.writeFileSync(path.join(__dirname,'manifest.h'),text);
