// Published parameters remain unchanged; only residue labels are adapted to the AA fixture.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import crypto from 'node:crypto';

const root = path.dirname(fileURLToPath(import.meta.url));
const upstream = path.join(root, 'upstream');
const lipidCommit = '2121527045696345069deb4d3df754724888a4f9';
const mirrorCommit = 'fcc507a98e0b4a005c73ea8055287c001f432cd2';
const specs = [
  ['CHL', 'CHOL', 'sterols_v1', 'Cholesterol'],
  ['POPC', 'POPC', 'phospholipids_PC_v2', '16:0/18:1 phosphatidylcholine'],
  ['DPPC', 'DPPC', 'phospholipids_PC_v2', '16:0/16:0 phosphatidylcholine'],
  ['DOPC', 'DOPC', 'phospholipids_PC_v2', '18:1/18:1 phosphatidylcholine'],
  ['POPE', 'POPE', 'phospholipids_PE_v2', '16:0/18:1 phosphatidylethanolamine'],
  ['DOPE', 'DOPE', 'phospholipids_PE_v2', '18:1/18:1 phosphatidylethanolamine'],
  ['POPS', 'POPS', 'phospholipids_PS_v2', '16:0/18:1 phosphatidylserine'],
  ['POPG', 'POPG', 'phospholipids_PG_v2', '16:0/18:1 phosphatidylglycerol'],
  ['POPA', 'POPA', 'phospholipids_PA_v2', '16:0/18:1 phosphatidic acid'],
  ['CDL', null, null, 'Cardiolipin: AA composition does not match published tetraoleoyl cardiolipin'],
  ['SPM', 'PSM', 'phospholipids_SM_v2', 'Palmitoyl sphingomyelin'],
  ['LPC', null, null, 'Lysophosphatidylcholine'],
  ['TOG', 'TO', 'triglycerides_v2', 'Triolein'],
  ['CER', 'PCER', 'ceramides_v2', 'Palmitoyl ceramide'],
  ['GLC', null, null, 'Glucocerebroside'],
  ['GAL', null, null, 'Galactocerebroside'],
  ['GNG', null, null, 'Ganglioside: AA oxygenation does not match published GM3'],
];

const aaFile = path.join(root, '../../rendering_all_atom_all_lipids.gro');
const aaText = fs.readFileSync(aaFile, 'utf8');
const aaLines = aaText.trimEnd().split('\n');
const aaCount = Number(aaLines[1]);
const aaResidues = [];
for (const line of aaLines.slice(2, aaCount + 2)) {
  const residueNumber = Number(line.slice(0, 5)), residue = line.slice(5, 10).trim();
  let group = aaResidues.at(-1);
  if (!group || group.residueNumber !== residueNumber || group.residue !== residue) {
    group = { residueNumber, residue, atoms: 0, composition: {}, sum: [0, 0, 0] };
    aaResidues.push(group);
  }
  ++group.atoms;
  const element = line.slice(10, 15).trim().replace(/^\d+/, '').match(/^[A-Z][a-z]?/)?.[0];
  if (!element) throw new Error(`Cannot infer AA element: ${line}`);
  group.composition[element] = (group.composition[element] ?? 0) + 1;
  for (let axis = 0; axis < 3; ++axis) group.sum[axis] += Number(line.slice(20 + axis * 8, 28 + axis * 8));
}
const aaStart = aaResidues.findIndex(group => group.residue === 'CHL');
const aaLipids = aaResidues.slice(aaStart);
if (aaStart < 0 || aaLipids.length !== specs.length || aaLipids.some((group, slot) => group.residue !== specs[slot][0])) {
  throw new Error('AA fixture must contain CHL followed by the requested 16 lipids in the original order');
}
const aaBox = aaLines[aaCount + 2];
const aaSource = {
  path: 'datasets/rendering_all_atom_all_lipids.gro',
  sha256: crypto.createHash('sha256').update(aaText).digest('hex'),
  centerDefinition: 'arithmetic centroid of all atoms in each AA residue',
};

const macros = new Map();
const knownTypes = new Set();
let baseSection = '';
for (const line of fs.readFileSync(path.join(root, '../martini_v3.0.0.itp'), 'utf8').split('\n')) {
  const header = line.match(/^\s*\[\s*(\w+)\s*\]/);
  if (header) baseSection = header[1];
  else if (baseSection === 'atomtypes' && line.trim() && !line.trim().startsWith(';')) knownTypes.add(line.trim().split(/\s+/)[0]);
}
const bonded = fs.readFileSync(path.join(upstream, 'martini_v3.0.0_ffbonded_v2.itp'), 'utf8');
for (const match of bonded.matchAll(/^#define\s+(\S+)\s+([^;\n]+)/gm)) {
  macros.set(match[1], match[2].trim().split(/\s+/));
}
const sourceName = key => key === 'gm3_final' ? 'gm3_final.itp' : `martini_v3.0.0_${key}.itp`;

function extract(file, name) {
  const lines = fs.readFileSync(path.join(upstream, file), 'utf8').split('\n');
  const starts = lines.flatMap((line, i) => /^\s*\[\s*moleculetype\s*\]/i.test(line) ? [i] : []);
  for (let n = 0; n < starts.length; ++n) {
    const block = lines.slice(starts[n], starts[n + 1] ?? lines.length);
    const definition = block.slice(1).find(line => line.split(';')[0].trim());
    if (definition?.trim().split(/\s+/)[0] !== name) continue;
    // Comments preceding the next molecule describe that next molecule, not this one.
    while (block.length && (!block.at(-1).trim() || block.at(-1).trim().startsWith(';'))) block.pop();
    return block.join('\n') + '\n';
  }
  throw new Error(`Missing published molecule ${name} in ${file}`);
}

function sections(text) {
  const result = {};
  let section = '';
  const enabled = [];
  for (let line of text.split('\n')) {
    line = line.split(';')[0].trim();
    if (line.startsWith('#ifdef')) { enabled.push(false); continue; }
    if (line === '#else') { enabled[enabled.length - 1] = !enabled.at(-1); continue; }
    if (line === '#endif') { enabled.pop(); continue; }
    if (enabled.includes(false) || !line || line.startsWith('#')) continue;
    const header = line.match(/^\[\s*(\w+)\s*\]/);
    if (header) { section = header[1]; result[section] ??= []; continue; }
    const tokens = line.split(/\s+/).flatMap(token => macros.get(token) ?? [token]);
    result[section].push(tokens);
  }
  return result;
}

const sub = (a, b) => a.map((v, i) => v - b[i]);
const add = (a, b) => a.map((v, i) => v + b[i]);
const scale = (a, t) => a.map(v => v * t);
const dot = (a, b) => a.reduce((sum, v, i) => sum + v * b[i], 0);
const norm = a => Math.hypot(...a);
const unit = a => scale(a, 1 / Math.max(norm(a), 1e-12));
const cross = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
const angle = (a, b, c) => Math.acos(Math.max(-1, Math.min(1, dot(unit(sub(a, b)), unit(sub(c, b))))));
const wrap = a => Math.atan2(Math.sin(a), Math.cos(a));
function torsion(a, b, c, d) {
  const axis = unit(sub(c, b)), ab = sub(a, b), cd = sub(d, c);
  const v = sub(ab, scale(axis, dot(ab, axis))), w = sub(cd, scale(axis, dot(cd, axis)));
  return Math.atan2(dot(cross(axis, v), w), dot(v, w));
}

function geometry(s, name) {
  const atoms = s.atoms, n = atoms.length;
  const virtual = new Set([...(s.virtual_sites3 ?? []), ...(s.virtual_sitesn ?? [])].map(row => +row[0] - 1));
  const real = atoms.flatMap((_, i) => virtual.has(i) ? [] : [i]);
  const edges = [...(s.bonds ?? []), ...(s.constraints ?? [])].map(row => [+row[0] - 1, +row[1] - 1, +row[3]]);
  const pos = Array.from({ length: n }, () => null);
  pos[real[0]] = [0, 0, 0];
  let seed = 1234567;
  const rand = () => ((seed = (1664525 * seed + 1013904223) >>> 0) / 4294967296);
  for (let loop = 0; loop < n; ++loop) {
    for (const [a, b, length] of edges) {
      if (!!pos[a] === !!pos[b]) continue;
      const from = pos[a] ? a : b, to = pos[a] ? b : a;
      const bead = atoms[to][4], tail = /^(?:[CDT]\d|AM)/.test(bead);
      const direction = unit([0.8 * (rand() - 0.5), 0.5 * (rand() - 0.5), tail ? -1 : 0.7 * (rand() - 0.5)]);
      pos[to] = add(pos[from], scale(direction, length));
    }
  }
  for (let i = 0; i < n; ++i) pos[i] ??= [rand(), rand(), rand()];
  const variables = real.flatMap(i => pos[i]);
  function unpack(x) {
    const p = Array.from({ length: n }, () => null);
    real.forEach((i, k) => { p[i] = x.slice(k * 3, k * 3 + 3); });
    for (const row of s.virtual_sites3 ?? []) {
      const [site, i, j, k, type, a, b, c] = row.map(Number);
      if (type !== 4) throw new Error(`Unsupported virtual site ${name}: ${type}`);
      const ij = sub(p[j - 1], p[i - 1]), ik = sub(p[k - 1], p[i - 1]);
      p[site - 1] = add(p[i - 1], add(add(scale(ij, a), scale(ik, b)), scale(cross(ij, ik), c)));
    }
    for (const row of s.virtual_sitesn ?? []) {
      const [site, type, ...indices] = row.map(Number);
      if (type !== 1) throw new Error('Only equal-weight virtual_sitesn is implemented');
      p[site - 1] = scale(indices.map(i => p[i - 1]).reduce(add, [0, 0, 0]), 1 / indices.length);
    }
    return p;
  }
  const connected = new Set();
  for (const [a, b] of edges) connected.add([a, b].sort((a, b) => a - b).join(','));
  for (const row of s.exclusions ?? []) for (const other of row.slice(1)) {
    connected.add([+row[0] - 1, +other - 1].sort((a, b) => a - b).join(','));
  }
  function energy(x) {
    const p = unpack(x);
    let e = 0;
    for (const row of s.bonds ?? []) e += 100 * (norm(sub(p[+row[0] - 1], p[+row[1] - 1])) - +row[3]) ** 2;
    for (const row of s.constraints ?? []) e += 20000 * (norm(sub(p[+row[0] - 1], p[+row[1] - 1])) - +row[3]) ** 2;
    for (const row of s.angles ?? []) {
      const theta = angle(...row.slice(0, 3).map(i => p[+i - 1]));
      const target = +row[4] * Math.PI / 180;
      e += 0.8 * (theta - target) ** 2;
    }
    for (const row of s.dihedrals ?? []) {
      const phi = torsion(...row.slice(0, 4).map(i => p[+i - 1]));
      const target = +row[5] * Math.PI / 180;
      if (+row[4] === 9) e += +row[6] * 0.002 * (1 + Math.cos(+row[7] * phi - target));
      else e += 0.2 * wrap(phi - target) ** 2;
    }
    for (const row of s.dihedral_restraints ?? []) {
      const phi = torsion(...row.slice(0, 4).map(i => p[+i - 1]));
      e += 0.1 * wrap(phi - +row[5] * Math.PI / 180) ** 2;
    }
    for (let a = 0; a < n; ++a) for (let b = a + 1; b < n; ++b) {
      if (virtual.has(a) || virtual.has(b) || connected.has(`${a},${b}`)) continue;
      const distance = norm(sub(p[a], p[b]));
      if (distance < 0.32) e += 10 * (distance - 0.32) ** 2;
    }
    return e;
  }
  // Deterministic finite-difference L-BFGS embeds the published bonded geometry.
  function gradient(x) {
    return x.map((value, i) => {
      x[i] = value + 1e-5; const plus = energy(x);
      x[i] = value - 1e-5; const minus = energy(x);
      x[i] = value;
      return (plus - minus) / 2e-5;
    });
  }
  let x = variables, g = gradient(x), e = energy(x);
  const history = [];
  for (let iteration = 0; iteration < 2500; ++iteration) {
    let q = [...g], alpha = [];
    for (let h = history.length - 1; h >= 0; --h) {
      const entry = history[h];
      alpha[h] = entry.rho * dot(entry.s, q);
      q = sub(q, scale(entry.y, alpha[h]));
    }
    const last = history.at(-1);
    let direction = scale(q, last ? dot(last.s, last.y) / dot(last.y, last.y) : 0.001);
    history.forEach((entry, h) => { direction = add(direction, scale(entry.s, alpha[h] - entry.rho * dot(entry.y, direction))); });
    direction = scale(direction, -1);
    if (dot(g, direction) >= 0) { direction = scale(g, -0.001); history.length = 0; }
    let step = 1, next, nextEnergy;
    for (let trial = 0; trial < 35; ++trial) {
      next = add(x, scale(direction, step)); nextEnergy = energy(next);
      if (Number.isFinite(nextEnergy) && nextEnergy <= e + 1e-4 * step * dot(g, direction)) break;
      step *= 0.5;
    }
    if (step < 1e-10) break;
    const nextGradient = gradient(next), dx = sub(next, x), dy = sub(nextGradient, g), curvature = dot(dx, dy);
    if (curvature > 1e-12) {
      history.push({ s: dx, y: dy, rho: 1 / curvature });
      if (history.length > 12) history.shift();
    }
    x = next; g = nextGradient; e = nextEnergy;
    if (norm(g) < 1e-5) break;
  }
  const p = unpack(x);
  // Point the average hydrocarbon tail down; translation later uses the full-bead centroid.
  const tails = atoms.flatMap((row, i) => /^[CDT]\d/.test(row[4]) ? [i] : []);
  if (tails.length) {
    const center = scale(p.reduce(add, [0, 0, 0]), 1 / n);
    const tailCenter = scale(tails.map(i => p[i]).reduce(add, [0, 0, 0]), 1 / tails.length);
    const z = unit(sub(center, tailCenter));
    const xx = unit(cross(Math.abs(z[1]) < 0.9 ? [0, 1, 0] : [1, 0, 0], z)), yy = cross(z, xx);
    for (let i = 0; i < n; ++i) p[i] = [dot(p[i], xx), dot(p[i], yy), dot(p[i], z)];
  }
  const errors = edges.map(([a, b, length]) => norm(sub(p[a], p[b])) - length);
  const constraints = (s.constraints ?? []).map(row => Math.abs(norm(sub(p[+row[0] - 1], p[+row[1] - 1])) - +row[3]));
  const angleErrors = (s.angles ?? []).map(row => Math.abs(angle(...row.slice(0, 3).map(i => p[+i - 1])) * 180 / Math.PI - +row[4]));
  if (Math.max(...errors.map(Math.abs)) > 0.05 || Math.max(0, ...constraints) > 0.002) {
    throw new Error(`Could not embed published geometry for ${name}`);
  }
  return { positions: p, geometry: {
    maxBondOrConstraintErrorNm: Math.max(...errors.map(Math.abs)),
    rmsBondOrConstraintErrorNm: Math.sqrt(dot(errors, errors) / errors.length),
    maxConstraintErrorNm: Math.max(0, ...constraints),
    maxAngleErrorDegrees: Math.max(0, ...angleErrors),
    objective: e,
  } };
}

let selected = '; Selected published Martini 3 molecules. See PROVENANCE.md.\n; Only [atoms] residue labels are changed; molecule names and all parameters are preserved.\n';
let serial = 0, groLines = [], moleculeLines = [], manifest = [];
for (const [slot, [label, molecule, key, description]] of specs.entries()) {
  const aa = aaLipids[slot], centerNm = aa.sum.map(value => value / aa.atoms);
  const correspondence = { aaResidueNumber: aa.residueNumber, aaAtoms: aa.atoms, aaElementCountsFromAtomNames: aa.composition };
  if (!molecule) {
    manifest.push({ slot: slot + 1, aaResidue: label, ...correspondence, description, status: 'unsupported', centerNm, atoms: 0 });
    continue;
  }
  const file = sourceName(key), original = extract(file, molecule), s = sections(original);
  for (const row of s.atoms) {
    if (!knownTypes.has(row[1])) throw new Error(`${molecule}: ${row[1]} is absent from the existing Martini 3.0.0 base force field`);
  }
  let section = '';
  const adapted = original.split('\n').map(line => {
    const header = line.match(/^\s*\[\s*(\w+)\s*\]/);
    if (header) section = header[1];
    if (section === 'atoms' && /^\s*\d+\s/.test(line)) {
      return line.replace(/^(\s*\d+\s+\S+\s+\d+\s+)\S+/, `$1${label}`);
    }
    return line;
  }).join('\n');
  selected += `\n; Source: ${file}; original molecule: ${molecule}; AA label: ${label}\n${adapted}`;
  const result = geometry(s, molecule), positions = result.positions;
  const centroid = scale(positions.reduce(add, [0, 0, 0]), 1 / positions.length);
  const placed = positions.map(p => add(sub(p, centroid), centerNm));
  // Distribute GRO rounding error so each centroid is as exact as 0.001-nm coordinates allow.
  const quantized = placed.map(p => p.map(value => Math.round(value * 1000)));
  for (let axis = 0; axis < 3; ++axis) {
    let difference = Math.round(centerNm[axis] * placed.length * 1000) - quantized.reduce((sum, p) => sum + p[axis], 0);
    const indices = placed.map((p, i) => ({ i, remainder: p[axis] * 1000 - quantized[i][axis] }))
      .sort((a, b) => difference > 0 ? b.remainder - a.remainder : a.remainder - b.remainder);
    for (const { i } of indices) {
      if (!difference) break;
      const adjustment = Math.sign(difference);
      quantized[i][axis] += adjustment;
      difference -= adjustment;
    }
    if (difference) throw new Error(`Could not balance GRO rounding for ${label}`);
  }
  s.atoms.forEach((row, i) => {
    serial += 1;
    groLines.push(`${String(slot + 1).padStart(5)}${label.padEnd(5)}${row[4].padStart(5)}${String(serial).padStart(5)}${quantized[i].map(v => (v / 1000).toFixed(3).padStart(8)).join('')}`);
  });
  moleculeLines.push(`${molecule.padEnd(8)} 1 ; slot ${slot + 1}: ${label}`);
  const source = key === 'gm3_final'
    ? `https://raw.githubusercontent.com/CCPBioSim/aiida-gromacs/${mirrorCommit}/examples/PTH2R_coarse-grained_files/gromacs/${file}`
    : `https://raw.githubusercontent.com/Martini-Force-Field-Initiative/M3-Lipid-Parameters/${lipidCommit}/ITPs/${file}`;
  manifest.push({ slot: slot + 1, aaResidue: label, ...correspondence, molecule, description, status: 'available', source, atoms: s.atoms.length,
    charge: s.atoms.reduce((sum, row) => sum + +row[6], 0), centerNm,
    topologyRecords: Object.fromEntries(['bonds', 'constraints', 'angles', 'dihedrals', 'dihedral_restraints', 'virtual_sites3', 'virtual_sitesn'].map(section => [section, (s[section] ?? []).length])),
    ...result.geometry });
  console.log(`${label.padEnd(4)} ${molecule.padEnd(4)} ${s.atoms.length} beads; max distance error ${result.geometry.maxBondOrConstraintErrorNm.toFixed(5)} nm`);
}
fs.writeFileSync(path.join(root, 'selected_lipids.itp'), selected);
fs.writeFileSync(path.join(root, 'lipids_available.gro'), ['Published Martini 3 lipids centered on AA residues; unsupported slots 10,12,15,16,17 omitted', String(serial), ...groLines, aaBox, ''].join('\n'));
fs.writeFileSync(path.join(root, 'molecules.top'), ['[ molecules ]', '; Published molecule order, preserving original 17-slot indices', ...moleculeLines, ''].join('\n'));
fs.writeFileSync(path.join(root, 'manifest.json'), JSON.stringify({ aaSource, requestedSlots: 17, availableMolecules: moleculeLines.length, atoms: serial, unsupported: specs.filter(spec => !spec[1]).map(spec => spec[0]), entries: manifest }, null, 2) + '\n');
const sums = fs.readdirSync(upstream).filter(f => fs.statSync(path.join(upstream, f)).isFile()).sort().map(f => {
  const hash = crypto.createHash('sha256').update(fs.readFileSync(path.join(upstream, f))).digest('hex');
  return `${hash}  upstream/${f}`;
});
fs.writeFileSync(path.join(root, 'SHA256SUMS'), sums.join('\n') + '\n');
