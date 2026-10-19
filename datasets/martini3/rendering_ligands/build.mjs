import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { fileURLToPath } from 'node:url';
import assert from 'node:assert/strict';

export const root = path.dirname(fileURLToPath(import.meta.url));
export const revision = '9a630c18934be270b4f77189722650f4397e4ef2';
export const repository = 'https://github.com/Martini-Force-Field-Initiative/M3-Small-Molecules';
export const specs = [
  { id: 'CAFF', molecule: 'LIG_CAFF', name: 'Caffeine', formula: { C: 8, H: 10, N: 4, O: 2 }, family: 'poly', count: 24, beads: 7 },
  { id: 'CLPR', molecule: 'LIG_CLPR', name: 'Chlorpropham', formula: { C: 10, H: 12, Cl: 1, N: 1, O: 2 }, family: 'mono', count: 26, beads: 5 },
  { id: 'INDO', molecule: 'LIG_INDO', name: 'Indole', formula: { C: 8, H: 7, N: 1 }, family: 'poly', count: 16, beads: 5 },
  { id: 'BZTA', molecule: 'LIG_BZTA', name: 'Benzothiazole', formula: { C: 7, H: 5, N: 1, S: 1 }, family: 'poly', count: 14, beads: 5 },
];

export const hash = data => crypto.createHash('sha256').update(data).digest('hex');
export const source = name => fs.readFileSync(path.join(root, 'upstream', name), 'utf8');
export const topologyFile = spec => `models_itps_opt-${spec.family}_${spec.id}.itp`;
export const mappingFile = spec => `${spec.id}_oplsaaTOcg_cgbuilder${spec.id === 'INDO' ? '_refined' : ''}.ndx`;

export function sections(text) {
  const result = {};
  let section = '';
  const enabled = [];
  for (let line of text.split('\n')) {
    line = line.split(';')[0].trim();
    if (line === '#ifndef FLEXIBLE') { enabled.push(true); continue; }
    if (line === '#endif') { enabled.pop(); continue; }
    if (!line || enabled.includes(false)) continue;
    if (line.startsWith('#')) throw new Error(`Unsupported directive ${line}`);
    const header = line.match(/^\[\s*(\w+)\s*\]$/);
    if (header) { section = header[1]; result[section] ??= []; }
    else (result[section] ??= []).push(line.split(/\s+/));
  }
  return result;
}

export function parsePDB(text) {
  return text.split('\n').filter(line => /^(ATOM  |HETATM)/.test(line)).map(line => ({
    serial: Number(line.slice(6, 11)),
    name: line.slice(12, 16).trim(),
    position: [30, 38, 46].map(start => Number(line.slice(start, start + 8)) / 10),
  }));
}

export function parseGro(text) {
  const lines = text.trimEnd().split('\n');
  const atoms = lines.slice(2, 2 + Number(lines[1])).map(line => ({
    residue: line.slice(5, 10).trim(),
    name: line.slice(10, 15).trim(),
    position: [20, 28, 36].map(start => Number(line.slice(start, start + 8))),
  }));
  assert.equal(atoms.length, Number(lines[1]));
  return atoms;
}

export const centroid = atoms => [0, 1, 2].map(axis => atoms.reduce((sum, atom) => sum + atom.position[axis], 0) / atoms.length);
const subtract = (a, b) => a.map((v, i) => v - b[i]);
const dot = (a, b) => a.reduce((s, v, i) => s + v * b[i], 0);
const unit = a => a.map(v => v / Math.hypot(...a));
const cross = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];

// Align the first three corresponding bead centroids without changing AA geometry.
function frame(points) {
  const x = unit(subtract(points[1], points[0]));
  const z = unit(cross(x, subtract(points[2], points[0])));
  return [x, cross(z, x), z];
}

export function renameTopology(spec, text) {
  let section = '';
  return text.split('\n').map(line => {
    const header = line.match(/^\s*\[\s*(\w+)\s*\]/);
    if (header) section = header[1];
    else if (section === 'moleculetype' && line.split(';')[0].trim()) {
      assert.equal(line.trim().split(/\s+/)[0], spec.id);
      return line.replace(spec.id, spec.molecule);
    }
    return line;
  }).join('\n');
}

function gro(title, groups) {
  let index = 0;
  const lines = groups.flatMap((group, i) => group.atoms.map(atom =>
    String(i + 1).padStart(5) + group.id.padEnd(5) + atom.name.padStart(5) +
    String(++index).padStart(5) + atom.position.map(v => v.toFixed(3).padStart(8)).join('')));
  return [title, String(index).padStart(5), ...lines, ' 102.00000   8.00000   8.00000', ''].join('\n');
}

export async function build({ fetchSources = false } = {}) {
  const lock = JSON.parse(fs.readFileSync(path.join(root, 'sources.json')));
  for (const entry of lock.files) {
    const filename = path.join(root, 'upstream', entry.file);
    if (fetchSources) {
      const response = await fetch(entry.url);
      if (!response.ok) throw new Error(`${response.status}: ${entry.url}`);
      const bytes = Buffer.from(await response.arrayBuffer());
      assert.equal(hash(bytes), entry.sha256, entry.file);
      fs.writeFileSync(filename, bytes);
    }
    assert.equal(hash(fs.readFileSync(filename)), entry.sha256, `Source checksum: ${entry.file}`);
  }
  const aaGroups = [], cgGroups = [], molecules = [];
  for (const [slot, spec] of specs.entries()) {
    const aa = parsePDB(source(`${spec.id}_LigParGen.pdb`));
    const cg = parseGro(source(`${spec.id}.gro`));
    const aaTopology = sections(source(`${spec.id}_LigParGen.itp`));
    const cgTopology = sections(source(topologyFile(spec)));
    assert.equal(aa.length, spec.count);
    assert.equal(cg.length, spec.beads);
    assert.deepEqual(aa.map(a => a.serial), aa.map((_, i) => i + 1));
    assert.deepEqual(aa.map(a => a.name), aaTopology.atoms.map(row => row[4]));
    assert.deepEqual(cg.map(a => a.name), cgTopology.atoms.map(row => row[4]));
    const composition = {};
    for (const atom of aa) {
      const element = atom.name.match(/^[A-Z][a-z]?/)[0];
      composition[element] = (composition[element] ?? 0) + 1;
    }
    assert.deepEqual(composition, spec.formula);
    const mapping = Object.entries(sections(source(mappingFile(spec)))).map(([group, rows], i) => {
      assert.equal(group, `B${i}`);
      const indices = rows.flat().map(Number);
      assert.ok(indices.length && indices.every(n => n >= 1 && n <= aa.length));
      return { group, bead: cg[i].name, aaIndices: indices };
    });
    assert.equal(mapping.length, cg.length);
    const covered = new Set(mapping.flatMap(group => group.aaIndices));
    for (const atom of aa.filter(atom => !atom.name.startsWith('H'))) assert.ok(covered.has(atom.serial));
    const mapped = mapping.map(group => centroid(group.aaIndices.map(i => aa[i - 1])));
    const from = frame(mapped), to = frame(cg.map(atom => atom.position));
    const aaCenter = centroid(aa), cgCenter = centroid(cg), center = [78 + 6 * slot, 3, 4];
    for (const atom of aa) {
      const local = from.map(axis => dot(subtract(atom.position, aaCenter), axis));
      atom.position = center.map((v, axis) => v + to.reduce((sum, basis, i) => sum + local[i] * basis[axis], 0));
    }
    for (const atom of cg) atom.position = atom.position.map((v, axis) => center[axis] + v - cgCenter[axis]);
    aaGroups.push({ id: spec.id, atoms: aa });
    cgGroups.push({ id: spec.id, atoms: cg });
    molecules.push({
      ...spec, center, smiles: source(`${spec.id}.smiles`).trim(), mapping,
      aaSource: `${spec.id}_LigParGen.pdb`, cgSource: `${spec.id}.gro`,
      topologySource: topologyFile(spec), mappingSource: mappingFile(spec),
      unmappedAAIndices: aa.filter(atom => !covered.has(atom.serial)).map(atom => atom.serial),
      sections: Object.fromEntries(Object.entries(cgTopology).map(([name, rows]) => [name, rows.length])),
    });
  }
  const outputs = {
    'ligands_aa.gro': gro('Published atomistic ligand counterparts: CAFF CLPR INDO BZTA', aaGroups),
    'ligands_cg.gro': gro('Published Martini 3 ligand counterparts: CAFF CLPR INDO BZTA', cgGroups),
    'selected_ligands.itp': specs.map(spec => renameTopology(spec, source(topologyFile(spec)))).join('\n'),
    'includes.itp': '#include "selected_ligands.itp"\n',
    'molecules.top': '; Append these rows inside [ molecules ], in GRO order.\n' + specs.map(spec => `${spec.molecule} 1\n`).join(''),
    'ligands.top': '#include "../martini_v3.0.0.itp"\n#include "includes.itp"\n\n[ system ]\nFour published Martini 3 ligands\n\n[ molecules ]\n' + specs.map(spec => `${spec.molecule} 1\n`).join(''),
    'manifest.json': JSON.stringify({
      repository, revision, coordinateUnits: 'nm', centerDefinition: 'arithmetic centroid of every AA atom / every CG site independently',
      orientation: 'AA rigidly aligned using frames from author mapping groups B0/B1/B2 and published CG sites 1/2/3; CG geometry only translated',
      molecules, totals: { aaAtoms: aaGroups.reduce((n, g) => n + g.atoms.length, 0), cgAtoms: cgGroups.reduce((n, g) => n + g.atoms.length, 0) },
    }, null, 2) + '\n',
  };
  for (const [name, text] of Object.entries(outputs)) fs.writeFileSync(path.join(root, name), text);
  return outputs;
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  await build({ fetchSources: process.argv.includes('--fetch') });
  console.log('Built four exact AA/CG identities: CAFF CLPR INDO BZTA (80 AA atoms / 22 CG sites).');
}
