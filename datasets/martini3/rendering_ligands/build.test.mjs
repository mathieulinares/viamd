import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import {
  root, specs, source, topologyFile, hash, sections, parsePDB, parseGro,
  centroid, renameTopology, build,
} from './build.mjs';

const read = name => fs.readFileSync(path.join(root, name), 'utf8');
const distance = (a, b) => Math.hypot(...a.position.map((v, i) => v - b.position[i]));

test('all pinned upstream files and the checksum list match their immutable source lock', () => {
  const lock = JSON.parse(read('sources.json'));
  for (const entry of lock.files) {
    assert.equal(hash(source(entry.file)), entry.sha256, entry.file);
    assert.ok(entry.url.includes(`/${lock.revision}/`));
  }
  assert.equal(read('SHA256SUMS'), lock.files.map(f => `${f.sha256}  upstream/${f.file}\n`).join(''));
});

test('builder reproduces the checked-in outputs byte for byte', async () => {
  const names = ['ligands_aa.gro', 'ligands_cg.gro', 'selected_ligands.itp', 'includes.itp', 'molecules.top', 'ligands.top', 'manifest.json'];
  const before = Object.fromEntries(names.map(name => [name, read(name)]));
  assert.deepEqual(await build(), before);
});

test('AA and CG identities, atom ordering, rigid source geometry and panel centroids match', () => {
  const aa = parseGro(read('ligands_aa.gro')), cg = parseGro(read('ligands_cg.gro'));
  const manifest = JSON.parse(read('manifest.json'));
  assert.equal(aa.length, 80);
  assert.equal(cg.length, 22);
  assert.deepEqual(manifest.molecules.map(m => m.id), specs.map(m => m.id));
  let aaOffset = 0, cgOffset = 0;
  for (const [i, spec] of specs.entries()) {
    const aaGroup = aa.slice(aaOffset, aaOffset += spec.count);
    const cgGroup = cg.slice(cgOffset, cgOffset += spec.beads);
    const aaOriginal = parsePDB(source(`${spec.id}_LigParGen.pdb`));
    const cgOriginal = parseGro(source(`${spec.id}.gro`));
    for (const [group, original] of [[aaGroup, aaOriginal], [cgGroup, cgOriginal]]) {
      assert.deepEqual(group.map(atom => atom.name), original.map(atom => atom.name));
      assert.ok(group.every(atom => atom.residue === spec.id));
      for (const [axis, value] of centroid(group).entries()) {
        assert.ok(Math.abs(value - [78 + 6 * i, 3, 4][axis]) < 0.00051);
      }
      for (let a = 0; a < group.length; ++a) for (let b = a + 1; b < group.length; ++b) {
        assert.ok(Math.abs(distance(group[a], group[b]) - distance(original[a], original[b])) < 0.0018);
      }
    }
    const aaAtoms = sections(source(`${spec.id}_LigParGen.itp`)).atoms;
    assert.equal(aaAtoms.length, spec.count);
    assert.ok(Math.abs(aaAtoms.reduce((q, row) => q + Number(row[6]), 0)) < 0.001);
    const mapping = manifest.molecules[i].mapping;
    assert.equal(mapping.length, spec.beads);
    assert.deepEqual(mapping.map(group => group.bead), cgGroup.map(atom => atom.name));
  }
});

test('every published topology parameter is unchanged; only molecule names are prefixed', () => {
  const expected = specs.map(spec => renameTopology(spec, source(topologyFile(spec)))).join('\n');
  assert.equal(read('selected_ligands.itp'), expected);
  const types = new Set(sections(fs.readFileSync(path.join(root, '../martini_v3.0.0.itp'), 'utf8')).atomtypes.map(row => row[0]));
  for (const spec of specs) {
    const original = sections(source(topologyFile(spec)));
    const renamed = sections(renameTopology(spec, source(topologyFile(spec))));
    assert.deepEqual(renamed, { ...original, moleculetype: [[spec.molecule, ...original.moleculetype[0].slice(1)]] });
    assert.ok(original.atoms.every(row => types.has(row[1])));
    assert.ok(original.constraints.length >= 3);
    assert.ok(original.dihedrals.length >= 1);
    assert.equal(original.atoms.length, spec.beads);
    if (spec.id === 'CAFF') {
      assert.equal(original.virtual_sites2.length, 1);
      assert.equal(original.virtual_sites3.length, 2);
    } else if (spec.id === 'CLPR') {
      assert.equal(original.bonds.length, 3);
      assert.equal(original.angles.length, 2);
    } else assert.equal(original.virtual_sitesn.length, 1);
  }
  assert.equal(read('molecules.top').split('\n').filter(line => line && !line.startsWith(';')).join('\n'),
    specs.map(spec => `${spec.molecule} 1`).join('\n'));
});

test('GROMACS compiles all four published models with the existing Martini 3 force field', t => {
  const available = spawnSync('gmx', ['--version'], { encoding: 'utf8' });
  if (available.error?.code === 'ENOENT') return t.skip('gmx is not installed; all offline fidelity checks still run');
  assert.equal(available.status, 0);
  const output = path.join(root, 'validation-output');
  fs.mkdirSync(output, { recursive: true });
  try {
    const result = spawnSync('gmx', [
      'grompp', '-f', 'validation.mdp', '-c', 'ligands_cg.gro', '-p', 'ligands.top',
      '-o', 'validation-output/ligands.tpr', '-po', 'validation-output/mdout.mdp',
      '-pp', 'validation-output/processed.top',
    ], { cwd: root, encoding: 'utf8', env: { ...process.env, GMX_MAXBACKUP: '-1' } });
    fs.writeFileSync(path.join(root, 'validation.log'), `${result.stdout ?? ''}${result.stderr ?? ''}`);
    assert.equal(result.status, 0, result.stderr);
    const dump = spawnSync('gmx', ['dump', '-s', 'validation-output/ligands.tpr'], { cwd: root, encoding: 'utf8' });
    assert.equal(dump.status, 0, dump.stderr);
    assert.match(dump.stdout, /natoms\s*=\s*22/);
    for (const spec of specs) assert.ok(dump.stdout.includes(spec.molecule), spec.molecule);
  } finally {
    fs.rmSync(output, { recursive: true, force: true });
  }
});
