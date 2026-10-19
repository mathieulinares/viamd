import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import test from 'node:test';
import crypto from 'node:crypto';

const root = path.dirname(fileURLToPath(import.meta.url));
const read = name => fs.readFileSync(path.join(root, name), 'utf8');
const manifest = JSON.parse(read('manifest.json'));
const expectedLabels = ['CHL', 'POPC', 'DPPC', 'DOPC', 'POPE', 'DOPE', 'POPS', 'POPG', 'POPA', 'CDL', 'SPM', 'LPC', 'TOG', 'CER', 'GLC', 'GAL', 'GNG'];
const available = manifest.entries.filter(entry => entry.status === 'available');

function block(text, name) {
  return text.split(/(?=^\s*\[\s*moleculetype\s*\])/m).find(piece => {
    const lines = piece.split('\n').map(line => line.split(';')[0].trim()).filter(Boolean);
    return /^\[\s*moleculetype\s*\]/.test(lines[0] ?? '') && lines[1]?.split(/\s+/)[0] === name;
  });
}
function canonical(text, residue) {
  let section = '';
  return text.split('\n').map(line => {
    const clean = line.split(';')[0].trim();
    const header = clean.match(/^\[\s*(\w+)\s*\]/);
    if (header) { section = header[1]; return `[${section}]`; }
    const row = clean.split(/\s+/);
    if (residue && section === 'atoms' && /^\d+$/.test(row[0])) row[3] = residue;
    return row.join(' ');
  }).filter(Boolean).join('\n');
}
function atoms(text) {
  const section = text.match(/\[\s*atoms\s*\]([\s\S]*?)(?=\[|$)/)[1];
  return section.split('\n').map(line => line.split(';')[0].trim()).filter(Boolean).map(line => line.split(/\s+/));
}

test('available molecules preserve the requested ordering without substituting unsupported species', () => {
  assert.equal(manifest.requestedSlots, 17);
  assert.deepEqual(manifest.entries.map(entry => entry.aaResidue), expectedLabels);
  assert.deepEqual(manifest.unsupported, ['CDL', 'LPC', 'GLC', 'GAL', 'GNG']);
  assert.equal(available.length, 12);
  assert.equal(manifest.atoms, 142);
  assert.deepEqual(read('molecules.top').split('\n').map(line => line.split(';')[0].trim()).filter(line => line && !line.startsWith('[')).map(line => line.split(/\s+/)[0]), available.map(entry => entry.molecule));
});

test('published topologies are unchanged except for the explicitly documented residue labels', () => {
  const selected = read('selected_lipids.itp');
  for (const entry of available) {
    const filename = new URL(entry.source).pathname.split('/').at(-1);
    const original = block(read(`upstream/${filename}`), entry.molecule);
    const derived = block(selected, entry.molecule);
    assert.ok(original && derived, entry.molecule);
    assert.equal(canonical(derived), canonical(original, entry.aaResidue), entry.molecule);
    assert.equal(atoms(derived).length, entry.atoms);
  }
});

test('GRO contains every published bead at the corresponding AA centroid', () => {
  const lines = read('lipids_available.gro').trimEnd().split('\n');
  const count = Number(lines[1]);
  assert.equal(count, manifest.atoms);
  assert.equal(lines.length, count + 3);
  let offset = 2;
  for (const entry of available) {
    const rows = atoms(block(read('selected_lipids.itp'), entry.molecule));
    const centroid = [0, 0, 0];
    for (const row of rows) {
      const line = lines[offset++];
      assert.equal(Number(line.slice(0, 5)), entry.slot);
      assert.equal(line.slice(5, 10).trim(), entry.aaResidue);
      assert.equal(line.slice(10, 15).trim(), row[4]);
      assert.equal(Number(line.slice(15, 20)), offset - 2);
      for (let axis = 0; axis < 3; ++axis) {
        const value = Number(line.slice(20 + axis * 8, 28 + axis * 8));
        assert.ok(Number.isFinite(value));
        centroid[axis] += value / rows.length;
      }
    }
    centroid.forEach((value, axis) => assert.ok(Math.abs(value - entry.centerNm[axis]) <= 0.00050001 / rows.length));
    assert.ok(entry.maxBondOrConstraintErrorNm < 0.05);
    assert.ok(entry.maxConstraintErrorNm < 0.002);
  }
});

test('placement uses the current AA fixture with cholesterol separate and a four-column lipid layout', () => {
  const aaText = fs.readFileSync(path.join(root, '../../rendering_all_atom_all_lipids.gro'), 'utf8');
  assert.equal(crypto.createHash('sha256').update(aaText).digest('hex'), manifest.aaSource.sha256);
  const lines = aaText.trimEnd().split('\n'), count = Number(lines[1]);
  const groups = [];
  for (const line of lines.slice(2, count + 2)) {
    const name = line.slice(5, 10).trim(), id = Number(line.slice(0, 5));
    let group = groups.at(-1);
    if (!group || group.name !== name || group.id !== id) {
      group = { name, id, coordinates: [] };
      groups.push(group);
    }
    group.coordinates.push([0, 1, 2].map(axis => Number(line.slice(20 + axis * 8, 28 + axis * 8))));
  }
  const lipids = groups.slice(groups.findIndex(group => group.name === 'CHL'));
  assert.deepEqual(lipids.map(group => group.name), expectedLabels);
  manifest.entries.forEach((entry, slot) => {
    assert.equal(entry.aaResidueNumber, lipids[slot].id);
    for (let axis = 0; axis < 3; ++axis) {
      const center = lipids[slot].coordinates.reduce((sum, p) => sum + p[axis], 0) / lipids[slot].coordinates.length;
      assert.equal(entry.centerNm[axis], center);
    }
  });
  assert.ok(manifest.entries[0].centerNm[0] < Math.min(...manifest.entries.slice(1).map(entry => entry.centerNm[0])));
  for (let row = 0; row < 4; ++row) {
    const entries = manifest.entries.slice(1 + row * 4, 5 + row * 4);
    assert.equal(entries.length, 4);
    for (let column = 1; column < 4; ++column) assert.ok(entries[column].centerNm[0] > entries[column - 1].centerNm[0]);
    assert.ok(Math.max(...entries.map(entry => entry.centerNm[1])) - Math.min(...entries.map(entry => entry.centerNm[1])) < 1);
  }
  assert.equal(read('lipids_available.gro').trimEnd().split('\n').at(-1), lines[count + 2]);
});

test('vendored source checksums match their pinned downloads', () => {
  for (const line of read('SHA256SUMS').trim().split('\n')) {
    const [hash, file] = line.split('  ');
    assert.equal(crypto.createHash('sha256').update(fs.readFileSync(path.join(root, file))).digest('hex'), hash, file);
  }
});
