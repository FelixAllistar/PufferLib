// Offline generator. Runtime never contacts Smogon or invokes JavaScript.
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const root = __dirname;
const source = path.join(root, 'data/smogon-gen1.json');
const manifestPath = path.join(root, 'data/source.json');
const {Dex, TeamValidator} = require('../../build/pokemon/validation/node_modules/pokemon-showdown');
const version = require('../../build/pokemon/validation/node_modules/pokemon-showdown/package.json').version;
if (version !== '0.11.11') throw Error(`Expected Showdown 0.11.11, got ${version}`);
const sha = x => crypto.createHash('sha256').update(x).digest('hex');
async function main() {
    if (process.argv.includes('--refresh')) {
        const url = 'https://data.pkmn.cc/sets/gen1.json';
        const response = await fetch(url);
        if (!response.ok) throw Error(`Fetch failed: ${response.status}`);
        const raw = await response.text();
        JSON.parse(raw);
        fs.mkdirSync(path.dirname(source), {recursive: true});
        fs.writeFileSync(source, raw);
        fs.writeFileSync(manifestPath, JSON.stringify({url, fetched_at: new Date().toISOString(), sha256: sha(raw),
            attribution: 'Smogon University and its contributors; distributed via pkmn/smogon. Set data is not covered by the API code MIT license.',
            documentation: 'https://pkmn.github.io/smogon/data/sets/', validator: 'pokemon-showdown@0.11.11 gen1ou'}, null, 2) + '\n');
    }
    const raw = fs.readFileSync(source, 'utf8');
    const provenance = JSON.parse(fs.readFileSync(manifestPath, 'utf8'));
    if (sha(raw) !== provenance.sha256) throw Error('Source snapshot hash mismatch');
    const dex = Dex.mod('gen1');
    const validator = TeamValidator.get('gen1ou');
    const banned = new Set(['doubleteam','minimize','horndrill','fissure','guillotine','dig','fly']);
    const candidates = [], rejected = [];
    const dataset = JSON.parse(raw);
    // Standard RBY tiers only: do not import Stadium, tradeback, rentals or
    // altered-movepool formats as if they were ordinary RBY recommendations.
    const formats = ['ou','uu','nu','pu','zu','lclevel100','lc','middlecup','ubers'];
    const inherited = {Metapod:'Caterpie', Kakuna:'Weedle'};
    for (const [evolution, prevo] of Object.entries(inherited)) dataset[evolution] = dataset[prevo];
    for (const [species, analyses] of Object.entries(dataset).sort((a,b)=>dex.species.get(a[0]).num-dex.species.get(b[0]).num)) {
        const mon = dex.species.get(species);
        if (mon.num < 1 || mon.num > 149) continue;
        const seen = new Set();
        for (const format of formats) {
        const templates = analyses[format] || {};
        for (const [template, set] of Object.entries(templates)) {
            let variants = [[]];
            for (const slot of set.moves) variants = variants.flatMap(v =>
                (Array.isArray(slot) ? slot : [slot]).map(move => [...v, dex.moves.get(move)]));
            let variant = 0;
            for (const moves of variants) {
                if (new Set(moves.map(m => m.id)).size !== moves.length) continue;
                const key = moves.map(m => m.num).sort((a,b) => a-b).join(',');
                if (seen.has(key)) continue;
                if (moves.some(m => !m.exists || m.num < 1 || m.num > 164 || banned.has(m.id))) {
                    rejected.push({species,format,template,moves:moves.map(m=>m.name),reason:'banned/invalid move'}); continue;
                }
                seen.add(key);
                const number = ++variant;
                candidates.push({species, species_id: mon.num, source_species:inherited[species] || species,
                    inherited:!!inherited[species], source_format:format, template, variant:number,
                    name: `${species} ${format} ${template.replace(/[^a-zA-Z0-9 -]/g,'')} ${number}`,
                    moves: moves.map(m => m.name), move_ids: moves.map(m => m.num)});
            }
        }
        }
    }
    // Validate each FULL expanded set in a Species-Clause legal team. This also
    // checks event/move combination legality, not merely individual learnability.
    const toSet = r => ({species:r.species, moves:r.moves, level:100,
        evs:{hp:252,atk:252,def:252,spa:252,spd:252,spe:252},
        ivs:{hp:31,atk:31,def:31,spa:31,spd:31,spe:31}});
    const records = [];
    for (const record of candidates) {
        const team = [record];
        for (const other of candidates.filter(r=>r.source_format==='ou')) {
            if (!team.some(r => r.species_id === other.species_id)) team.push(other);
            if (team.length === 6) break;
        }
        const errors = validator.validateTeam(team.map(toSet));
        if (errors) rejected.push({...record,reason:errors.join('; ')});
        else records.push(record);
    }
    for (let species = 1; species <= 149; species++) if (!records.some(r=>r.species_id===species))
        throw Error(`No validated sourced moveset for species ${species}`);
    if (records.length > 65534 || Math.max(...Array.from({length:149},(_,i)=>records.filter(r=>r.species_id===i+1).length))>160)
        throw Error('Catalog exceeds ABI limits');
    const generated = '// Generated by import_sets.cjs from pinned data/source.json. Do not edit.\n';
    const catalogHash = sha(JSON.stringify({level:100,stats:'maximum Gen 1 DVs/stat experience/PP',records}));
    const zig = generated + 'const pkmn = @import("pkmn");\nconst P = pkmn.gen1.helpers.Pokemon;\npub const sets = [_]P{\n' +
        records.map(r => `    .{ .species = @enumFromInt(${r.species_id}), .moves = &.{ ${r.move_ids.map(m => `@enumFromInt(${m})`).join(', ')} } },`).join('\n') +
        '\n};\npub const names = [_][:0]const u8{\n' + records.map(r => `    ${JSON.stringify(r.name)},`).join('\n') + '\n};\n';
    // Display-only metadata: deliberately excluded from the policy catalog hash.
    const labels = [''];
    const tiers = [];
    for (let id=1;id<=149;id++) {
        const record=records.find(r=>r.species_id===id);
        const tier=dex.species.get(record.species).tier;
        const ouStrategy=!!dataset[record.species]?.ou;
        const label=record.species.replace(/[^a-zA-Z0-9]/g,'')+'_'+tier+(tier!=='OU' && ouStrategy?'-SS':'');
        labels.push(label);
        tiers.push({species:record.species,species_id:id,tier,has_ou_strategy:ouStrategy,label});
    }
    const outputs = {'species_labels.h': generated+'#pragma once\nstatic const char* const pk_species_labels[150] = {\n'+labels.map(x=>'    '+JSON.stringify(x)+',').join('\n')+'\n};\n',
        'data/species_labels.json': JSON.stringify({tier_source:'pokemon-showdown@0.11.11 gen1 formats-data',strategy_source_sha256:sha(raw),ss_meaning:'Non-OU species with a published OU strategy; not an official tier',species:tiers},null,2)+'\n',
        'catalog.zig': zig, 'catalog_meta.h': generated + `#pragma once\n#define PK_SETS ${records.length}\n#define PK_CATALOG_SHA "${catalogHash}"\n`,
        'data/catalog.json': JSON.stringify({source_sha256:sha(raw), catalog_sha256:catalogHash, sets:records.map((r,id) => ({id,...r})), rejected},null,2)+'\n'};
    for (const [file, contents] of Object.entries(outputs)) {
        if (process.argv.includes('--check')) {
            if (fs.readFileSync(path.join(root,file),'utf8') !== contents) throw Error(`Stale generated file: ${file}`);
        } else fs.writeFileSync(path.join(root,file),contents);
    }
    console.log(`${records.length} sourced variants / ${new Set(records.map(r=>r.species)).size} species: full gen1ou validation passed`);
}
main().catch(e => {console.error(e);process.exit(1);});
