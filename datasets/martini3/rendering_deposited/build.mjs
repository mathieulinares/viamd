import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const root = path.dirname(fileURLToPath(import.meta.url));
const upstream = path.join(root, 'upstream');
const center = [97, 25, 12];
const box = [122, 52, 36];
const hashes = {};
function source(filename) {
    const data = fs.readFileSync(path.join(upstream, filename), 'utf8');
    hashes[filename] = crypto.createHash('sha256').update(data).digest('hex');
    return data;
}

export function parseTopology(text) {
    let section = '';
    const atoms = [], edges = [];
    for (const row of text.split('\n')) {
        const clean = row.split(';')[0].trim();
        const header = /^\[\s*(\w+)\s*\]/.exec(clean);
        if (header) {section = header[1]; continue;}
        if (!/^\d/.test(clean)) continue;
        const fields = clean.split(/\s+/);
        if (section === 'atoms') atoms.push({
            residue: Number(fields[2]), residueName: fields[3], atomName: fields[4],
        });
        if (['bonds', 'constraints'].includes(section)) edges.push(fields.slice(0, 2).map(n => Number(n) - 1));
        if (section.startsWith('virtual_sites')) {
            const partners = section === 'virtual_sitesn' ? fields.slice(2) :
                fields.slice(1, 1 + Number(section.slice('virtual_sites'.length)));
            for (const partner of partners) edges.push([Number(fields[0]) - 1, Number(partner) - 1]);
        }
    }
    assert(atoms.length > 0, 'Empty topology');
    return {atoms, edges};
}

function moleculeBlocks(text) {
    return new Map(text.split(/(?=^[ \t]*\[\s*moleculetype\s*\])/m).slice(1).map(block => {
        const clean = block.split('\n').map(line => line.split(';')[0].trim())
            .filter(line => line && !line.startsWith('#'));
        return [clean[1].split(/\s+/)[0], block];
    }));
}

function rows(text) {
    const section = text.split(/\[\s*molecules\s*\]/)[1];
    assert(section, 'No molecule inventory');
    return section.split('\n').map(line => line.split(';')[0].trim()).filter(Boolean)
        .map(line => {const [name, count] = line.split(/\s+/); return {name, count: Number(count)};});
}

export function gro(text) {
    const lines = text.trimEnd().split('\n'), count = Number(lines[1]);
    assert.equal(lines.length, count + 3);
    return {
        title: lines[0], box: lines.at(-1).trim().split(/\s+/).map(Number),
        atoms: lines.slice(2, count + 2).map(line => ({
            residue: Number(line.slice(0, 5)), residueName: line.slice(5, 10).trim(),
            atomName: line.slice(10, 15).trim(),
            xyz: [20, 28, 36].map(start => Number(line.slice(start, start + 8))),
        })),
    };
}

const centroid = atoms => [0, 1, 2].map(axis =>
    atoms.reduce((sum, atom) => sum + atom.xyz[axis], 0) / atoms.length);
const minimumImage = (value, length) => value - Math.round(value / length) * length;

function whole(atoms, edges, sourceBox, reference) {
    const adjacency = atoms.map(() => []);
    for (const [a, b] of edges) {
        assert(a >= 0 && b >= 0 && a < atoms.length && b < atoms.length);
        adjacency[a].push(b);
        adjacency[b].push(a);
    }
    const visited = new Set();
    const output = atoms.map(atom => ({...atom, xyz: [...atom.xyz]}));
    for (let seed = 0; seed < atoms.length; seed++) {
        if (visited.has(seed)) continue;
        output[seed].xyz = output[seed].xyz.map((value, axis) =>
            reference[axis] + minimumImage(value - reference[axis], sourceBox[axis]));
        const queue = [seed];
        visited.add(seed);
        for (let index = 0; index < queue.length; index++) {
            const parent = queue[index];
            for (const child of adjacency[parent]) {
                if (visited.has(child)) continue;
                output[child].xyz = atoms[child].xyz.map((value, axis) => output[parent].xyz[axis]
                    + minimumImage(value - atoms[parent].xyz[axis], sourceBox[axis]));
                visited.add(child);
                queue.push(child);
            }
        }
    }
    const midpoint = centroid(output);
    const shift = midpoint.map((value, axis) =>
        Math.round((reference[axis] - value) / sourceBox[axis]) * sourceBox[axis]);
    for (const atom of output) atom.xyz = atom.xyz.map((value, axis) => value + shift[axis]);
    return output;
}

function inventory(snapshot, topologyRows, definitions) {
    const reference = snapshot.box.map(value => value / 2);
    const molecules = [];
    let offset = 0;
    for (const entry of topologyRows) {
        if (['W', 'NA', 'CL', 'SOD', 'CLA', 'TIP3'].includes(entry.name)) break;
        const topology = parseTopology(definitions.get(entry.name));
        for (let instance = 0; instance < entry.count; instance++) {
            const atoms = snapshot.atoms.slice(offset, offset + topology.atoms.length);
            assert.equal(atoms.length, topology.atoms.length);
            for (const [i, atom] of atoms.entries()) {
                assert.equal(atom.atomName, topology.atoms[i].atomName, `${entry.name} atom ${i + 1}`);
                assert.equal(atom.residueName, topology.atoms[i].residueName);
            }
            molecules.push({sourceName: entry.name, sourceOffset: offset,
                atoms: whole(atoms, topology.edges, snapshot.box, reference),
                lipid: ['POPC', 'POPE', 'POPI'].includes(entry.name)});
            offset += atoms.length;
        }
    }
    assert(snapshot.atoms.slice(offset).every(atom =>
        ['W', 'ION', 'SOD', 'CLA', 'TIP3'].includes(atom.residueName)), 'Unexpected omitted species');
    return molecules;
}

// Horn's quaternion fit, with Jacobi diagonalization of the symmetric 4x4 matrix.
function fit(moving, target) {
    assert.equal(moving.length, target.length);
    const from = centroid(moving), to = centroid(target);
    const s = Array.from({length: 3}, () => [0, 0, 0]);
    for (let index = 0; index < moving.length; index++) {
        for (let i = 0; i < 3; i++) for (let j = 0; j < 3; j++) {
            s[i][j] += (moving[index].xyz[i] - from[i]) * (target[index].xyz[j] - to[j]);
        }
    }
    const [[a,b,c],[d,e,f],[g,h,i]] = s;
    const matrix = [[a+e+i,f-h,g-c,b-d],[f-h,a-e-i,b+d,g+c],
        [g-c,b+d,e-a-i,f+h],[b-d,g+c,f+h,i-a-e]];
    const vectors = Array.from({length: 4}, (_, row) =>
        Array.from({length: 4}, (_, col) => Number(row === col)));
    for (let iteration = 0; iteration < 100; iteration++) {
        let p = 0, q = 1;
        for (let row = 0; row < 4; row++) for (let col = row + 1; col < 4; col++) {
            if (Math.abs(matrix[row][col]) > Math.abs(matrix[p][q])) {p = row; q = col;}
        }
        if (Math.abs(matrix[p][q]) < 1e-10) break;
        const angle = 0.5 * Math.atan2(2 * matrix[p][q], matrix[q][q] - matrix[p][p]);
        const cosine = Math.cos(angle), sine = Math.sin(angle);
        const pp = matrix[p][p], qq = matrix[q][q], pq = matrix[p][q];
        for (let row = 0; row < 4; row++) {
            if (row === p || row === q) continue;
            const rp = matrix[row][p], rq = matrix[row][q];
            matrix[row][p] = matrix[p][row] = cosine * rp - sine * rq;
            matrix[row][q] = matrix[q][row] = sine * rp + cosine * rq;
        }
        matrix[p][p] = cosine*cosine*pp - 2*sine*cosine*pq + sine*sine*qq;
        matrix[q][q] = sine*sine*pp + 2*sine*cosine*pq + cosine*cosine*qq;
        matrix[p][q] = matrix[q][p] = 0;
        for (let row = 0; row < 4; row++) {
            const rp = vectors[row][p], rq = vectors[row][q];
            vectors[row][p] = cosine * rp - sine * rq;
            vectors[row][q] = sine * rp + cosine * rq;
        }
    }
    const largest = [0,1,2,3].reduce((best, index) => matrix[index][index] > matrix[best][best] ? index : best);
    const [w,x,y,z] = vectors.map(row => row[largest]);
    const rotation = [[1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w)],
        [2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w)],
        [2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)]];
    return {from, to, rotation};
}

function transform(atom, alignment, origin) {
    const relative = atom.xyz.map((value, axis) => value - alignment.from[axis]);
    atom.xyz = alignment.rotation.map((row, axis) => row.reduce((sum, value, index) =>
        sum + value * relative[index], 0) + alignment.to[axis] - origin[axis] + center[axis]);
    assert(atom.xyz.every((value, axis) => Number.isFinite(value) && value > 75 * Number(axis === 0)
        && value < box[axis]), `Out of display bounds: ${atom.xyz}`);
}

function writeGro(filename, molecules) {
    let serial = 0;
    const lines = molecules.flatMap(molecule => molecule.atoms.map(atom =>
        String(atom.residue % 100000).padStart(5) + atom.residueName.padEnd(5) + atom.atomName.padStart(5)
        + String(++serial % 100000).padStart(5)
        + atom.xyz.map(value => value.toFixed(3).padStart(8)).join('')));
    fs.writeFileSync(path.join(root, filename), ['Zenodo 16745188 SND3/SEC61 final simulated snapshot, no solvent',
        String(serial), ...lines, box.map(value => value.toFixed(5)).join(' '), ''].join('\n'));
}

function build() {
    const protein = source('COARSE.GRAINED/molecule_0.itp');
    const lipidBlocks = new Map([
        ...moleculeBlocks(source('COARSE.GRAINED/martini_v3.0.0_phospholipids_v1.itp')),
        ...moleculeBlocks(source('COARSE.GRAINED/martini_v3.0_phosphoinositides_v1.0.itp')),
    ]);
    const cgDefinitions = new Map([['molecule_0', protein],
        ...['POPC','POPE','POPI'].map(name => [name, lipidBlocks.get(name)])]);
    const aaDefinitions = new Map(['PROA','PROB','PROC','PROD','PROE','PROF','POPC','POPE','POPI']
        .map(name => [name, source(`ALL.ATOM/toppar/${name}.itp`)]));
    const cgSnapshot = gro(source('COARSE.GRAINED/final_10mus.gro'));
    const aaSnapshot = gro(source('ALL.ATOM/REPLICA1/final-1.gro'));
    const cg = inventory(cgSnapshot, rows(source('COARSE.GRAINED/topol.top')), cgDefinitions);
    const aa = inventory(aaSnapshot, rows(source('ALL.ATOM/REPLICA1/topol-1.top')), aaDefinitions);
    const ca = aa.filter(molecule => !molecule.lipid).flatMap(molecule => molecule.atoms.filter(atom => atom.atomName === 'CA'));
    const bb = cg[0].atoms.filter(atom => atom.atomName === 'BB');
    assert.equal(ca.length, 1246);
    assert.deepEqual(ca.map(atom => atom.residueName.replace(/^HS[DEP]$/, 'HIS')), bb.map(atom => atom.residueName));
    const alignment = fit(ca, bb);
    const rmsd = Math.sqrt(ca.reduce((sum, atom, index) => {
        const relative = atom.xyz.map((value, axis) => value - alignment.from[axis]);
        return sum + alignment.rotation.reduce((total, row, axis) => total +
            (row.reduce((value, term, j) => value + term * relative[j], 0) + alignment.to[axis]
                - bb[index].xyz[axis]) ** 2, 0);
    }, 0) / ca.length);
    assert(rmsd < 2, `AA/CG backbone alignment RMSD too large: ${rmsd}`);
    const heads = cg.filter(molecule => molecule.lipid)
        .flatMap(molecule => molecule.atoms.filter(atom => atom.atomName === 'PO4'));
    const origin = [cgSnapshot.box[0] / 2, cgSnapshot.box[1] / 2, centroid(heads)[2]];
    const identity = {from: [0,0,0], to: [0,0,0], rotation: [[1,0,0],[0,1,0],[0,0,1]]};
    for (const molecule of aa) for (const atom of molecule.atoms) transform(atom, alignment, origin);
    for (const molecule of cg) for (const atom of molecule.atoms) transform(atom, identity, origin);
    const rename = name => name === 'molecule_0' ? 'DEP_SND3' : `DEP_${name}`;
    const selected = [...cgDefinitions].map(([name, text]) => text.replace(
        new RegExp(`^(\\s*)${name}(\\s+\\d+\\s*(?:;[^\\n]*)?)$`, 'm'),
        `$1${rename(name)}$2`)).join('\n');
    fs.writeFileSync(path.join(root, 'selected.itp'), selected);
    writeGro('system_aa.gro', aa);
    writeGro('system_cg.gro', cg);
    const counts = molecules => molecules.reduce((result, molecule) => {
        result[molecule.sourceName] = (result[molecule.sourceName] ?? 0) + 1;
        return result;
    }, {});
    const metadata = {
        record: 'https://zenodo.org/records/16745188', archiveMd5: '708ee124d8739b0f99c6d84a297e9a11',
        license: 'CC-BY-4.0', aaSnapshot: 'ALL.ATOM/REPLICA1/final-1.gro',
        cgSnapshot: 'COARSE.GRAINED/final_10mus.gro', cgTimeMicroseconds: 10,
        centerNm: center, boxNm: box, originNm: origin, aaAlignment: alignment, backboneRmsdNm: rmsd,
        aaInventory: counts(aa), cgInventory: counts(cg), sourceHashes: hashes,
        aaAtoms: aa.reduce((sum, molecule) => sum + molecule.atoms.length, 0),
        cgAtoms: cg.reduce((sum, molecule) => sum + molecule.atoms.length, 0),
        cgMolecules: cg.map(molecule => ({name: rename(molecule.sourceName),
            sourceName: molecule.sourceName, sourceOffset: molecule.sourceOffset,
            atoms: molecule.atoms.length, singleResidue: molecule.lipid})),
        aaMolecules: aa.map(molecule => ({name: molecule.sourceName,
            sourceOffset: molecule.sourceOffset, atoms: molecule.atoms.length})),
    };
    fs.writeFileSync(path.join(root, 'manifest.json'), JSON.stringify(metadata, null, 2) + '\n');
    console.log(`Imported SND3: ${metadata.aaAtoms} AA atoms, ${metadata.cgAtoms} CG beads; backbone RMSD ${rmsd.toFixed(3)} nm`);
}

if (process.argv[1] === fileURLToPath(import.meta.url)) build();
