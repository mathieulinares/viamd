"""Prepare the resolved SID1/dsRNA complex and its paired display bilayer."""

import argparse
import hashlib
import json
from pathlib import Path
import shlex
import subprocess

import numpy as np
from scipy.spatial import cKDTree


ROOT = Path(__file__).resolve().parent
SCENE_CENTER = np.array([90.0, 23.0, 9.0])
BOX = [112.0, 46.0, 32.0]


def read_gro(filename):
    lines = Path(filename).read_text().splitlines()
    count = int(lines[1])
    if len(lines) != count + 3:
        raise ValueError(f"Invalid GRO count: {filename}")
    return [
        {
            "residue": int(line[:5]),
            "resname": line[5:10].strip(),
            "name": line[10:15].strip(),
            "xyz": np.array([float(line[start:start + 8]) for start in (20, 28, 36)]),
        }
        for line in lines[2:count + 2]
    ]


def write_gro(filename, atoms):
    records = ["Paired SID1 dsRNA complex in constructed POPC bilayer", str(len(atoms))]
    for serial, atom in enumerate(atoms, 1):
        if not np.isfinite(atom["xyz"]).all():
            raise ValueError(f"Nonfinite coordinate in {filename}: {atom}")
        records.append(
            f"{atom['residue'] % 100000:5d}{atom['resname']:<5s}{atom['name']:>5s}"
            f"{serial % 100000:5d}" + "".join(f"{value:8.3f}" for value in atom["xyz"])
        )
    records.append(" ".join(f"{value:.5f}" for value in BOX))
    Path(filename).write_text("\n".join(records) + "\n")


def write_pdb(filename, atoms):
    records = []
    last_chain = None
    for serial, atom in enumerate(atoms, 1):
        if last_chain is not None and atom["chain"] != last_chain:
            records.append("TER")
        name = f" {atom['name']:<3s}" if len(atom["name"]) < 4 else atom["name"]
        records.append(
            f"ATOM  {serial:5d} {name} {atom['resname']:>3s} {atom['chain']}"
            f"{atom['residue']:4d}    "
            + "".join(f"{value * 10:8.3f}" for value in atom["xyz"])
            + "  1.00  0.00"
        )
        last_chain = atom["chain"]
    Path(filename).write_text("\n".join(records + ["TER", "END"]) + "\n")


def topology_atoms(filename):
    section = ""
    atoms = []
    for line in Path(filename).read_text().splitlines():
        clean = line.split(";")[0].strip()
        if clean.startswith("["):
            section = clean.strip("[] ").strip()
        elif section == "atoms" and clean and not clean.startswith("#"):
            fields = clean.split()
            atoms.append({
                "residue": int(fields[2]), "resname": fields[3], "name": fields[4],
            })
    if not atoms:
        raise ValueError(f"No atoms in {filename}")
    return atoms


def run(command, cwd, log):
    result = subprocess.run(command, cwd=cwd, capture_output=True, text=True, check=False)
    Path(log).write_text(result.stdout + result.stderr)
    if result.returncode:
        raise RuntimeError(f"Command failed; inspect {log}:\n{result.stderr[-4000:]}")


def prepare(work, martinize, rna_mapper, python):
    source = (ROOT / "upstream/8XC1.pdb").read_text().splitlines()
    atoms = []
    for line in source:
        if not line.startswith("ATOM  ") or line[16] not in (" ", "A"):
            continue
        if line[21] not in "ABCDEF":
            continue
        atoms.append({
            "residue": int(line[22:26]), "resname": line[17:20].strip(),
            "name": line[12:16].strip(), "chain": line[21],
            "xyz": np.array([float(line[start:start + 8]) / 10 for start in (30, 38, 46)]),
        })
    atoms, repairs = reconstruct_sidechains(atoms)
    features = json.loads((ROOT / "upstream/Q9GZC8_transmembrane.json").read_text())["features"]
    helices = []
    for chain in "AB":
        ca = {atom["residue"]: atom["xyz"] for atom in atoms
              if atom["chain"] == chain and atom["name"] == "CA"}
        for feature in features:
            start = feature["location"]["start"]["value"]
            end = feature["location"]["end"]["value"]
            points = [ca[residue] for residue in range(start, end + 1) if residue in ca]
            if len(points) >= 15:
                helices.append(np.array(points))
    if len(helices) != 22:
        raise ValueError(f"Expected 22 transmembrane helices, got {len(helices)}")
    directions = [np.linalg.svd(points - points.mean(axis=0))[2][0] for points in helices]
    _, vectors = np.linalg.eigh(sum(np.outer(vector, vector) for vector in directions))
    normal = vectors[:, -1]
    midplane = np.mean([points.mean(axis=0) for points in helices], axis=0)
    ectodomain = np.mean([atom["xyz"] for atom in atoms
                         if atom["chain"] in "AB" and atom["residue"] < 300], axis=0)
    if np.dot(ectodomain - midplane, normal) < 0:
        normal *= -1
    x_axis = np.cross([0.0, 1.0, 0.0], normal)
    x_axis /= np.linalg.norm(x_axis)
    rotation = np.array([x_axis, np.cross(normal, x_axis), normal])
    for atom in atoms:
        atom["xyz"] = rotation @ (atom["xyz"] - midplane) + SCENE_CENTER
    write_gro(ROOT / "complex_aa.gro", atoms)
    fragments = []
    secondary = {}
    for line in source:
        if line.startswith("HELIX "):
            for residue in range(int(line[21:25]), int(line[33:37]) + 1):
                secondary[(line[19], residue)] = "H"
        elif line.startswith("SHEET "):
            for residue in range(int(line[22:26]), int(line[33:37]) + 1):
                secondary[(line[21], residue)] = "E"
    for chain in "AB":
        previous = None
        for atom in (atom for atom in atoms if atom["chain"] == chain):
            residue = atom["residue"]
            if previous is None or residue > previous + 1:
                fragments.append([])
            fragments[-1].append(atom)
            previous = residue
    metadata = []
    for index, fragment in enumerate(fragments):
        name = f"sid1_{index + 1}"
        residues = list(dict.fromkeys(atom["residue"] for atom in fragment))
        sequence = "".join(secondary.get((fragment[0]["chain"], residue), "C") for residue in residues)
        write_pdb(work / f"{name}.pdb", fragment)
        run([martinize, "-f", str(work / f"{name}.pdb"), "-x", str(work / f"{name}_cg.pdb"),
             "-o", str(work / f"{name}.top"), "-name", name, "-ff", "martini3001",
             "-p", "none", "-ss", sequence], ROOT, work / f"{name}.log")
        topology = ROOT / f"{name}_0.itp"
        mapped = pdb_coordinates(work / f"{name}_cg.pdb", topology)
        write_gro(ROOT / f"{name}.gro", mapped)
        metadata.append({
            "molecule": f"{name}_0", "gro": f"{name}.gro", "itp": f"{name}_0.itp",
            "atoms": len(mapped), "chain": fragment[0]["chain"],
            "firstResidue": residues[0], "lastResidue": residues[-1], "secondaryStructure": sequence,
        })
    rna = [atom for atom in atoms if atom["chain"] in "CDEF"]
    write_pdb(work / "sid1_rna.pdb", rna)
    run([python, rna_mapper, "-f", str(work / "sid1_rna.pdb"),
         "-os", str(work / "sid1_rna_cg.pdb"), "-ot", str(ROOT / "sid1_rna.itp"),
         "-mol", "sid1_rna", "-elastic", "yes", "-p", "no"],
        ROOT, work / "sid1_rna.log")
    mapped = pdb_coordinates(work / "sid1_rna_cg.pdb", ROOT / "sid1_rna.itp")
    write_gro(ROOT / "sid1_rna.gro", mapped)
    metadata.append({"molecule": "sid1_rna", "gro": "sid1_rna.gro",
                     "itp": "sid1_rna.itp", "atoms": len(mapped)})
    (ROOT / "complex_molecules.json").write_text(json.dumps(metadata, indent=2) + "\n")
    (ROOT / "orientation.json").write_text(json.dumps({
        "originNm": midplane.tolist(), "rotation": rotation.tolist(), "centerNm": SCENE_CENTER.tolist(),
        "transmembraneHelices": len(helices), "aaAtoms": len(atoms),
        "sourceSha256": hashlib.sha256((ROOT / "upstream/8XC1.pdb").read_bytes()).hexdigest(),
        "excluded": ["glycans", "bound cholesterol/POPC", "zinc"],
        "reconstructedSidechainAtoms": repairs,
    }, indent=2) + "\n")


def reconstruct_sidechains(atoms):
    templates = {}
    for residue in ("ASP", "LYS", "LEU"):
        columns = []
        template = {}
        for line in (ROOT / f"upstream/{residue}.cif").read_text().splitlines():
            if line.startswith("_chem_comp_atom."):
                columns.append(line.split(".")[1].strip())
            elif line.startswith(residue + " ") and columns:
                fields = dict(zip(columns, shlex.split(line), strict=True))
                if fields["type_symbol"] != "H" and fields["atom_id"] != "OXT":
                    template[fields["atom_id"]] = np.array([
                        float(fields[f"pdbx_model_Cartn_{axis}_ideal"]) / 10 for axis in "xyz"
                    ])
            elif line.strip() == "#" and template:
                break
        templates[residue] = template
    residues = {}
    for atom in atoms:
        residues.setdefault((atom["chain"], atom["residue"]), []).append(atom)
    repaired = []
    output = []
    for (chain, residue), group in residues.items():
        template = templates.get(group[0]["resname"])
        if chain in "AB" and template:
            observed = {atom["name"]: atom["xyz"] for atom in group}
            missing = [name for name in template if name not in observed]
            if missing:
                anchors = ["N", "CA", "C", "CB"]
                fixed = np.array([observed[name] for name in anchors])
                moving = np.array([template[name] for name in anchors])
                a, b = moving.mean(axis=0), fixed.mean(axis=0)
                u, _, vt = np.linalg.svd((moving - a).T @ (fixed - b))
                correction = np.eye(3)
                correction[-1, -1] = np.linalg.det(u @ vt)
                rotation = u @ correction @ vt
                for name in missing:
                    group.append({**group[0], "name": name, "xyz": (template[name] - a) @ rotation + b})
                repaired.append({"chain": chain, "residue": residue,
                                 "resname": group[0]["resname"], "addedAtoms": missing})
        output.extend(group)
    return output, repaired


def pdb_coordinates(filename, topology):
    records = [line for line in Path(filename).read_text().splitlines() if line.startswith("ATOM")]
    atoms = topology_atoms(topology)
    if len(atoms) != len(records):
        raise ValueError(f"Topology and coordinate counts differ: {filename}")
    for atom, line in zip(atoms, records, strict=True):
        if atom["name"] != line[12:16].strip():
            raise ValueError(f"Bead order differs: {filename}")
        atom["xyz"] = np.array([float(line[start:start + 8]) / 10 for start in (30, 38, 46)])
        if not np.isfinite(atom["xyz"]).all():
            raise ValueError(f"Nonfinite mapping output: {filename}, bead {atom['name']}")
    return atoms


def rotation_to_z(direction):
    z_axis = direction / np.linalg.norm(direction)
    x_axis = np.cross([0.0, 1.0, 0.0], z_axis)
    x_axis /= np.linalg.norm(x_axis)
    return np.array([x_axis, np.cross(z_axis, x_axis), z_axis])


def bilayer():
    from rdkit import Chem
    from rdkit.Chem import rdMolTransforms

    complex_aa = read_gro(ROOT / "complex_aa.gro")
    metadata = json.loads((ROOT / "complex_molecules.json").read_text())
    complex_cg = [atom for entry in metadata for atom in read_gro(ROOT / entry["gro"])]
    aa_source = read_gro(ROOT / "../../rendering_all_atom_all_lipids.gro")
    aa_template = [atom for atom in aa_source if atom["resname"] == "POPC"]
    cg_source = read_gro(ROOT / "../rendering_lipids/lipids_available.gro")
    cg_template = [atom for atom in cg_source if atom["resname"] == "POPC"]
    # The AA fixture names atoms by element/ordinal in PubChem's heavy-atom order.
    molecule = Chem.MolFromMolFile(str(ROOT / "upstream/popc.sdf"), removeHs=True)
    if molecule is None:
        raise ValueError("Cannot parse pinned PubChem POPC source")
    atom_names = {}
    element_counts = {}
    for atom in molecule.GetAtoms():
        element = atom.GetSymbol()
        element_counts[element] = element_counts.get(element, 0) + 1
        atom_names[atom.GetIdx()] = f"{element}{element_counts[element]}"
    phosphorus = next(atom.GetIdx() for atom in molecule.GetAtoms() if atom.GetSymbol() == "P")
    distances = Chem.GetDistanceMatrix(molecule)[phosphorus]
    terminals = sorted((atom.GetIdx() for atom in molecule.GetAtoms()
                        if atom.GetSymbol() == "C" and atom.GetDegree() == 1),
                       key=lambda index: distances[index], reverse=True)[:2]
    by_name = {atom["name"]: atom["xyz"] for atom in aa_template}
    if len(aa_template) != 134 or len(cg_template) != 12:
        raise ValueError("Expected full POPC AA and published Martini template")
    extended = Chem.AddHs(molecule)
    conformer = Chem.Conformer(extended.GetNumAtoms())
    hydrogen = 0
    extended_names = dict(atom_names)
    for atom in extended.GetAtoms():
        index = atom.GetIdx()
        if atom.GetSymbol() == "H":
            hydrogen += 1
            extended_names[index] = f"H{hydrogen}"
        conformer.SetAtomPosition(index, by_name[extended_names[index]] * 10)
    extended.RemoveAllConformers()
    extended.AddConformer(conformer)
    conformer = extended.GetConformer()
    for terminal in terminals:
        chain = [terminal]
        while True:
            candidates = [neighbor.GetIdx() for neighbor in extended.GetAtomWithIdx(chain[-1]).GetNeighbors()
                          if neighbor.GetSymbol() == "C" and neighbor.GetIdx() not in chain]
            if not candidates:
                break
            if len(candidates) != 1:
                raise ValueError("Unexpected branch in POPC acyl chain")
            chain.append(candidates[0])
        for index in range(len(chain) - 3):
            segment = chain[index:index + 4]
            if all(extended.GetBondBetweenAtoms(segment[j], segment[j + 1]).GetBondType()
                   == Chem.BondType.SINGLE for j in range(3)):
                rdMolTransforms.SetDihedralDeg(conformer, *segment, 180.0)
    extended_positions = {extended_names[index]: np.array(conformer.GetAtomPosition(index)) / 10
                          for index in range(extended.GetNumAtoms())}
    aa_p = extended_positions[atom_names[phosphorus]]
    aa_tail = np.mean([extended_positions[atom_names[index]] for index in terminals], axis=0)
    aa_coordinates = np.array([extended_positions[atom["name"]] for atom in aa_template])
    aa_coordinates = (rotation_to_z(aa_p - aa_tail) @ (aa_coordinates - aa_coordinates.mean(axis=0)).T).T
    cg_coordinates = np.array([atom["xyz"] for atom in cg_template])
    cg_by_name = {atom["name"]: atom["xyz"] for atom in cg_template}
    cg_tail = (cg_by_name["C4A"] + cg_by_name["C4B"]) / 2
    cg_coordinates = (rotation_to_z(cg_by_name["PO4"] - cg_tail)
                      @ (cg_coordinates - cg_coordinates.mean(axis=0)).T).T
    aa_tree = cKDTree(np.array([atom["xyz"] for atom in complex_aa]))
    cg_tree = cKDTree(np.array([atom["xyz"] for atom in complex_cg]))
    membrane_aa, membrane_cg, sites = [], [], []
    occupied_aa = None
    occupied_cg = None
    aa_heavy = np.array([not atom["name"].startswith("H") for atom in aa_template])
    for leaflet in (-1, 1):
        for row in range(18):
            for column in range(18):
                angle = 0.0
                c, s = np.cos(angle), np.sin(angle)
                rotate = np.array([[c, -s, 0], [s, c, 0], [0, 0, 1]])
                if leaflet == -1:
                    rotate = rotate @ np.diag([1, -1, -1])
                center = SCENE_CENTER + np.array([(column - 8.5) * 0.85,
                                                   (row - 8.5) * 0.85, leaflet * 1.15])
                if leaflet == 1:
                    center[:2] += 0.425
                aa_xyz = (rotate @ aa_coordinates.T).T + center
                cg_xyz = (rotate @ cg_coordinates.T).T + center
                if aa_tree.query(aa_xyz[aa_heavy])[0].min() < 0.22:
                    continue
                if cg_tree.query(cg_xyz)[0].min() < 0.45:
                    continue
                if occupied_aa is not None and occupied_aa.query(aa_xyz[aa_heavy])[0].min() < 0.15:
                    continue
                if occupied_cg is not None and occupied_cg.query(cg_xyz)[0].min() < 0.35:
                    continue
                residue = len(sites) + 1
                for source, points, output in ((aa_template, aa_xyz, membrane_aa),
                                                (cg_template, cg_xyz, membrane_cg)):
                    for atom, point in zip(source, points, strict=True):
                        output.append({**atom, "residue": residue, "xyz": point})
                sites.append({"residue": residue, "leaflet": leaflet, "row": row,
                              "column": column, "centerNm": center.tolist()})
                occupied_aa = cKDTree(np.array([atom["xyz"] for atom in membrane_aa
                                                if not atom["name"].startswith("H")]))
                occupied_cg = cKDTree(np.array([atom["xyz"] for atom in membrane_cg]))
    if len(sites) < 200 or any(sum(site["leaflet"] == side for site in sites) < 80 for side in (-1, 1)):
        raise ValueError(f"Too few bilayer lipids: {len(sites)}; leaflet counts "
                         f"{[sum(site['leaflet'] == side for site in sites) for side in (-1, 1)]}")
    write_gro(ROOT / "membrane_aa.gro", membrane_aa)
    write_gro(ROOT / "membrane_cg.gro", membrane_cg)
    (ROOT / "membrane.json").write_text(json.dumps({
        "lipid": "POPC", "count": len(sites), "aaAtomsPerLipid": len(aa_template),
        "cgBeadsPerLipid": len(cg_template), "sites": sites,
        "note": "Constructed display bilayer, not equilibrated. Independent AA and published CG conformers centered at paired sites.",
    }, indent=2) + "\n")
    print(f"Constructed {len(sites)} matched POPC lipids around SID1/dsRNA")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--prepare", action="store_true")
    parser.add_argument("--work", type=Path)
    parser.add_argument("--martinize")
    parser.add_argument("--rna-mapper")
    parser.add_argument("--python")
    arguments = parser.parse_args()
    if arguments.prepare:
        if not all((arguments.work, arguments.martinize, arguments.rna_mapper, arguments.python)):
            parser.error("--prepare requires --work, --martinize, --rna-mapper and --python")
        arguments.work.mkdir(parents=True, exist_ok=True)
        prepare(arguments.work.resolve(), arguments.martinize, arguments.rna_mapper, arguments.python)
    bilayer()
