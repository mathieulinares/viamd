import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const output = path.resolve(process.argv[2] ?? '');
assert(process.argv[2], 'Provide a temporary output directory containing 1UBQ/1QJP/1AFO.pdb');
const lines = fs.readFileSync(path.join(here, '../rendering_all_atom_all_lipids.gro'), 'utf8').trimEnd().split('\n');
const count = Number(lines[1]);
assert.equal(lines.length, count + 3);
const atoms = lines.slice(2, count + 2).map(line => ({
    residue: Number(line.slice(0, 5)),
    name: line.slice(5, 10).trim(),
    atom: line.slice(10, 15).trim(),
    xyz: [20, 28, 36].map(start => Number(line.slice(start, start + 8))),
}));

function pdbAtom(atom, serial, chain) {
    const atomName = atom.atom.length < 4 ? ` ${atom.atom.padEnd(3)}` : atom.atom;
    return `ATOM  ${String(serial).padStart(5)} ${atomName} ${atom.name.padStart(3)} ${chain}${String(atom.residue).padStart(4)}    `
        + atom.xyz.map(value => (value * 10).toFixed(3).padStart(8)).join('')
        + '  1.00  0.00';
}

function writePdb(name, chains) {
    let serial = 1;
    const records = [];
    for (const [index, chain] of chains.entries()) {
        for (const atom of chain) records.push(pdbAtom(atom, serial++, String.fromCharCode(65 + index)));
        records.push('TER');
    }
    fs.writeFileSync(path.join(output, `${name}.pdb`), `${records.join('\n')}\nEND\n`);
}

const proteinNames = new Set('ALA ARG ASN ASP CYS GLN GLU GLY HIS ILE LEU LYS MET PHE PRO SER THR TRP TYR VAL'.split(' '));
const proteinFragments = [];
let previousResidue;
for (const atom of atoms) {
    if (!proteinNames.has(atom.name)) break;
    if (previousResidue === undefined || atom.residue < previousResidue || atom.residue > previousResidue + 1) {
        proteinFragments.push([]);
    }
    proteinFragments.at(-1).push(atom);
    previousResidue = atom.residue;
}
assert.equal(proteinFragments.length, 7, 'Expected ubiquitin, four OmpA fragments and two GpA chains');

function secondaryStructure(source, fragment) {
    const annotations = fs.readFileSync(path.join(output, `${source}.pdb`), 'utf8').split('\n');
    const assignment = new Map();
    for (const line of annotations) {
        if (line.startsWith('HELIX ')) {
            for (let residue = Number(line.slice(21, 25)); residue <= Number(line.slice(33, 37)); residue++) {
                assignment.set(residue, 'H');
            }
        } else if (line.startsWith('SHEET ')) {
            for (let residue = Number(line.slice(22, 26)); residue <= Number(line.slice(33, 37)); residue++) {
                assignment.set(residue, 'E');
            }
        }
    }
    return [...new Set(fragment.map(atom => atom.residue))].map(residue => assignment.get(residue) ?? 'C').join('');
}

const proteins = proteinFragments.map((fragment, index) => {
    const name = index === 0 ? 'ubiquitin' : index < 5 ? `ompa_${index}` : `gpa_${index - 4}`;
    const source = index === 0 ? '1UBQ' : index < 5 ? '1QJP' : '1AFO';
    writePdb(name, [fragment]);
    return {name, source, atoms: fragment.length, secondaryStructure: secondaryStructure(source, fragment)};
});

const dna = atoms.filter(atom => /^D[ACGT]$/.test(atom.name));
const dnaChains = [];
previousResidue = undefined;
for (const atom of dna) {
    if (previousResidue === undefined || atom.residue < previousResidue) dnaChains.push([]);
    dnaChains.at(-1).push(atom);
    previousResidue = atom.residue;
}
assert.equal(dnaChains.length, 1, 'The scene numbers DNA residues continuously across both strands');
const strands = [dna.filter(atom => atom.residue <= 12), dna.filter(atom => atom.residue > 12)];
assert(strands.every(strand => strand.length > 0));
writePdb('dna', strands);

const parentBases = {
    '2MG': 'G', H2U: 'U', M2G: 'G', OMC: 'C', OMG: 'G',
    YYG: 'G', PSU: 'U', '5MC': 'C', '7MG': 'G', '5MU': 'U', '1MA': 'A',
};
const rnaNames = new Set(['A', 'C', 'G', 'U', ...Object.keys(parentBases)]);
const rna = atoms.filter(atom => rnaNames.has(atom.name));
assert.equal(new Set(rna.map(atom => atom.residue)).size, 76);
const substitutions = [...new Map(rna.filter(atom => parentBases[atom.name]).map(atom => [
    atom.residue, {residue: atom.residue, original: atom.name, parent: parentBases[atom.name]},
])).values()];
writePdb('rna', [rna.map(atom => ({...atom, name: parentBases[atom.name] ?? atom.name}))]);
fs.writeFileSync(path.join(output, 'protein_inputs.json'), `${JSON.stringify(proteins, null, 2)}\n`);
fs.writeFileSync(path.join(output, 'rna_parent_bases.json'), `${JSON.stringify(substitutions, null, 2)}\n`);
console.log(JSON.stringify({proteins, dnaAtoms: dna.length, rnaAtoms: rna.length, rnaParentBases: substitutions}, null, 2));
