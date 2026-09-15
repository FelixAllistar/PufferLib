// Offline only. Runtime uses generated native tables, never a JS validator.
// Exhaustively validates every 1..4-move combination in each legal Gen1 pool.
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const root = __dirname;
const showdown = path.join(root, '../../build/pokemon/validation/node_modules/pokemon-showdown');
const {TeamValidator} = require(showdown);
const version = require(path.join(showdown, 'package.json')).version;
if (version !== '0.11.11') throw Error(`Expected pokemon-showdown 0.11.11, got ${version}`);
const pin = '9b88fd6c5467f703c38951d5b2e8a660314d410b';
const engine = path.join(root, `../../build/pokemon/engine-${pin}/src/lib/gen1/data/moves.zig`);
const source = fs.readFileSync(engine, 'utf8');
const sha = value => crypto.createHash('sha256').update(value).digest('hex');
function sourceFingerprint(directory) {
    const hash=crypto.createHash('sha256');
    function visit(relative) {
        for(const entry of fs.readdirSync(path.join(directory,relative),{withFileTypes:true}).sort((a,b)=>a.name.localeCompare(b.name,'en'))) {
            const name=path.join(relative,entry.name);
            if(entry.isDirectory())visit(name);
            else if(/\.(js|json)$/.test(name)) {hash.update(name);hash.update('\0');hash.update(fs.readFileSync(path.join(directory,name)));}
        }
    }
    visit('dist');return hash.digest('hex');
}
const validatorFingerprint=sourceFingerprint(showdown);
const typeSource=fs.readFileSync(path.join(path.dirname(engine),'types.zig'),'utf8');
const typeRows=[...typeSource.split('const CHART:')[1].split('const PRECEDENCE')[0].matchAll(/\[_\]Effectiveness\{([^}]+)\}/g)]
    .map(match=>match[1].split(',').map(x=>x.trim()).filter(Boolean).map(x=>({N:1,R:0.5,S:2,I:0})[x]));
if(typeRows.length!==15 || typeRows.some(row=>row.length!==15 || row.some(x=>x===undefined)))throw Error('Engine type chart parse mismatch');
const v = TeamValidator.get('gen1ou');
const dex = v.dex;
const types = ['Normal','Fighting','Flying','Poison','Ground','Rock','Bug','Ghost',
    'Fire','Water','Grass','Electric','Psychic','Ice','Dragon'];
const normalize = name => name.toLowerCase().replace(/[^a-z0-9]/g, '');
const effects = source.split('pub const Effect = enum(u8) {')[1].split('comptime {')[0]
    .replace(/\/\/[^\n]*/g, '').split(',').map(x => x.trim()).filter(Boolean);
const blocks = [...source.split('const DATA = [_]Data{')[1].split('\n    };')[0]
    .matchAll(/\/\/ ([A-Za-z0-9]+)\n\s*\.\{([\s\S]*?)\},/g)];
if (blocks.length !== 165) throw Error(`Unexpected engine move table: ${blocks.length}`);
const engineMoves = blocks.map(([, name, body], index) => {
    const field = key => body.match(new RegExp('\\.' + key + ' = ([^,]+),'))?.[1];
    const accuracy = field('accuracy');
    const percent = accuracy?.match(/^percent\((\d+)\)$/);
    const byte = percent ? Math.min(255, Math.floor(Number(percent[1]) * 255 / 100)) : Number(accuracy);
    const result = {id:index+1, name, effect:field('effect')?.slice(1), power:Number(field('bp')),
        accuracy:byte, type:field('type')?.slice(1), target:field('target')?.slice(1)};
    if (!Number.isFinite(byte) || !effects.includes(result.effect) || !types.includes(result.type))
        throw Error(`Unknown engine definition: ${JSON.stringify(result)}`);
    return result;
});
const moveById = new Map(dex.moves.all().filter(m => m.num > 0 && m.num <= 165).map(m => [m.num, m]));
const statusNames = ['slp','par','psn','tox','brn','frz','confusion','flinch'];
const statNames = ['atk','def','spe','spa','accuracy','evasion'];
const volatileNames = ['reflect','lightscreen','mist','focusenergy','substitute','leechseed',
    'bide','rage','partiallytrapped','confusion'];
const moveFeatureNames = ['present','power_div_250','accuracy_byte_div_256','always_hits',
    'max_pp_div_64','physical','special','status','target_self','target_other',
    'heal_fraction','drain_fraction','recoil_fraction','self_faint','self_sleep','cure_self_status',
    'priority_div_2','min_hits_div_5','max_hits_div_5','charge','recharge','recharge_ko_exception',
    'explode_defense_halved','high_critical','focus_energy_gen1_critical_bug','half_target_hp',
    'fixed_damage_div_100','level_damage','counter','bide','crash','sleep_required',
    'hp_cost_fraction','clear_boosts','transform','copy_move','random_move','disable','force_switch_or_escape',
    ...statusNames.map(x => 'target_' + x + '_nominal_probability'),
    ...statNames.map(x => 'target_' + x + '_stages_div_6'),
    ...statNames.map(x => 'self_' + x + '_stages_div_6'),
    ...volatileNames.map(x => 'volatile_' + x),
    ...types.map(x => 'type_' + x), ...effects.map(x => 'effect_' + x)];
const moveWidth = Math.ceil(moveFeatureNames.length / 8) * 8;
while (moveFeatureNames.length < moveWidth) moveFeatureNames.push('padding_' + moveFeatureNames.length);
function moveFeatures(m, e) {
    const f = Object.fromEntries(moveFeatureNames.map(key => [key, 0]));
    const set = (key, value) => { if (!(key in f) || !Number.isFinite(Number(value))) throw Error(key); f[key] = Number(value); };
    set('present',1); set('power_div_250',e.power/250); set('accuracy_byte_div_256',e.accuracy/256);
    set('always_hits',m.accuracy === true || e.effect === 'Swift');
    set('max_pp_div_64',Math.min(61,Math.floor(m.pp/5)*8)/64);
    // Category follows the Gen1 type split; fixed/special damage is identified separately.
    set(m.category === 'Status' ? 'status' : types.indexOf(e.type) < 8 ? 'physical' : 'special',1);
    set(e.target === 'Self' ? 'target_self' : 'target_other',1);
    const heal = e.effect === 'Heal' ? (m.id === 'rest' ? 1 : 0.5) : 0;
    set('heal_fraction',heal);
    set('drain_fraction',['DrainHP','DreamEater'].includes(e.effect) ? 0.5 : 0);
    set('recoil_fraction',m.recoil ? m.recoil[0]/m.recoil[1] : e.effect === 'Recoil' ? 0.25 : 0);
    set('self_faint',e.effect === 'Explode'); set('self_sleep',m.id === 'rest');
    set('cure_self_status',m.id === 'rest'); set('priority_div_2',m.priority/2);
    const hits = Array.isArray(m.multihit) ? m.multihit : [m.multihit || 1,m.multihit || 1];
    set('min_hits_div_5',hits[0]/5); set('max_hits_div_5',hits[1]/5);
    set('charge',e.effect === 'Charge'); set('recharge',e.effect === 'HyperBeam');
    set('recharge_ko_exception',e.effect === 'HyperBeam'); set('explode_defense_halved',e.effect === 'Explode');
    set('high_critical',e.effect === 'HighCritical'); set('focus_energy_gen1_critical_bug',e.effect === 'FocusEnergy');
    set('half_target_hp',e.effect === 'SuperFang');
    set('fixed_damage_div_100',typeof m.damage === 'number' ? m.damage/100 : 0);
    set('level_damage',m.damage === 'level'); set('counter',m.id === 'counter'); set('bide',m.id === 'bide');
    set('crash',e.effect === 'JumpKick'); set('sleep_required',e.effect === 'DreamEater');
    set('hp_cost_fraction',e.effect === 'Substitute' ? 0.25 : 0); set('clear_boosts',e.effect === 'Haze');
    set('transform',e.effect === 'Transform'); set('copy_move',['Mimic','MirrorMove'].includes(e.effect));
    set('random_move',e.effect === 'Metronome'); set('disable',e.effect === 'Disable');
    set('force_switch_or_escape',e.effect === 'SwitchAndTeleport');
    function event(event, probability, self) {
        if (!event) return;
        for (const status of statusNames) {
            if (!self && (event.status === status || event.volatileStatus === status))
                set('target_' + status + '_nominal_probability',probability);
        }
        for (const stat of statNames) if (event.boosts?.[stat])
            set((self ? 'self_' : 'target_') + stat + '_stages_div_6',event.boosts[stat]/6 * probability);
        if (event.self) eventHelper(event.self,probability,true);
    }
    const eventHelper = event;
    event(m,1,e.target === 'Self');
    for (const secondary of m.secondaries || []) event(secondary,(secondary.chance ?? 100)/100,false);
    for (const name of volatileNames) set('volatile_' + name,m.volatileStatus === name || m.self?.volatileStatus === name);
    set('type_' + e.type,1); set('effect_' + e.effect,1);
    return moveFeatureNames.map(name => f[name]);
}
const moves = [{id:0,name:'None',features:new Array(moveWidth).fill(0)}];
for (const e of engineMoves) {
    const m = moveById.get(e.id);
    if (!m || normalize(m.name).replace('vise','vice') !== normalize(e.name).replace('vise','vice'))
        throw Error(`Move identity mismatch ${e.id}: ${m?.name} / ${e.name}`);
    moves.push({...e,name:m.name,pp:m.pp,features:moveFeatures(m,e)});
}
const toSet = (species, ids) => ({species,moves:ids.map(id => moveById.get(id).name),level:100,
    evs:{hp:252,atk:252,def:252,spa:252,spd:252,spe:252},
    ivs:{hp:31,atk:31,def:31,spa:31,spd:31,spe:31}});
const speciesList = dex.species.all().filter(s => s.num >= 1 && s.num <= 149 && !s.forme)
    .sort((a,b) => a.num-b.num);
if (speciesList.length !== 149) throw Error('Expected exactly the 149 non-Uber Gen1 species');
const speciesFeatureNames = ['present','hp_div_255','atk_div_255','def_div_255','spe_div_255','spc_div_255',
    'base_critical_probability','legal_move_count_div_164',...types.map(t => 'type_' + t),
    ...moveFeatureNames.map(f => 'movepool_mean_' + f)];
const speciesWidth = Math.ceil(speciesFeatureNames.length/8)*8;
while(speciesFeatureNames.length < speciesWidth) speciesFeatureNames.push('padding_' + speciesFeatureNames.length);
const species = [{id:0,name:'None',moves:[],features:new Array(speciesWidth).fill(0)}];
for (const s of speciesList) {
    const pool = moves.slice(1,165).filter(m => !v.validateSet(toSet(s.name,[m.id]))).map(m => m.id);
    if (!pool.length) throw Error(`No legal moves: ${s.name}`);
    const f = [1,s.baseStats.hp/255,s.baseStats.atk/255,s.baseStats.def/255,s.baseStats.spe/255,s.baseStats.spa/255,
        Math.floor(s.baseStats.spe/2)/256,pool.length/164,...types.map(t => Number(s.types.includes(t))),
        ...moveFeatureNames.map((_,i) => pool.reduce((sum,id) => sum + moves[id].features[i],0)/pool.length)];
    while(f.length < speciesWidth)f.push(0);
    species.push({id:s.num,name:s.name,types:s.types,stats:s.baseStats,moves:pool,features:f});
}
let checked = 0;
const forbidden = [];
const audit = [];
function isSubset(small,large) { return small.every(x => large.includes(x)); }
if(process.argv.includes('--reuse-audit')) {
    const previous=JSON.parse(fs.readFileSync(path.join(root,'data/freepick.json'),'utf8'));
    const expected=previous.sha256;delete previous.sha256;
    if(sha(JSON.stringify(previous))!==expected || previous.showdown!==version ||
        previous.engine_moves_sha256!==sha(source) || previous.validator_source_sha256!==validatorFingerprint || previous.format!=='gen1ou' ||
        JSON.stringify(previous.species.map(s=>s.moves))!==JSON.stringify(species.map(s=>s.moves)))
        throw Error('Cannot reuse audit: its checksum, rules, or legal move pools changed');
    checked=previous.checked;forbidden.push(...previous.forbidden);audit.push(...previous.audit);
} else for (const s of species.slice(1)) {
    const bad = [];
    let count = 0, invalid = 0;
    for (let size=1;size<=Math.min(4,s.moves.length);size++) {
        function visit(start, chosen) {
            if (chosen.length === size) {
                const errors = v.validateSet(toSet(s.name,chosen));
                if(!errors && bad.some(b=>isSubset(b,chosen)))
                    throw Error(`Non-monotone legality: ${s.name} ${chosen}`);
                count++; checked++;
                if (errors) {
                    invalid++;
                    if (!bad.some(b => isSubset(b,chosen))) bad.push([...chosen]);
                }
                return;
            }
            for (let i=start;i<=s.moves.length-(size-chosen.length);i++) {
                chosen.push(s.moves[i]); visit(i+1,chosen); chosen.pop();
            }
        }
        visit(0,[]);
    }
    for (const set of bad) forbidden.push({species:s.id,moves:set});
    audit.push({species:s.id,checked:count,invalid,minimal_forbidden:bad.length});
    process.stderr.write(`${s.id}/149 ${s.name}: ${count} sets, ${invalid} invalid; total ${checked}\n`);
}
const metadata = {version:1,format:'gen1ou',showdown:version,validator_source_sha256:validatorFingerprint,
    engine:pin,engine_moves_sha256:sha(source),engine_types_sha256:sha(typeSource),type_chart:typeRows,
    level:100,stats:'maximum Gen1 DVs/stat experience/PP',move_feature_names:moveFeatureNames,
    species_feature_names:speciesFeatureNames,species,moves,forbidden,audit,checked};
const hash = sha(JSON.stringify(metadata));
metadata.sha256 = hash;
const float = value => {
    const text = Number(value).toPrecision(9);
    return (/[.e]/i.test(text) ? text : text + '.0') + 'f';
};
const matrix = rows => rows.map(row => '    {' + row.features.map(float).join(',') + '},').join('\n')+'\n';
const bitRows = species.map(s => {
    const words=[0n,0n,0n]; for(const m of s.moves)words[Math.floor(m/64)]|=1n<<BigInt(m%64);
    return '    {' + words.map(w=>'UINT64_C(0x'+w.toString(16)+')').join(',')+'},';
});
const generated = '// Generated by generate_freepick.cjs. Do not edit.\n';
const header = generated + '#pragma once\n#include <stdint.h>\n' +
    `#define PK_RULES_VERSION 1\n#define PK_RULES_SHA "${hash}"\n#define PK_MOVE_FEATURES ${moveWidth}\n#define PK_SPECIES_FEATURES ${speciesWidth}\n` +
    `#define PK_FORBIDDEN_COUNT ${forbidden.length}\n#define PK_LEGALITY_AUDITED_SETS ${checked}\n` +
    'static const uint64_t pk_legal_moves[150][3] = {\n'+bitRows.join('\n')+'\n};\n' +
    'typedef struct { uint8_t species, count, moves[4]; } PKForbiddenSet;\n' +
    'static const PKForbiddenSet pk_forbidden_sets[PK_FORBIDDEN_COUNT ? PK_FORBIDDEN_COUNT : 1] = {\n' +
    (forbidden.length ? forbidden.map(s=>'    {'+s.species+','+s.moves.length+',{'+s.moves.join(',')+'}},').join('\n') : '    {0,0,{0}},')+'\n};\n' +
    'static const char* const pk_species_names[150] = {\n'+species.map(s=>'    '+JSON.stringify(s.name)+',').join('\n')+'\n};\n' +
    'static const char* const pk_move_names[166] = {\n'+moves.map(m=>'    '+JSON.stringify(m.name)+',').join('\n')+'\n};\n' +
    'static const uint8_t pk_move_types[166] = {'+moves.map(m=>m.id?types.indexOf(m.type):0).join(',')+'};\n' +
    'static const uint8_t pk_move_pp[166] = {'+moves.map(m=>m.pp||0).join(',')+'};\n' +
    'static const uint8_t pk_species_types[150][2] = {\n'+species.map(s=>'    {'+(s.id?types.indexOf(s.types[0]):0)+','+(s.id?types.indexOf(s.types[1]||s.types[0]):0)+'},').join('\n')+'\n};\n' +
    'static const uint16_t pk_species_stats[150][5] = {\n'+species.map(s=>'    {'+(s.id?['hp','atk','def','spe','spa'].map(k=>2*s.stats[k]+(k==='hp'?203:98)).join(','):'0,0,0,0,0')+'},').join('\n')+'\n};\n';
const zigRules = generated + 'pub const sha = "' + hash + '";\n' +
    'pub const legal_moves = [_][3]u64{\n'+species.map(s=>{
        const words=[0n,0n,0n];for(const m of s.moves)words[Math.floor(m/64)]|=1n<<BigInt(m%64);
        return '    .{'+words.map(w=>'0x'+w.toString(16)).join(',')+'},';
    }).join('\n')+'\n};\n' +
    'pub const Forbidden = struct { species: u8, count: u8, moves: [4]u8 };\n' +
    'pub const forbidden = [_]Forbidden{\n'+forbidden.map(s=>'    .{.species='+s.species+',.count='+s.moves.length+',.moves=.{'+[...s.moves,...new Array(4-s.moves.length).fill(0)].join(',')+'}},').join('\n')+'\n};\n';
const outputs = {'freepick_rules.h':header,'freepick_rules.zig':zigRules,'move_features.inc':generated+matrix(moves),
    'species_features.inc':generated+matrix(species),'type_chart.inc':generated+typeRows.map(row=>'    {'+row.map(float).join(',')+'},').join('\n')+'\n',
    'data/freepick.json':JSON.stringify(metadata,null,2)+'\n'};
for(const [name,contents] of Object.entries(outputs)) {
    const target=path.join(root,name);
    if(process.argv.includes('--check')) {
        if(fs.readFileSync(target,'utf8')!==contents)throw Error(`Stale generated file: ${name}`);
    } else {
        fs.writeFileSync(target+'.tmp',contents);fs.renameSync(target+'.tmp',target);
    }
}
console.log(`Free-pick rules ${hash}: ${checked} full sets audited, ${forbidden.length} minimal forbidden combinations`);
