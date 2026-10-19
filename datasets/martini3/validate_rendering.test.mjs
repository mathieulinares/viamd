import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import {fileURLToPath} from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const expectedLipids = ['CHL', 'POPC', 'DPPC', 'DOPC', 'POPE', 'DOPE',
    'POPS', 'POPG', 'POPA', 'SPM', 'TOG', 'CER'];
const expectedMolecules = JSON.parse(fs.readFileSync(path.join(here, 'rendering_molecules.json'), 'utf8'));
const layout = JSON.parse(fs.readFileSync(path.join(here, 'rendering_layout.json'), 'utf8'));

function gro(filename) {
    const lines = fs.readFileSync(filename, 'utf8').trimEnd().split('\n');
    const count = Number(lines[1]);
    assert.equal(lines.length, count + 3);
    const atoms = lines.slice(2, count + 2).map(line => ({
        label: line.slice(0, 15),
        residue: line.slice(0, 10),
        name: line.slice(5, 10).trim(),
        xyz: [20, 28, 36].map(start => Number(line.slice(start, start + 8))),
    }));
    for (const atom of atoms) assert(atom.xyz.every(Number.isFinite));
    return atoms;
}

function lipidBlocks(atoms) {
    return atoms.filter(atom => expectedLipids.includes(atom.name)).reduce((blocks, atom) => {
        if (blocks.at(-1)?.residue !== atom.residue) blocks.push({residue: atom.residue, name: atom.name, atoms: []});
        blocks.at(-1).atoms.push(atom);
        return blocks;
    }, []);
}

function runGmx(args) {
    const result = spawnSync('gmx', args, {encoding: 'utf8', maxBuffer: 30 * 1024 * 1024});
    assert.equal(result.status, 0, `${args.join(' ')}\n${result.stdout}\n${result.stderr}`);
    return result.stdout;
}

test('AA and CG contain the same 12 lipids in identical order and matching spatial slots', () => {
    const aa = lipidBlocks(gro(path.join(here, '../rendering_all_atom.gro')).slice(0, layout.baseAaAtoms));
    const cg = lipidBlocks(gro(path.join(here, '../rendering_martini_cg.gro')).slice(0, layout.baseCgAtoms));
    assert.deepEqual(aa.map(block => block.name), expectedLipids);
    assert.deepEqual(cg.map(block => block.name), expectedLipids);
    for (let index = 0; index < aa.length; index++) {
        for (let axis = 0; axis < 3; axis++) {
            const center = block => block.atoms.reduce((sum, atom) => sum + atom.xyz[axis], 0) / block.atoms.length;
            assert(Math.abs(center(aa[index]) - center(cg[index])) <= 0.00051, aa[index].name);
        }
    }
    assert.equal(gro(path.join(here, '../rendering_all_atom_all_lipids.gro')).length, 7449);
});

test('compiled TPR contains bonds, constraints, angles, dihedrals and virtual sites for actual molecules', () => {
    const dump = runGmx(['dump', '-s', path.join(here, '../rendering_martini_cg.tpr')]);
    assert.equal(Number(/natoms = (\d+)/.exec(dump)?.[1]), layout.cgAtoms);
    const pieces = dump.split(/^\s{3}moltype \(\d+\):/m).slice(1);
    const uniqueMolecules = [...new Map(expectedMolecules.map(entry => [entry.name, entry])).values()];
    assert.equal(pieces.length, uniqueMolecules.length);
    const totals = {bonds: 0, constraints: 0, angles: 0, dihedrals: 0, virtualSites: 0};
    const report = [];
    for (let index = 0; index < pieces.length; index++) {
        const piece = pieces[index];
        const name = /^\s+name="([^"]+)"/m.exec(piece)?.[1];
        const expected = uniqueMolecules.find(entry => entry.name === name);
        assert(expected, `Unexpected molecule ${name}`);
        const adjacency = Array.from({length: expected.atoms}, () => new Set());
        const counts = {bonds: 0, constraints: 0, angles: 0, dihedrals: 0, virtualSites: 0};
        for (const line of piece.split('\n')) {
            const entry = /^\s+\d+ type=\d+ \(([^)]+)\)\s+([\d\s]+)$/.exec(line);
            if (!entry) continue;
            const type = entry[1];
            const atoms = entry[2].trim().split(/\s+/).map(Number);
            let connects = false;
            if (['BONDS', 'G96BONDS', 'HARMONIC', 'MORSE'].includes(type)) {counts.bonds++; connects = true;}
            else if (type.startsWith('CONSTR')) {counts.constraints++; connects = true;}
            else if (type.startsWith('VSITE')) {counts.virtualSites++; connects = true;}
            else if (type.includes('ANGLE') || type === 'LINEAR_ANGLES') counts.angles++;
            else if (type.includes('DIH')) counts.dihedrals++;
            if (connects) for (const atom of atoms.slice(1)) {
                assert(atom >= 0 && atom < expected.atoms && atoms[0] >= 0 && atoms[0] < expected.atoms);
                adjacency[atoms[0]].add(atom);
                adjacency[atom].add(atoms[0]);
            }
        }
        assert(counts.bonds + counts.constraints > 0, `${expected.name}: no compiled molecular connectivity`);
        for (let atom = 0; atom < adjacency.length; atom++) {
            assert(adjacency[atom].size > 0, `${expected.name}: isolated bead ${atom + 1}`);
        }
        const instances = expectedMolecules.filter(entry => entry.name === name)
            .reduce((sum, entry) => sum + entry.count, 0);
        for (const key of Object.keys(totals)) totals[key] += counts[key] * instances;
        report.push({molecule: expected.name, atoms: expected.atoms, instances, ...counts});
    }
    for (const [key, value] of Object.entries(totals)) assert(value > 0, `No ${key} in compiled TPR`);
    console.log(JSON.stringify({totals, molecules: report}, null, 2));
});

test('TPR round-trip preserves all bead labels and coordinates', () => {
    const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'viamd-bonded-check-'));
    const output = path.join(temporary, 'roundtrip.gro');
    try {
        runGmx(['editconf', '-f', path.join(here, '../rendering_martini_cg.tpr'), '-o', output]);
        const actual = gro(output);
        const expected = gro(path.join(here, '../rendering_martini_cg.gro'));
        assert.equal(actual.length, expected.length);
        for (let index = 0; index < actual.length; index++) {
            assert.equal(actual[index].label, expected[index].label, `Atom ${index + 1}`);
            for (let axis = 0; axis < 3; axis++) {
                assert(Math.abs(actual[index].xyz[axis] - expected[index].xyz[axis]) < 0.0011);
            }
        }
    } finally {
        if (fs.existsSync(output)) fs.unlinkSync(output);
        fs.rmdirSync(temporary);
    }
});

test('zero-step GROMACS evaluation has finite energy and rejects severe overlaps', () => {
    const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'viamd-bonded-energy-'));
    try {
        const result = spawnSync('gmx', [
            'mdrun', '-s', path.join(here, '../rendering_martini_cg.tpr'),
            '-deffnm', path.join(temporary, 'sanity'), '-nt', '1',
        ], {encoding: 'utf8', maxBuffer: 1024 * 1024});
        assert.equal(result.status, 0, `${result.stdout}\n${result.stderr}`);
        const output = result.stdout + result.stderr;
        const energy = Number(/Potential Energy\s*=\s*([\deE+.-]+)/.exec(output)?.[1]);
        const force = Number(/Maximum force\s*=\s*([\deE+.-]+)/.exec(output)?.[1]);
        assert(Number.isFinite(energy) && Math.abs(energy) < 1e7, `Potential energy: ${energy}`);
        assert(Number.isFinite(force) && force < 1e7, `Maximum force: ${force}`);
        console.log(`Zero-step potential ${energy} kJ/mol; max force ${force} kJ/mol/nm`);
    } finally {
        for (const filename of fs.readdirSync(temporary)) fs.unlinkSync(path.join(temporary, filename));
        fs.rmdirSync(temporary);
    }
});
