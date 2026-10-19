import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import test from 'node:test';
import {fileURLToPath} from 'node:url';
import {gro, parseTopology} from './build.mjs';

const root = path.dirname(fileURLToPath(import.meta.url));
const text = filename => fs.readFileSync(path.join(root, filename), 'utf8');
const manifest = JSON.parse(text('manifest.json'));
const centroid = atoms => [0,1,2].map(axis => atoms.reduce((sum, atom) => sum + atom.xyz[axis], 0) / atoms.length);

test('retained final Zenodo snapshots and published molecular parameters have not been replaced', () => {
    assert.equal(manifest.archiveMd5, '708ee124d8739b0f99c6d84a297e9a11');
    for (const [filename, checksum] of Object.entries(manifest.sourceHashes)) {
        assert.equal(crypto.createHash('sha256').update(text(`upstream/${filename}`)).digest('hex'), checksum, filename);
    }
    assert.match(text(`upstream/${manifest.cgSnapshot}`).split('\n')[0], /t=\s*10000000\.00000/);
    assert.deepEqual(manifest.aaInventory, {
        PROA: 1, PROB: 1, PROC: 1, PROD: 1, PROE: 1, PROF: 1, POPE: 432, POPC: 612, POPI: 156,
    });
    assert.deepEqual(manifest.cgInventory, {molecule_0: 1, POPC: 1442, POPE: 1037, POPI: 375});
    const selected = text('selected.itp');
    assert(selected.includes(text('upstream/COARSE.GRAINED/molecule_0.itp')
        .replace(/^molecule_0(\s+1\s*)$/m, 'DEP_SND3$1')));
    for (const [name, filename] of [['POPC','martini_v3.0.0_phospholipids_v1.itp'],
        ['POPE','martini_v3.0.0_phospholipids_v1.itp'], ['POPI','martini_v3.0_phosphoinositides_v1.0.itp']]) {
        const block = text(`upstream/COARSE.GRAINED/${filename}`)
            .split(/(?=^[ \t]*\[\s*moleculetype\s*\])/m)
            .find(piece => new RegExp(`^[ \\t]*${name}\\s+1\\s*$`, 'm').test(piece));
        assert(block, name);
        assert(selected.includes(block.replace(new RegExp(`^([ \\t]*)${name}(\\s+1\\s*)$`, 'm'), `$1DEP_${name}$2`)));
    }
    assert(manifest.backboneRmsdNm < 1);
});

test('every displayed atom preserves the deposited snapshot modulo PBC and one rigid alignment', () => {
    for (const resolution of ['aa', 'cg']) {
        const original = gro(text(`upstream/${manifest[`${resolution}Snapshot`]}`));
        const output = gro(text(`system_${resolution}.gro`)).atoms;
        const molecules = manifest[`${resolution}Molecules`];
        const alignment = resolution === 'aa' ? manifest.aaAlignment :
            {rotation: [[1,0,0],[0,1,0],[0,0,1]], from: [0,0,0], to: [0,0,0]};
        let offset = 0;
        for (const molecule of molecules) {
            for (let i = 0; i < molecule.atoms; i++) {
                const source = original.atoms[molecule.sourceOffset + i], atom = output[offset + i];
                assert.equal(atom.atomName, source.atomName);
                assert.equal(atom.residueName, source.residueName);
                const relative = atom.xyz.map((value, axis) =>
                    value - manifest.centerNm[axis] + manifest.originNm[axis] - alignment.to[axis]);
                const back = [0,1,2].map(axis => alignment.from[axis] +
                    alignment.rotation.reduce((sum, row, index) => sum + row[axis] * relative[index], 0));
                for (let axis = 0; axis < 3; axis++) {
                    const delta = back[axis] - source.xyz[axis];
                    assert(Math.abs(delta - Math.round(delta / original.box[axis]) * original.box[axis]) < 0.0009,
                        `${resolution} ${offset + i + 1}: changed source coordinates`);
                }
            }
            offset += molecule.atoms;
        }
        assert.equal(offset, output.length);
        assert(output.every(atom => !['W','ION','SOD','CLA','TIP3'].includes(atom.residueName)));
    }
});

test('periodic molecules are whole and the simulated mixed bilayer retains distinct, disordered conformations', () => {
    const aaOutput = gro(text('system_aa.gro')).atoms;
    const aaSource = gro(text(`upstream/${manifest.aaSnapshot}`));
    const aaDefinitions = new Map([...new Set(manifest.aaMolecules.map(molecule => molecule.name))]
        .map(name => [name, parseTopology(text(`upstream/ALL.ATOM/toppar/${name}.itp`))]));
    let aaOffset = 0;
    for (const molecule of manifest.aaMolecules) {
        for (const [a,b] of aaDefinitions.get(molecule.name).edges) {
            const local = [0,1,2].map(axis => {
                const delta = aaSource.atoms[molecule.sourceOffset + b].xyz[axis] -
                    aaSource.atoms[molecule.sourceOffset + a].xyz[axis];
                return delta - Math.round(delta / aaSource.box[axis]) * aaSource.box[axis];
            });
            for (let axis = 0; axis < 3; axis++) {
                const expected = manifest.aaAlignment.rotation[axis]
                    .reduce((sum, value, index) => sum + value * local[index], 0);
                assert(Math.abs(aaOutput[aaOffset + b].xyz[axis] - aaOutput[aaOffset + a].xyz[axis] - expected) < 0.0011,
                    `${molecule.name} AA periodic break at ${a + 1}/${b + 1}`);
            }
        }
        aaOffset += molecule.atoms;
    }
    const output = gro(text('system_cg.gro')).atoms;
    const original = gro(text(`upstream/${manifest.cgSnapshot}`));
    const selected = text('selected.itp').split(/(?=^[ \t]*\[\s*moleculetype\s*\])/m)
        .filter(block => /^\s*\[\s*moleculetype\s*\]/.test(block));
    const definitions = new Map(selected.map(block =>
        [block.match(/^\s*(DEP_\w+)\s+1\s*$/m)?.[1], parseTopology(block)]));
    let offset = 0;
    const heads = [], tailVectors = [];
    for (const molecule of manifest.cgMolecules) {
        const atoms = output.slice(offset, offset + molecule.atoms);
        for (const [a, b] of definitions.get(molecule.name).edges) {
            for (let axis = 0; axis < 3; axis++) {
                const delta = original.atoms[molecule.sourceOffset + b].xyz[axis] -
                    original.atoms[molecule.sourceOffset + a].xyz[axis];
                const expected = delta - Math.round(delta / original.box[axis]) * original.box[axis];
                assert(Math.abs(atoms[b].xyz[axis] - atoms[a].xyz[axis] - expected) < 0.0011,
                    `${molecule.name} periodic break at ${a + 1}/${b + 1}`);
            }
        }
        if (molecule.singleResidue) {
            heads.push(atoms.find(atom => atom.atomName === 'PO4'));
            const c1 = atoms.find(atom => atom.atomName === 'C1A'), c4 = atoms.find(atom => atom.atomName === 'C4A');
            assert(c1 && c4);
            tailVectors.push(c4.xyz.map((value, axis) => (value - c1.xyz[axis]).toFixed(3)).join(','));
        }
        offset += molecule.atoms;
    }
    assert.equal(heads.length, 2854);
    for (const side of [-1,1]) {
        const leaflet = heads.filter(atom => (atom.xyz[2] - manifest.centerNm[2]) * side > 0);
        assert(leaflet.length > 1000);
        const mean = centroid(leaflet)[2];
        const roughness = Math.sqrt(leaflet.reduce((sum, atom) => sum + (atom.xyz[2] - mean) ** 2, 0) / leaflet.length);
        assert(roughness > 0.1, `Flattened leaflet: roughness ${roughness} nm`);
    }
    assert(new Set(tailVectors).size > 2500, 'Membrane was replaced by repeated ideal lipid conformers');
});

test('main scenes retain the isolated examples and ligands while replacing only the combined block', () => {
    const aa = gro(text('../../rendering_all_atom.gro')).atoms;
    const cg = gro(text('../../rendering_martini_cg.gro')).atoms;
    const layout = JSON.parse(text('../rendering_layout.json'));
    assert.equal(aa.length, 6645 + manifest.aaAtoms + 80);
    assert.equal(cg.length, 1611 + manifest.cgAtoms + 22);
    assert.equal(layout.membraneLipidsAa, 1200);
    assert.equal(layout.membraneLipidsCg, 2854);
    const removed = new Set(['CDL','LPC','GLC','GAL','GNG']);
    const base = gro(text('../../rendering_all_atom_all_lipids.gro')).atoms
        .filter(atom => !removed.has(atom.residueName));
    assert.deepEqual(aa.slice(0,6645), base);
    for (const [atoms, count] of [[aa,80],[cg,22]]) {
        const ligands = atoms.slice(-count);
        assert.deepEqual([...new Set(ligands.map(atom => atom.residueName))], ['CAFF','CLPR','INDO','BZTA']);
        for (const [index, name] of ['CAFF','CLPR','INDO','BZTA'].entries()) {
            const position = centroid(ligands.filter(atom => atom.residueName === name));
            [78+6*index,3,4].forEach((value,axis) => assert(Math.abs(position[axis]-value) < 0.00051));
        }
    }
    assert(aa.slice(6645).every(atom => atom.xyz[0] > 75));
});
