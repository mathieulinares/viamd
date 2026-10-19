import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const work = process.argv[2];
const proteins = JSON.parse(fs.readFileSync(
    path.join(work ?? path.join(here, 'rendering_proteins'), 'protein_inputs.json'), 'utf8'));

function topologyAtoms(filename) {
    let section = '';
    const atoms = [];
    for (const line of fs.readFileSync(filename, 'utf8').split('\n')) {
        const clean = line.split(';')[0].trim();
        const match = /^\[\s*(\w+)\s*\]/.exec(clean);
        if (match) section = match[1];
        else if (section === 'atoms' && clean && !clean.startsWith('#')) {
            const fields = clean.split(/\s+/);
            atoms.push({residue: Number(fields[2]), residueName: fields[3], atomName: fields[4]});
        }
    }
    assert(atoms.length > 0, `No topology atoms in ${filename}`);
    return atoms;
}

function mappedMolecule(name, filename, topology) {
    const coordinates = fs.readFileSync(filename, 'utf8').split('\n')
        .filter(line => /^(ATOM  |HETATM)/.test(line))
        .map(line => ({
            name: line.slice(12, 16).trim(),
            xyz: [30, 38, 46].map(start => Number(line.slice(start, start + 8)) / 10),
        }));
    const atoms = topologyAtoms(topology);
    assert.equal(coordinates.length, atoms.length, `${name}: topology/coordinate count`);
    for (let index = 0; index < atoms.length; index++) {
        assert.equal(coordinates[index].name, atoms[index].atomName, `${name}: atom ${index + 1}`);
        atoms[index].xyz = coordinates[index].xyz;
        assert(atoms[index].xyz.every(Number.isFinite));
    }
    return {name, atoms};
}

function storedMolecule(name, filename, topology) {
    const lines = fs.readFileSync(filename, 'utf8').trimEnd().split('\n');
    const count = Number(lines[1]);
    assert.equal(lines.length, count + 3);
    const atoms = topologyAtoms(topology);
    assert.equal(atoms.length, count);
    for (let index = 0; index < atoms.length; index++) {
        const line = lines[index + 2];
        assert.equal(line.slice(10, 15).trim(), atoms[index].atomName);
        atoms[index].xyz = [20, 28, 36].map(start => Number(line.slice(start, start + 8)));
    }
    return {name, atoms};
}

const molecules = [];
const extensionIncludes = [];
const pairedAaExtensions = [];
const deposited = path.join(here, 'rendering_deposited');
const depositedMetadata = JSON.parse(fs.readFileSync(path.join(deposited, 'manifest.json'), 'utf8'));
const sceneBox = depositedMetadata.boxNm;
for (const protein of proteins) {
    const name = `${protein.name}_0`;
    const topology = path.join(here, 'rendering_proteins', `${name}.itp`);
    const coordinateFile = path.join(here, 'rendering_proteins', `${protein.name}.gro`);
    const molecule = work
        ? mappedMolecule(name, path.join(work, `${protein.name}_cg.pdb`), topology)
        : storedMolecule(name, coordinateFile, topology);
    molecules.push(molecule);
    if (work) writeGro(coordinateFile, [molecule], [70, 40, 12]);
}
for (const name of ['dna', 'rna']) {
    const topology = path.join(here, 'rendering_nucleic', `${name}.itp`);
    if (name === 'dna') {
        let section = '';
        const normalized = fs.readFileSync(topology, 'utf8').split('\n').map(line => {
            const header = /^\s*\[\s*(\w+)\s*\]/.exec(line);
            if (header) section = header[1];
            if (section !== 'atoms' || !/^\s*\d/.test(line)) return line;
            const fields = line.trim().split(/\s+/);
            fields[3] = `D${fields[3].replace(/^D/, '')}`;
            return fields.join(' ');
        }).join('\n');
        fs.writeFileSync(topology, normalized);
    }
    const coordinateFile = path.join(here, 'rendering_nucleic', `${name}.gro`);
    const molecule = work
        ? mappedMolecule(`rendering_${name}`, path.join(work, `${name}_cg.pdb`), topology)
        : storedMolecule(`rendering_${name}`, coordinateFile, topology);
    if (name === 'dna') {
        for (const atom of molecule.atoms) atom.residueName = `D${atom.residueName.replace(/^D/, '')}`;
    }
    molecules.push(molecule);
    if (work) writeGro(coordinateFile, [molecule], [70, 40, 12]);
}

const lipidLines = fs.readFileSync(path.join(here, 'rendering_lipids/lipids_available.gro'), 'utf8')
    .trimEnd().split('\n');
const lipidRows = fs.readFileSync(path.join(here, 'rendering_lipids/molecules.top'), 'utf8').split('\n')
    .filter(line => line.trim() && !/^\s*[;[]/.test(line));
const lipids = [];
const lastPolymerResidue = molecules.at(-1).atoms.at(-1).residue;
let last;
for (const line of lipidLines.slice(2, Number(lipidLines[1]) + 2)) {
    const key = line.slice(0, 10);
    if (key !== last) lipids.push({name: lipidRows[lipids.length].trim().split(/\s+/)[0], atoms: [], singleResidue: true});
    lipids.at(-1).atoms.push({
        residue: lastPolymerResidue + lipids.length,
        residueName: line.slice(5, 10).trim(),
        atomName: line.slice(10, 15).trim(),
        xyz: [20, 28, 36].map(start => Number(line.slice(start, start + 8))),
    });
    last = key;
}
assert.equal(lipids.length, lipidRows.length);
molecules.push(...lipids);

function readGroAtoms(filename) {
    const lines = fs.readFileSync(filename, 'utf8').trimEnd().split('\n');
    const count = Number(lines[1]);
    assert.equal(lines.length, count + 3, filename);
    return lines.slice(2, count + 2).map(line => ({
        residue: Number(line.slice(0, 5)),
        residueName: line.slice(5, 10).trim(),
        atomName: line.slice(10, 15).trim(),
        xyz: [20, 28, 36].map(start => Number(line.slice(start, start + 8))),
    }));
}

function residueGroups(atoms) {
    const groups = [];
    let previous;
    for (const atom of atoms) {
        const key = `${atom.residue}:${atom.residueName}`;
        if (key !== previous) groups.push([]);
        groups.at(-1).push(atom);
        previous = key;
    }
    return groups;
}

const combinedStart = molecules.reduce((sum, molecule) => sum + molecule.atoms.length, 0);
const depositedCg = readGroAtoms(path.join(deposited, 'system_cg.gro'));
let depositedOffset = 0;
for (const entry of depositedMetadata.cgMolecules) {
    const atoms = depositedCg.slice(depositedOffset, depositedOffset + entry.atoms);
    assert.equal(atoms.length, entry.atoms);
    molecules.push({name: entry.name, atoms, singleResidue: entry.singleResidue});
    depositedOffset += entry.atoms;
}
assert.equal(depositedOffset, depositedCg.length);
extensionIncludes.push('rendering_deposited/selected.itp');
for (const atom of readGroAtoms(path.join(deposited, 'system_aa.gro'))) pairedAaExtensions.push(atom);

const ligandRoot = path.join(here, 'rendering_ligands');
const ligandMetadata = JSON.parse(fs.readFileSync(path.join(ligandRoot, 'manifest.json'), 'utf8'));
const ligandGroups = residueGroups(readGroAtoms(path.join(ligandRoot, 'ligands_cg.gro')));
assert.equal(ligandGroups.length, 4);
for (const [index, atoms] of ligandGroups.entries()) {
    molecules.push({name: ligandMetadata.molecules[index].molecule, atoms, singleResidue: true});
}
extensionIncludes.push('rendering_ligands/includes.itp');
pairedAaExtensions.push(...readGroAtoms(path.join(ligandRoot, 'ligands_aa.gro')));

const fullAa = fs.readFileSync(path.join(here, '../rendering_all_atom_all_lipids.gro'), 'utf8')
    .trimEnd().split('\n');
const allLipidNames = new Set(['CHL', 'POPC', 'DPPC', 'DOPC', 'POPE', 'DOPE', 'POPS', 'POPG',
    'POPA', 'CDL', 'SPM', 'LPC', 'TOG', 'CER', 'GLC', 'GAL', 'GNG']);
const selectedNames = new Set(lipids.map(lipid => lipid.atoms[0].residueName));
const pairedAa = fullAa.slice(2, Number(fullAa[1]) + 2)
    .filter(line => !allLipidNames.has(line.slice(5, 10).trim()) || selectedNames.has(line.slice(5, 10).trim()))
    .map((line, index) => line.slice(0, 15) + String(index + 1).padStart(5) + line.slice(20));
const baseAaAtoms = pairedAa.length;
let lastAaResidue = Math.max(...pairedAa.map(line => Number(line.slice(0, 5))));
for (const group of residueGroups(pairedAaExtensions)) {
    ++lastAaResidue;
    for (const atom of group) {
        pairedAa.push(String(lastAaResidue % 100000).padStart(5) + atom.residueName.padEnd(5)
            + atom.atomName.padStart(5) + String((pairedAa.length + 1) % 100000).padStart(5)
            + atom.xyz.map(value => value.toFixed(3).padStart(8)).join(''));
    }
}
fs.writeFileSync(path.join(here, '../rendering_all_atom.gro'), [
    'VIAMD paired AA rendering panel, deposited SND3 membrane complex and four ligands',
    String(pairedAa.length), ...pairedAa, sceneBox.map(value => value.toFixed(5)).join(' '), '',
].join('\n'));

function writeGro(filename, entries, box) {
    let serial = 0;
    let nextSingleResidue = Math.max(0, ...entries.filter(entry => !entry.singleResidue)
        .flatMap(entry => entry.atoms.map(atom => atom.residue)));
    const records = [];
    for (const molecule of entries) {
        const singleResidue = molecule.singleResidue ? ++nextSingleResidue : undefined;
        for (const atom of molecule.atoms) {
            const residue = singleResidue ?? atom.residue;
            records.push(String(residue % 100000).padStart(5) + atom.residueName.padEnd(5)
                + atom.atomName.padStart(5) + String(++serial % 100000).padStart(5)
                + atom.xyz.map(value => value.toFixed(3).padStart(8)).join(''));
        }
    }
    fs.writeFileSync(filename, ['VIAMD bonded Martini 3 rendering examples', String(serial), ...records,
        box.map(value => value.toFixed(5)).join(' '), ''].join('\n'));
}

writeGro(path.join(here, '../rendering_martini_cg.gro'), molecules, sceneBox);
const includes = [
    'martini_v3.0.0.itp',
    'rendering_nucleic/martini_alpha_dna.itp',
    'rendering_nucleic/martini_v3.0.0_rna.itp',
    'rendering_lipids/includes.itp',
    ...proteins.map(protein => `rendering_proteins/${protein.name}_0.itp`),
    'rendering_nucleic/dna.itp',
    'rendering_nucleic/rna.itp',
    ...extensionIncludes,
];
const moleculeRows = [];
for (const molecule of molecules) {
    const previous = moleculeRows.at(-1);
    if (previous?.name === molecule.name) previous.count++;
    else moleculeRows.push({name: molecule.name, count: 1, atoms: molecule.atoms.length});
}
const topology = [
    '; Published Martini molecular topologies; spatial rendering panel, not an equilibrated mixture.',
    ...includes.map(filename => `#include "${filename}"`),
    '', '[ system ]', 'VIAMD bonded Martini 3 rendering panel', '', '[ molecules ]',
    ...moleculeRows.map(molecule => `${molecule.name} ${molecule.count}`), '',
].join('\n');
fs.writeFileSync(path.join(here, 'rendering_visualization.top'), topology);
fs.writeFileSync(path.join(here, 'rendering_molecules.json'), JSON.stringify(
    moleculeRows, null, 2) + '\n');
fs.writeFileSync(path.join(here, 'rendering_layout.json'), JSON.stringify({
    baseAaAtoms, baseCgAtoms: combinedStart,
    aaAtoms: pairedAa.length, cgAtoms: molecules.reduce((sum, molecule) => sum + molecule.atoms.length, 0),
    combinedAaStart: baseAaAtoms, combinedCgStart: combinedStart,
    depositedAaAtoms: depositedMetadata.aaAtoms, depositedCgAtoms: depositedMetadata.cgAtoms,
    membraneLipidsAa: ['POPC', 'POPE', 'POPI'].reduce((sum, name) => sum + depositedMetadata.aaInventory[name], 0),
    membraneLipidsCg: ['POPC', 'POPE', 'POPI'].reduce((sum, name) => sum + depositedMetadata.cgInventory[name], 0),
    ligandAaAtoms: 80, ligandCgAtoms: 22, boxNm: sceneBox,
}, null, 2) + '\n');
console.log(`Assembled ${molecules.length} molecules, ${molecules.reduce((sum, molecule) => sum + molecule.atoms.length, 0)} beads.`);
