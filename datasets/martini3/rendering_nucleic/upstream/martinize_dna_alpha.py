#!/usr/bin/env python3
"""
Standalone Martinize DNA Script

Usage: python martinize_dna_v3.0.0.py -f ssDNA.pdb -mol dna -elastic yes -ef 100 -el 0.5 -eu 1.2 
-os molecule.pdb -ot molecule.itp

This script processes an all-atom DNA structure and returns coarse-grained topology in the 
GROMACS' .itp format and coarse-grained PDB. 

This script includes implementations of:
- Martini 3.0 DNA force field
- PDB parsing and manipulation
- Coarse-grained mapping
- Topology generation

Please cite:
    Yangaliev D, Ozkan SB. Coarse-grained DNA model for the Martini 3 force field. 
    Biophys J. 2025 Aug 5:S0006-3495(25)00483-7. doi: 10.1016/j.bpj.2025.07.034. 
"""

import argparse
import copy
import logging
import os
import sys
from datetime import datetime
from pathlib import Path
from typing import List, Tuple
import numpy as np

# Configure logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(levelname)s - %(message)s',
    datefmt='%H:%M:%S'
)
logger = logging.getLogger(__name__)


###################################
## ITP I/O Functions ##
###################################

def read_itp(filename):
    """Read a Gromacs ITP file and organize its contents by section."""
    logger.debug(f"Reading ITP file: {filename}")
    itp_data = {}
    current_tag = None
    
    try:
        with open(filename, "r", encoding="utf-8") as file:
            for line in file:
                # Skip comments and empty lines
                if line.strip() == "" or line.strip().startswith(";"):
                    continue
                # Detect section headers
                if line.startswith("[") and line.endswith("]\n"):
                    current_tag = line.strip()[1:-1].strip()
                    itp_data[current_tag] = []
                elif current_tag:
                    connectivity, parameters, comment = line2bond(line, current_tag)
                    itp_data[current_tag].append([connectivity, parameters, comment])
    except FileNotFoundError:
        logger.warning(f"ITP file not found: {filename}")
        # Return empty structure with expected sections
        return {
            "bonds": [],
            "angles": [],
            "dihedrals": [],
            "constraints": [],
            "exclusions": [],
            "pairs": [],
            "virtual_sites3": []
        }
    
    logger.debug(f"Successfully read ITP file with {len(itp_data)} sections")
    return itp_data


def line2bond(line, tag):
    """Parse a line from an ITP file and return connectivity, parameters, and comment."""
    data, _, comment = line.partition(";")
    data = data.split()
    comment = comment.strip()
    
    if tag == "bonds" or tag == "constraints":
        connectivity = data[:2]
        parameters = data[2:]
    elif tag == "angles":
        connectivity = data[:3]
        parameters = data[3:]
    elif tag == "dihedrals":
        connectivity = data[:4]
        parameters = data[4:]
    elif tag == "virtual_sites3":
        connectivity = data[:4]
        parameters = data[4:]
    else:
        connectivity = data
        parameters = []
    
    if parameters:
        try:
            parameters[0] = int(parameters[0])
            parameters[1:] = [float(i) for i in parameters[1:]]
        except (ValueError, IndexError):
            pass
    
    try:
        connectivity = tuple(int(i) for i in connectivity) if connectivity else ()
    except ValueError:
        connectivity = tuple(connectivity)
    parameters = tuple(parameters) if parameters else ()
    
    return connectivity, parameters, comment


def bond2line(connectivity=None, parameters="", comment=""):
    """Format a bond entry into a string for a Gromacs ITP file."""
    connectivity_str = "   ".join(f"{int(atom):5d}" for atom in connectivity)
    type_str = ""
    parameters_str = ""
    if parameters:
        type_str = f"{int(parameters[0]):2d}"
        # Format: all params except the last as float, last as int if integral
        params_list = []
        for i, param in enumerate(parameters[1:]):
            if i == len(parameters) - 2 and float(param).is_integer():
                params_list.append(f"{int(float(param)):7d}")
            else:
                params_list.append(f"{float(param):7.4f}")
        parameters_str = "   ".join(params_list)
    line = connectivity_str + "   " + type_str + "   " + parameters_str
    if comment:
        line += " ; " + comment
    line += "\n"
    return line


def format_header(molname="molecule", forcefield="", arguments="", timestamp="") -> List[str]:
    """Format ITP file header."""
    lines = [f'; MARTINI ({forcefield}) Coarse Grained topology file for "{molname}"\n']
    if timestamp:
        lines.append(f"; Generated on: {timestamp}\n")
    lines.append("; Created using the following options:\n")
    if arguments:
        lines.append(f"; {arguments}\n")
    lines.append("; " + "#" * 100 + "\n")
    return lines


def format_sequence_section(sequence, secstruct) -> List[str]:
    """Format sequence section for ITP file."""
    sequence_str = "".join(sequence)
    secstruct_str = "".join(secstruct)
    lines = ["; Sequence:\n"]
    lines.append(f"; {sequence_str}\n")
    lines.append("; Secondary Structure:\n")
    lines.append(f"; {secstruct_str}\n")
    return lines


def format_moleculetype_section(molname="molecule", nrexcl=1) -> List[str]:
    """Format moleculetype section."""
    lines = ["\n[ moleculetype ]\n"]
    lines.append("; Name         Exclusions\n")
    lines.append(f"{molname:<15s} {nrexcl:3d}\n")
    return lines


def format_atoms_section(atoms: List[Tuple]) -> List[str]:
    """Format atoms section."""
    lines = ["\n[ atoms ]\n"]
    for atom in atoms:
        atom = tuple(atom)
        if len(atom) == 9:
            # Format with 8 values + comment
            if atom[8] and atom[8].strip():  # If there's a non-empty comment
                line = "%5d %5s %5d %5s %5s %5d %7.4f %7.4f ; %s" % atom
            else:
                line = "%5d %5s %5d %5s %5s %5d %7.4f %7.4f" % atom[:8]
        else:
            # Format with 7 values + comment
            if len(atom) > 7 and atom[7] and atom[7].strip():  # If there's a non-empty comment
                line = "%5d %5s %5d %5s %5s %5d %7.4f ; %s" % atom
            else:
                line = "%5d %5s %5d %5s %5s %5d %7.4f" % atom[:7]
        line += "\n"
        lines.append(line)
    return lines


def format_bonded_section(header: str, bonds: List[List]) -> List[str]:
    """Format bonded interactions section."""
    if not bonds:
        return []
    
    lines = [f"\n[ {header} ]\n"]
    for bond in bonds:
        line = bond2line(*bond)
        lines.append(line)
    return lines


def format_posres_section(atoms: List[Tuple], posres_fc=1000, 
                          selection: List[str] = None) -> List[str]:
    """Format position restraints section."""
    if selection is None:
        selection = ["BB1", "BB2", "BB3", "SC1"]
    
    lines = [
        "\n#ifdef POSRES\n",
        f"#define POSRES_FC {posres_fc:.2f}\n",
        " [ position_restraints ]\n",
    ]
    
    for atom in atoms:
        atom_name = atom[4]  # atom name is at index 4
        if atom_name in selection:
            atom_id = atom[0]  # atom id is at index 0
            lines.append(f"  {atom_id:5d}    1    POSRES_FC    POSRES_FC    POSRES_FC\n")
    
    lines.append("#endif")
    return lines


###################################
## PDB Tools Classes ##
###################################

class Atom:
    """Represents a single atom in a PDB file."""
    
    def __init__(self, record="ATOM", atid=1, name="CA", alt_loc="", resname="ALA",
                 chid="A", resid=1, icode="", x=0.0, y=0.0, z=0.0, occupancy=1.0,
                 bfactor=0.0, segid="", element="C", charge=""):
        self.record = record
        self.atid = atid
        self.name = name
        self.alt_loc = alt_loc
        self.resname = resname
        self.chid = chid
        self.resid = resid
        self.icode = icode
        self.x = x
        self.y = y
        self.z = z
        self.occupancy = occupancy
        self.bfactor = bfactor
        self.segid = segid
        self.element = element
        self.charge = charge
        self.vec = (x, y, z)

    @classmethod
    def from_pdb_line(cls, line):
        """Create an Atom from a PDB line."""
        record = line[0:6].strip()
        atid = int(line[6:11].strip())
        name = line[12:16].strip()
        alt_loc = line[16:17].strip()
        resname = line[17:20].strip()
        chid = line[21:22].strip()
        resid = int(line[22:26].strip())
        icode = line[26:27].strip()
        x = float(line[30:38].strip())
        y = float(line[38:46].strip())
        z = float(line[46:54].strip())
        occupancy = float(line[54:60].strip()) if line[54:60].strip() else 1.0
        bfactor = float(line[60:66].strip()) if line[60:66].strip() else 0.0
        segid = line[72:76].strip() if len(line) > 72 else ""
        element = line[76:78].strip() if len(line) > 76 else ""
        charge = line[78:80].strip() if len(line) > 78 else ""
        
        return cls(record, atid, name, alt_loc, resname, chid, resid, icode,
                   x, y, z, occupancy, bfactor, segid, element, charge)

    def to_pdb_line(self):
        """Convert atom to PDB line format."""
        return (f"{self.record:<6s}{self.atid:>5d} {self.name:>4s}{self.alt_loc:1s}"
                f"{self.resname:>3s} {self.chid:1s}{self.resid:>4d}{self.icode:1s}   "
                f"{self.x:>8.3f}{self.y:>8.3f}{self.z:>8.3f}{self.occupancy:>6.2f}"
                f"{self.bfactor:>6.2f}      {self.segid:<4s}{self.element:>2s}{self.charge:>2s}")


class AtomList(list):
    """A list of Atom objects with convenient attribute access."""
    
    def __add__(self, other):
        return AtomList(super().__add__(other))

    @property
    def names(self):
        return [atom.name for atom in self]

    @property
    def resnames(self):
        return [atom.resname for atom in self]

    @property
    def vecs(self):
        return [atom.vec for atom in self]

    def mask(self, mask_vals, mode="name"):
        """Return a new AtomList with atoms matching the given mask."""
        if isinstance(mask_vals, str):
            mask_vals = [mask_vals]
        
        filtered = []
        for atom in self:
            if mode == "name" and atom.name in mask_vals:
                filtered.append(atom)
            elif mode == "resname" and atom.resname in mask_vals:
                filtered.append(atom)
            elif mode == "chid" and atom.chid in mask_vals:
                filtered.append(atom)
        
        return AtomList(filtered)

    def write_pdb(self, out_pdb, append=False):
        """Write the AtomList to a PDB file."""
        mode = "a" if append else "w"
        with open(out_pdb, mode, encoding="utf-8") as f:
            for atom in self:
                f.write(atom.to_pdb_line() + "\n")
            f.write("END\n")


class Residue:
    """Represents a residue containing Atom objects."""
    
    def __init__(self, resname="", resid=1, chid="A", icode=""):
        self.resname = resname
        self.resid = resid
        self.chid = chid
        self.icode = icode
        self.atoms = AtomList()

    def add_atom(self, atom):
        self.atoms.append(atom)


class Chain:
    """Represents a chain containing multiple residues."""
    
    def __init__(self, chid="A"):
        self.chid = chid
        self.residues = {}

    def add_atom(self, atom):
        key = (atom.resid, atom.icode)
        if key not in self.residues:
            self.residues[key] = Residue(atom.resname, atom.resid, atom.chid, atom.icode)
        self.residues[key].add_atom(atom)

    def __iter__(self):
        return iter(sorted(self.residues.values(), key=lambda r: (r.resid, r.icode)))


class Model:
    """Represents a model containing multiple chains."""
    
    def __init__(self, modid=1):
        self.modid = modid
        self.chains = {}

    def add_atom(self, atom):
        if atom.chid not in self.chains:
            self.chains[atom.chid] = Chain(atom.chid)
        self.chains[atom.chid].add_atom(atom)

    @property
    def atoms(self):
        all_atoms = []
        for chain in self.chains.values():
            for residue in chain:
                all_atoms.extend(residue.atoms)
        return AtomList(all_atoms)


class System:
    """Represents an entire system, potentially with multiple models."""
    
    def __init__(self):
        self.models = {}

    def __iter__(self):
        return iter(self.models.values())

    def add_atom(self, atom, modid=1):
        if modid not in self.models:
            self.models[modid] = Model(modid)
        self.models[modid].add_atom(atom)

    @property
    def atoms(self):
        all_atoms = []
        for model in self.models.values():
            all_atoms.extend(model.atoms)
        return AtomList(all_atoms)

    def chains(self):
        """Generator yielding all chains across all models."""
        for model in self.models.values():
            for chain in model.chains.values():
                yield chain


class PDBParser:
    """Parses a PDB file and builds a System object."""
    
    def __init__(self, pdb_path):
        self.pdb_path = pdb_path

    def parse(self):
        system = System()
        current_model = 1

        with open(self.pdb_path, "r", encoding="utf-8") as file:
            for line in file:
                record_type = line[0:6].strip()
                
                if record_type == "MODEL":
                    try:
                        current_model = int(line[10:14].strip())
                    except ValueError:
                        current_model = 1
                elif record_type in ("ATOM", "HETATM"):
                    try:
                        atom = Atom.from_pdb_line(line)
                        system.add_atom(atom, current_model)
                    except Exception as e:
                        print(f"Error parsing line: {line.strip()} -> {e}")
                elif record_type == "ENDMDL":
                    current_model += 1

        return system


def pdb2system(pdb_path) -> System:
    """Parse a PDB file into a System object."""
    parser = PDBParser(pdb_path)
    return parser.parse()


###################################
## CG Mapping Functions ##
###################################

def move_o3(system):
    """Move each O3' atom to the next residue."""
    o3_moved = 0
    for chain in system.chains():
        for i, residue in enumerate(chain):
            atoms = residue.atoms
            for atom in atoms[:]:  # Create a copy to iterate over
                if atom.name == "O3'":
                    atoms.remove(atom)
                    if i == 0:
                        o3atom = atom
                    else:
                        o3atom.resname = residue.resname
                        o3atom.resid = residue.resid
                        atoms.append(o3atom)
                        o3atom = atom
                    o3_moved += 1
                    break
    if o3_moved > 0:
        logger.debug(f"Moved {o3_moved} O3' atoms to next residues")


def map_residue(residue, mapping, atid):
    """Map an atomistic residue to a coarse-grained residue."""
    cgresidue = []
    dummy_atom = residue.atoms[0]
    
    for bname, anames in mapping.items():
        bead = copy.deepcopy(dummy_atom)
        bead.name = bname
        bead.atid = atid
        atid += 1
        
        # Find atoms matching the bead names
        atoms = [atom for atom in residue.atoms if atom.name in anames]
        if atoms:
            # Average coordinates
            bvec = np.average([atom.vec for atom in atoms], axis=0)
            bead.x, bead.y, bead.z = bvec[0], bvec[1], bvec[2]
            bead.vec = bvec
            bead.element = "Z" if bname.startswith("B") else "S"
            cgresidue.append(bead)
    
    return cgresidue


def map_chain(chain, ff, atid=1):
    """Map a chain of atomistic residues to a coarse-grained representation."""
    cgchain = []
    residues = list(chain)
    logger.debug(f"Mapping chain with {len(residues)} residues to CG")
    
    for idx, residue in enumerate(residues):
        if residue.resname not in ff.mapping:
            logger.warning(f"Unknown residue {residue.resname} at position {idx+1}")
            continue
            
        mapping = ff.mapping[residue.resname]
        if idx == 0:
            mapping = mapping.copy()
            if "BB1" in mapping:
                del mapping["BB1"]
                logger.debug(f"Removed BB1 from first residue {residue.resname}")
        
        cgresidue = map_residue(residue, mapping, atid)
        cgchain.extend(cgresidue)
        atid += len(mapping)
        
        if idx == 0 or idx == len(residues) - 1 or idx % 10 == 0:
            logger.debug(f"Mapped residue {idx+1}/{len(residues)}: {residue.resname} -> {len(cgresidue)} beads")
    
    logger.debug(f"Chain mapping complete: {len(residues)} residues -> {len(cgchain)} CG beads")
    return cgchain


###################################
## Bond List Class ##
###################################

class BondList(list):
    """List for storing bond-like interactions."""
    
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)


###################################
## Force Field Classes ##
###################################

class NucleicForceField:
    """Base class for nucleic acid force fields."""
    
    @staticmethod
    def read_itp_file(resname, directory, mol, version, itp_dir=None):
        """Read an ITP file for a given residue.
        
        Parameters:
        -----------
        itp_dir : str or Path, optional
            Custom directory containing ITP files. If None, uses the default
            martinize_{mol}_{version}_itps/ relative to the script directory.
        """
        # Get the script directory
        script_dir = Path(__file__).parent
        
        # Use custom ITP directory if provided, otherwise use default
        if itp_dir is not None:
            itp_path = Path(itp_dir)
        else:
            itp_path = script_dir / f"{mol}_alpha_itps"
        
        file_path = itp_path / f"{mol}_{resname}.itp"
        
        if file_path.exists():
            logger.debug(f"Reading ITP file: {file_path}")
            return read_itp(str(file_path))
        
        logger.warning(f"Could not find ITP file for residue {resname} at {file_path}")
        return {
            "bonds": [],
            "angles": [],
            "dihedrals": [],
            "constraints": [],
            "exclusions": [],
            "pairs": [],
            "virtual_sites3": []
        }

    @staticmethod
    def itp_to_indata(itp_data):
        """Convert ITP data into individual parameter lists."""
        sc_bonds = itp_data.get("bonds", [])
        sc_angles = itp_data.get("angles", [])
        sc_dihs = itp_data.get("dihedrals", [])
        sc_cons = itp_data.get("constraints", [])
        sc_excls = itp_data.get("exclusions", [])
        sc_pairs = itp_data.get("pairs", [])
        sc_vs3s = itp_data.get("virtual_sites3", [])
        return sc_bonds, sc_angles, sc_dihs, sc_cons, sc_excls, sc_pairs, sc_vs3s

    @staticmethod
    def parameters_by_resname(resnames, directory, mol, version, itp_dir=None):
        """Obtain parameters for each residue."""
        logger.info(f"Loading sidechain parameters for residues: {resnames}")
        params = []
        for resname in resnames:
            logger.debug(f"Loading parameters for residue {resname}")
            itp_data = NucleicForceField.read_itp_file(resname, directory, mol, version, itp_dir)
            param = NucleicForceField.itp_to_indata(itp_data)
            params.append(param)
            logger.debug(f"Loaded {len(param[0])} bonds, {len(param[1])} angles, {len(param[2])} dihedrals for {resname}")
        return dict(zip(resnames, params))

    def __init__(self, directory, mol, version, itp_dir=None):
        """Initialize the nucleic force field.
        
        Parameters:
        -----------
        itp_dir : str or Path, optional
            Custom directory containing ITP files. If None, uses the default
            martinize_{mol}_{version}_itps/ relative to the script directory.
        """
        self.directory = directory
        self.mol = mol
        self.version = version
        
        # Determine the actual ITP directory
        script_dir = Path(__file__).parent
        if itp_dir is not None:
            self.itp_dir = Path(itp_dir)
        else:
            self.itp_dir = script_dir / f"{mol}_alpha_itps"
        
        logger.info(f"Loading ITP files from {self.itp_dir}")
        
        self.resdict = self.parameters_by_resname(self.resnames, directory, mol, version, self.itp_dir)
        self.elastic_network = False
        self.el_bond_type = 6

    def _normalize_resname(self, resname):
        """Normalize terminal residue names: DA5/DA3 -> A, DC5/DC3 -> C, etc."""
        if resname in self.resdict:
            return resname
        # Strip D prefix and 3/5 suffix for DNA terminal residues
        base = resname.lstrip("D").rstrip("35")
        if base in self.resdict:
            return base
        return resname

    def sc_bonds(self, resname):
        return self.resdict[self._normalize_resname(resname)][0]

    def sc_angles(self, resname):
        return self.resdict[self._normalize_resname(resname)][1]

    def sc_dihs(self, resname):
        return self.resdict[self._normalize_resname(resname)][2]

    def sc_cons(self, resname):
        return self.resdict[self._normalize_resname(resname)][3]

    def sc_excls(self, resname):
        return self.resdict[self._normalize_resname(resname)][4]

    def sc_pairs(self, resname):
        return self.resdict[self._normalize_resname(resname)][5]

    def sc_vs3s(self, resname):
        return self.resdict[self._normalize_resname(resname)][6]

    def sc_blist(self, resname):
        """Get all bonded parameters for a residue."""
        rn = self._normalize_resname(resname)
        return [
            self.resdict[rn][0],
            self.resdict[rn][1],
            self.resdict[rn][2],
            self.resdict[rn][3],
            self.resdict[rn][4],
            self.resdict[rn][5],
            self.resdict[rn][6],
        ]

    @staticmethod
    def update_non_standard_mapping(mapping):
        """Update mapping with non-standard residue names."""
        mapping.update({
            "RA3": mapping["A"], "RA5": mapping["A"], "A3": mapping["A"], "A5": mapping["A"],
            "NARAP": mapping["A"], "DMA": mapping["A"],
            "DHA": mapping["A"], "SPA": mapping["A"], "RC3": mapping["C"], "RC5": mapping["C"],
            "C3": mapping["C"], "C5": mapping["C"], "5MC": mapping["C"], "3MP": mapping["C"],
            "MRC": mapping["C"], "NMC": mapping["C"], "RG3": mapping["G"], "RG5": mapping["G"],
            "G3": mapping["G"], "G5": mapping["G"], "1MG": mapping["G"], "2MG": mapping["G"],
            "7MG": mapping["G"], "MRG": mapping["G"],
            # DNA terminal residues (AMBER naming)
            "DA5": mapping["A"], "DA3": mapping["A"], "DA": mapping["A"],
            "DC5": mapping["C"], "DC3": mapping["C"], "DC": mapping["C"],
            "DG5": mapping["G"], "DG3": mapping["G"], "DG": mapping["G"],
            "DT5": mapping["T"], "DT3": mapping["T"], "DT": mapping["T"],
        })


class Martini30DNA(NucleicForceField):
    """Force field for Martini 3.0 DNA."""

    resnames = ["A", "C", "G", "T",]
    alt_resnames = ["ADE", "CYT", "GUA", "THY",]

    # ------------------------------------------------------------------
    # AA-to-CG mapping loaded from .map files at init time
    # ------------------------------------------------------------------

    @staticmethod
    def _read_map_file(map_path, bb_offset=0):
        """Parse a .map file.

        Returns ``(mapping_dict, atoms_list)`` where *atoms_list* entries are
        ``(bead_idx, type, bead_name, cgnr, charge, mass)``.
        *bb_offset* is added to bead indices (0 for backbone, 3 for sidechain).
        """
        mapping = {}
        atoms = []
        current = None
        with open(map_path) as f:
            for line in f:
                s = line.strip()
                if not s or s.startswith(";") or s.startswith("#"):
                    continue
                if s.startswith("[") and s.endswith("]"):
                    current = s[1:-1].strip()
                    continue
                parts = s.split()
                if not parts:
                    continue
                if current == "mapping":
                    mapping[parts[0]] = tuple(parts[1:])
                elif current == "atomtypes":
                    if len(parts) >= 4:
                        # CG_BEAD  TYPE  CHARGE  MASS
                        bead_name = parts[0]
                        atype = parts[1]
                        charge = float(parts[2])
                        mass = float(parts[3])
                        bead_idx = bb_offset + len(atoms)
                        cgnr = 1 if bb_offset == 0 else 2  # BB=1, SC=2
                        atoms.append((bead_idx, atype, bead_name, cgnr, charge, mass))
        return mapping, atoms

    def __init__(self, directory="dna_reg", mol="dna", version="v3.0.0", itp_dir=None):
        super().__init__(directory, mol, version, itp_dir)
        self.name = "martini30dna"

        script_dir = Path(__file__).parent
        map_dir = script_dir / f"{mol}_alpha_itps"

        # --- backbone mapping + atoms from map file ---
        bb_path = map_dir / "dna_bb.map"
        self.bb_mapping, self.bb_atoms = self._read_map_file(bb_path, bb_offset=0)

        # --- sidechain mappings ---
        for base in self.resnames:
            sc_path = map_dir / f"dna_{base}.map"
            sc_map, sc_at = self._read_map_file(sc_path, bb_offset=3)
            setattr(self, f"{base.lower()}_mapping", sc_map)
            setattr(self, f"{base.lower()}_atoms", sc_at)

        # Composite mapping dict
        self.mapping = {}
        for base in self.resnames:
            sc_map = getattr(self, f"{base.lower()}_mapping")
            self.mapping[base] = {**self.bb_mapping, **sc_map}
            for alt in self.alt_resnames:
                if alt.startswith(base):
                    self.mapping[alt] = {**self.bb_mapping, **sc_map}

        # Read backbone bonded parameters from ITP
        bb_blist = self.read_bb_itp(mol=mol, version=version, itp_dir=itp_dir)
        self.bb_bonds = bb_blist[0]
        self.bb_angles = bb_blist[1]
        self.bb_dihs = bb_blist[2]
        self.bb_cons = bb_blist[3]
        self.bb_excls = bb_blist[4]
        self.bb_pairs = bb_blist[5]
        self.bb_vs3s = bb_blist[6]
        self.bb_blist = bb_blist

        # Sidechain atom definitions per base
        sc_atoms = tuple(getattr(self, f"{b.lower()}_atoms") for b in self.resnames)
        self.mapdict = dict(zip(self.resnames, sc_atoms))

        NucleicForceField.update_non_standard_mapping(self.mapping)

    @staticmethod
    def read_bb_itp(mol="dna", version="v3.0.0", itp_dir=None):
        """Read backbone bonded parameters from dna_bb ITP file.

        Atom types come from ``dna_bb.map``; this only reads bonded terms.
        ITP indices are 0-based.
        """
        script_dir = Path(__file__).parent
        if itp_dir is not None:
            itp_path = Path(itp_dir)
        else:
            itp_path = script_dir / f"{mol}_alpha_itps"

        bb_file = itp_path / "dna_bb.itp"
        logger.info("Reading backbone parameters from %s", bb_file)

        bb_itp = read_itp(str(bb_file))

        bb_bonds = []
        for conn, params, comment in bb_itp.get("bonds", []):
            bb_bonds.append([list(conn), list(params), comment if isinstance(comment, str) else ""])

        bb_angles = []
        for conn, params, comment in bb_itp.get("angles", []):
            bb_angles.append([list(conn), list(params), comment if isinstance(comment, str) else ""])

        bb_dihs = []
        for conn, params, comment in bb_itp.get("dihedrals", []):
            bb_dihs.append([list(conn), list(params), comment if isinstance(comment, str) else ""])

        bb_cons = []
        for conn, params, comment in bb_itp.get("constraints", []):
            bb_cons.append([list(conn), list(params), comment if isinstance(comment, str) else ""])

        bb_excls = []
        for conn, params, comment in bb_itp.get("exclusions", []):
            bb_excls.append([list(conn), list(params) if params else (), comment if isinstance(comment, str) else ""])

        bb_blist = [bb_bonds, bb_angles, bb_dihs, bb_cons, bb_excls, [], []]

        return bb_blist

    def sc_atoms(self, resname):
        """Return side-chain atoms for the given residue."""
        return self.mapdict.get(self._normalize_resname(resname), [])


###################################
## Topology Class ##
###################################

class Topology:
    """Topology class for constructing coarse-grained topologies."""
    
    def __init__(self, forcefield, sequence: List = None, secstruct: List = None, **kwargs) -> None:
        molname = kwargs.pop("molname", "molecule")
        nrexcl = kwargs.pop("nrexcl", 1)
        restraint_force = kwargs.pop("restraint_force", 1000)
        
        self.ff = forcefield
        self.sequence = sequence if sequence is not None else []
        self.name = molname
        self.nrexcl = nrexcl
        self.restraint_force = restraint_force
        self.atoms: List = []
        self.bonds = BondList()
        self.angles = BondList()
        self.dihs = BondList()
        self.cons = BondList()
        self.excls = BondList()
        self.pairs = BondList()
        self.vs3s = BondList()
        self.posres = BondList()
        self.elnet = BondList()
        self.mapping: List = []
        self.natoms = len(self.atoms)
        self.blist = [self.bonds, self.angles, self.dihs, self.cons, self.excls, self.pairs, self.vs3s]
        self.secstruct = secstruct if secstruct is not None else ["F"] * len(self.sequence)

    def __iadd__(self, other) -> "Topology":
        """Implement in-place addition of another Topology instance."""
        def update_atom(atom, atom_shift, residue_shift):
            new_atom = atom[:]
            new_atom[0] += atom_shift  # atom id
            new_atom[2] += residue_shift  # residue id
            new_atom[5] += atom_shift  # Update charge group number
            return new_atom
            
        def update_bond(bond, atom_shift):
            conn = bond[0]
            conn = [idx + atom_shift for idx in conn]
            return [conn, bond[1], bond[2]]
        
        atom_shift = self.natoms
        residue_shift = len(self.sequence)
        
        new_atoms = [update_atom(atom, atom_shift, residue_shift) for atom in other.atoms]
        self.atoms.extend(new_atoms)
        
        for self_attrib, other_attrib in zip(self.blist, other.blist):
            updated_bonds = [update_bond(bond, atom_shift) for bond in other_attrib]
            self_attrib.extend(updated_bonds)
        
        self.sequence.extend(other.sequence)
        self.secstruct.extend(other.secstruct)
        self.natoms = len(self.atoms)
        
        return self

    def lines(self, arguments="", timestamp="") -> list:
        """Generate the topology file as a list of lines."""
        lines = format_header(molname=self.name, forcefield=self.ff.name, arguments=arguments, timestamp=timestamp)
        lines += format_sequence_section(self.sequence, self.secstruct)
        lines += format_moleculetype_section(molname=self.name, nrexcl=self.nrexcl)
        lines += format_atoms_section(self.atoms)
        lines += format_bonded_section("bonds", self.bonds)
        lines += format_bonded_section("angles", self.angles)
        lines += format_bonded_section("dihedrals", self.dihs)
        lines += format_bonded_section("constraints", self.cons)
        lines += format_bonded_section("exclusions", self.excls)
        lines += format_bonded_section("pairs", self.pairs)
        lines += format_bonded_section("virtual_sites3", self.vs3s)
        lines += format_bonded_section("bonds", self.elnet)
        lines += format_posres_section(self.atoms, posres_fc=self.restraint_force)
        return lines

    def write_to_itp(self, filename: str, arguments="", timestamp=""):
        """Write the topology to an ITP file."""
        with open(filename, "w", encoding="utf-8") as file:
            for line in self.lines(arguments=arguments, timestamp=timestamp):
                file.write(line)

    @staticmethod
    def _update_bb_connectivity(conn, atid, reslen, prevreslen=None):
        """Update backbone connectivity indices for a residue."""
        result = []
        prev = -1
        for idx in conn:
            if idx < 0:
                if prevreslen is not None:
                    result.append(atid - prevreslen + idx + 3)
                    continue
                return list(conn)
            if idx > prev:
                result.append(atid + idx)
            else:
                result.append(atid + idx + reslen)
                atid += reslen
            prev = idx
        return result

    @staticmethod
    def _update_sc_connectivity(conn, atid):
        """Update sidechain connectivity indices."""
        return [atid + idx for idx in conn]

    def _check_connectivity(self, conn):
        """Check if the connectivity indices are within valid boundaries.

        Parameters
        ----------
        conn : list of int
            Connectivity indices to check.

        Returns
        -------
        bool
            True if all indices are between 1 and natoms, False otherwise.
        """
        for idx in conn:
            if idx < 1 or idx > self.natoms:
                return False
        return True

    def process_atoms(self, start_atom: int = 0, start_resid: int = 1):
        """Process atoms based on the sequence and force field definitions."""
        atid = start_atom
        resid = start_resid
        
        for resname in self.sequence:
            ff_atoms = self.ff.bb_atoms + self.ff.sc_atoms(resname)
            reslen = len(ff_atoms)
            itp_resname = self.ff._normalize_resname(resname)

            for ffatom in ff_atoms:
                atom = [
                    ffatom[0] + atid,    # atom id
                    ffatom[1],           # type
                    resid,               # residue id
                    itp_resname,         # residue name (normalized)
                    ffatom[2],           # name
                    ffatom[3] + atid,    # charge group
                    ffatom[4],           # charge
                    ffatom[5],           # mass
                    "",
                ]
                self.atoms.append(atom)
            atid += reslen
            resid += 1
        
        if self.atoms:
            self.atoms.pop(0)  # Remove dummy atom
        self.natoms = len(self.atoms)

    def process_bb_bonds(self, start_atom: int = 0, start_resid: int = 1):
        """Process backbone bonds using force field definitions.

        Parameters
        ----------
        start_atom : int, optional
            Starting atom ID.
        start_resid : int, optional
            Starting residue ID.
        """
        logger.debug(self.sequence)
        atid = start_atom
        resid = start_resid
        prevreslen = None
        for resname in self.sequence:
            reslen = len(self.ff.bb_atoms) + len(self.ff.sc_atoms(resname))
            ff_blist = self.ff.bb_blist
            for btype, ff_btype in zip(self.blist, ff_blist):
                for bond in ff_btype:
                    if bond:
                        connectivity = bond[0]
                        parameters = bond[1]
                        comment = bond[2]
                        upd_conn = self._update_bb_connectivity(connectivity, atid, reslen, prevreslen)
                        if self._check_connectivity(upd_conn):
                            upd_bond = [list(upd_conn), list(parameters), comment]
                            btype.append(upd_bond)
            prevreslen = reslen
            atid += reslen
            resid += 1

    def process_sc_bonds(self, start_atom: int = 0, start_resid: int = 1):
        """Process sidechain bonds using force field definitions.

        Parameters
        ----------
        start_atom : int, optional
            Starting atom ID.
        start_resid : int, optional
            Starting residue ID.
        """
        atid = start_atom
        resid = start_resid
        for resname in self.sequence:
            reslen = len(self.ff.bb_atoms) + len(self.ff.sc_atoms(resname))
            ff_blist = self.ff.sc_blist(resname)
            for btype, ff_btype in zip(self.blist, ff_blist):
                for bond in ff_btype:
                    if bond:
                        connectivity = bond[0]
                        parameters = bond[1]
                        comment = bond[2]
                        upd_conn = self._update_sc_connectivity(connectivity, atid)
                        if self._check_connectivity(upd_conn):
                            upd_bond = [list(upd_conn), list(parameters), comment]
                            btype.append(upd_bond)
            atid += reslen
            resid += 1

    def elastic_network(self, atoms, anames: List[str] = None, el: float = 0.5, eu: float = 1.1, ef: float = 500):
        """Construct an elastic network between selected atoms."""
        if anames is None:
            anames = ["BB1", "BB3"]
            
        def get_distance(v1, v2):
            return 0.1 * np.linalg.norm(np.array(v1) - np.array(v2))
        
        selected = [atom for atom in atoms if atom.name in anames]
        
        for a1 in selected:
            for a2 in selected:
                if a2.atid - a1.atid > 3:
                    v1 = a1.vec
                    v2 = a2.vec
                    d = get_distance(v1, v2)
                    if el < d < eu:
                        comment = f"{a1.resname}{a1.atid}-{a2.resname}{a2.atid}"
                        self.elnet.append([[a1.atid, a2.atid], [6, d, ef], comment])


###################################
## Main Functions ##
###################################

def martinize_dna(input_pdb, output_topology='molecule.itp', output_structure='molecule.pdb',
                            force_field='reg', molecule_name='molecule', merge_chains='yes',
                            elastic_network='yes', elastic_force=200, elastic_lower=0.3, 
                            elastic_upper=1.2, position_restraints='backbone', 
                            restraint_force=1000, debug=False, itp_dir=None):
    """
    Martinize DNA function that can be imported and used programmatically.
    
    Converts an all-atom DNA structure to coarse-grained Martini representation using
    embedded force field definitions and mapping logic without extednal dependencies.
    
    Parameters:
    -----------
    input_pdb : str
        Path to input all-atom DNA structure PDB file
    output_topology : str, optional
        Output topology file path (default: 'molecule.itp')
    output_structure : str, optional
        Output CG structure file path (default: 'molecule.pdb')
    force_field : str, optional
        Force field variant: 'reg' for regular (default: 'reg')
    molecule_name : str, optional
        Molecule name in topology file (default: 'molecule')
    merge_chains : str, optional
        Merge separate chains if detected: 'yes'/'no' (default: 'yes')
    elastic_network : str, optional
        Add elastic network: 'yes'/'no' (default: 'yes')
    elastic_force : float, optional
        Elastic network force constant in kJ/mol/nm² (default: 200)
    elastic_lower : float, optional
        Elastic network lower cutoff in nm (default: 0.3)
    elastic_upper : float, optional
        Elastic network upper cutoff in nm (default: 1.2)
    position_restraints : str, optional
        Position restraints: 'no'/'backbone'/'all' (default: 'backbone')
    restraint_force : float, optional
        Position restraint force constant in kJ/mol/nm² (default: 1000)
    debug : bool, optional
        Enable debug logging (default: False)
    itp_dir : str, optional
        Custom directory containing residue ITP files. If None, uses the default
        martinize_dna_v3.0.0_itps/ relative to the script directory.
    Returns:
    --------
    tuple : (str, str)
        Paths to generated structure and topology files
    """
    if debug:
        logger.setLevel(logging.DEBUG)
    
    logger.info("=== Starting DNA AA->CG Conversion ===")
    logger.info(f"Input PDB: {input_pdb}")
    logger.info(f"Output structure: {output_structure}")
    logger.info(f"Output topology: {output_topology}")
    logger.info(f"Force field: {force_field} (v3.0.0)")
    logger.info(f"Molecule name: {molecule_name}")
    logger.info(f"Elastic network: {elastic_network}")
    if itp_dir:
        logger.info(f"Custom ITP directory: {itp_dir}")
    
    if force_field == "reg":
        logger.info("Initializing Martini 3.0 DNA force field")
        ff = Martini30DNA(itp_dir=itp_dir)
    else:
        raise ValueError(f"Unsupported force field option: {force_field}")
    
    # Parse PDB file
    logger.info(f"Parsing PDB file: {input_pdb}")
    system = pdb2system(input_pdb)
    logger.info(f"Loaded system with {len(system.atoms)} atoms")
    
    logger.info("Moving O3' atoms to next residues")
    move_o3(system)  # Adjust O3 atoms as required
    
    # Count chains
    chains = list(system.chains())
    logger.info(f"Found {len(chains)} chains to process")
    
    # Process chains
    structure = AtomList()
    topologies = []
    start_idx = 1
    
    for i, chain in enumerate(chains):
        logger.info(f"Processing chain {i+1}/{len(chains)}")
        cg_atoms, chain_top = process_chain(chain, ff, start_idx, molecule_name, restraint_force)
        structure.extend(cg_atoms)
        topologies.append(chain_top)
        start_idx += len(cg_atoms)
    
    logger.info(f"Total CG atoms generated: {len(structure)}")
    
    # Write CG structure
    logger.info(f"Writing CG structure to: {output_structure}")
    structure.write_pdb(output_structure)
    
    # Merge topologies
    merged_topology = merge_topologies(topologies)
    
    # Add elastic network if requested
    if elastic_network == 'yes':
        logger.info(f"Adding elastic network (el={elastic_lower}, eu={elastic_upper}, ef={elastic_force})")
        merged_topology.elastic_network(
            structure,
            anames=["BB1", "BB3"],
            el=elastic_lower,
            eu=elastic_upper,
            ef=elastic_force,
        )
        logger.info(f"Added {len(merged_topology.elnet)} elastic bonds")
    
    # Generate arguments string for header
    timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    
    # Format the parsed arguments with their actual values
    args_formatted = (
        f"input_pdb='{input_pdb}', output_topology='{output_topology}', "
        f"output_structure='{output_structure}', force_field='{force_field}', "
        f"molecule_name='{molecule_name}', merge_chains='{merge_chains}', "
        f"elastic_network='{elastic_network}', elastic_force={elastic_force}, "
        f"elastic_lower={elastic_lower}, elastic_upper={elastic_upper}, "
        f"position_restraints='{position_restraints}', restraint_force={restraint_force}, "
        f"debug={debug}"
    )
    
    # Write topology file
    logger.info(f"Writing topology file to: {output_topology}")
    merged_topology.write_to_itp(output_topology, arguments=args_formatted, timestamp=timestamp)
    
    logger.info("=== DNA Martinization completed successfully ===")
    logger.info(f"Coarse-grained structure written to: {output_structure}")
    logger.info(f"Topology file written to: {output_topology}")
    
    return output_structure, output_topology


def process_chain(_chain, _ff, _start_idx, _mol_name, restraint_force=1000):
    """Process an individual DNA chain: map it to coarse-grained representation and generate a topology."""
    residues = list(_chain)
    sequence = [res.resname for res in residues]
    logger.info(f"Processing chain with {len(residues)} residues: {''.join(sequence[:10])}{'...' if len(sequence) > 10 else ''}")
    
    logger.debug(f"Mapping chain to CG representation starting at atom {_start_idx}")
    _cg_atoms = map_chain(_chain, _ff, atid=_start_idx)
    logger.debug(f"Generated {len(_cg_atoms)} CG atoms")
    
    logger.debug("Creating topology and processing bonded interactions")
    chain_topology = Topology(forcefield=_ff, sequence=sequence, molname=_mol_name, restraint_force=restraint_force)
    chain_topology.process_atoms()
    chain_topology.process_bb_bonds()
    chain_topology.process_sc_bonds()
    
    logger.info(f"Chain topology: {len(chain_topology.atoms)} atoms, "
               f"{len(chain_topology.bonds)} bonds, {len(chain_topology.angles)} angles, "
               f"{len(chain_topology.dihs)} dihedrals")
    
    return _cg_atoms, chain_topology


def merge_topologies(top_list):
    """Merge multiple Topology objects into one."""
    logger.info(f"Merging {len(top_list)} topology objects")
    _merged_topology = top_list.pop(0)
    for i, new_top in enumerate(top_list):
        logger.debug(f"Merging topology {i+2}/{len(top_list)+1}")
        _merged_topology += new_top
    
    logger.info(f"Merged topology: {len(_merged_topology.atoms)} atoms, "
               f"{len(_merged_topology.bonds)} bonds, {len(_merged_topology.angles)} angles, "
               f"{len(_merged_topology.dihs)} dihedrals")
    
    return _merged_topology


def main():
    """Command-line interface for standalone DNA martinization."""
    parser = argparse.ArgumentParser(
        description="Standalone Coarse-grained Martini 3.0 force field for DNA",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
WORKFLOW DESCRIPTION:
    This standalone script converts all-atom DNA structures to coarse-grained Martini 
    representation without requiring extednal reforge dependencies:
    1. Parse all-atom PDB structure and identify chains
    2. For each chain, map to coarse-grained beads (class NucleicForceField)
    3. Load sidechain parameters from the additional ITP files
    4. Generate topology with backbone and sidechain bonded interactions
    5. Merge chains to one ITP file if specified
    6. Optionally add elastic network for enhanced structTHYl stability
    7. Write coarse-grained structure and topology files

USAGE EXAMPLES:
    
    # Basic usage - minimal required arguments
    python martinize_dna_v3.0.0.py -f input.pdb

    # Full workflow with custom parameters
    python martinize_dna_v3.0.0.py -f input.pdb -ot topology.itp -os structure.pdb \\
        -mol my_dna -elastic yes -ef 250 -el 0.25 -eu 1.5
    
    # Using as Python module (programmatic usage):
    >>> from martinize_dna_v3.0.0 import martinize_dna
    >>> structure, topology = martinize_dna('input.pdb', debug=True)
    >>> print(f"Generated files: {structure}, {topology}")

INPUT REQUIREMENTS:
    - Input PDB: All-atom DNA structure with standard nucleotide naming
    - Structure should have proper chain organization and residue numbering
    - Supported bases: A, U, G, C (standard DNA nucleotides)

Please cite:
    Yangaliev D, Ozkan SB. Coarse-grained DNA model for the Martini 3 force field. 
    Biophys J. 2025 Aug 5:S0006-3495(25)00483-7. doi: 10.1016/j.bpj.2025.07.034.     
        """
    )
    
    parser.add_argument("-f", required=True, type=str, help="Input PDB file")
    parser.add_argument(
        "-ot",
        default="molecule.itp",
        type=str,
        help="Output topology file (default: molecule.itp)",
    )
    parser.add_argument(
        "-os",
        default="molecule.pdb",
        type=str,
        help="Output CG structure (default: molecule.pdb)",
    )
    parser.add_argument(
        "-ff",
        default="reg",
        type=str,
        help="Force field: regular or polar (reg/pol) (default: reg)",
    )
    parser.add_argument(
        "-mol",
        default="molecule",
        type=str,
        help="Molecule name in the .itp file (default: molecule)",
    )
    parser.add_argument(
        "-merge",
        default="yes",
        type=str,
        help="Merge separate chains if detected (default: yes)",
    )
    parser.add_argument(
        "-elastic",
        default="yes",
        type=str,
        help="Add elastic network (default: yes)",
    )
    parser.add_argument(
        "-ef",
        default=200,
        type=float,
        help="Elastic network force constant (default: 200 kJ/mol/nm^2)",
    )
    parser.add_argument(
        "-el",
        default=0.5,
        type=float,
        help="Elastic network lower cutoff (default: 0.5 nm)",
    )
    parser.add_argument(
        "-eu",
        default=1.2,
        type=float,
        help="Elastic network upper cutoff (default: 1.2 nm)",
    )
    parser.add_argument(
        "-p",
        default="backbone",
        type=str,
        help="Output position restraints (no/backbone/all) (default: backbone)",
    )
    parser.add_argument(
        "-pf",
        default=1000,
        type=float,
        help="Position restraints force constant. Defined if 'POSRES' (default: 1000 kJ/mol/nm^2)",
    )
    parser.add_argument(
        "-itp_dir",
        default=None,
        type=str,
        help="Custom directory containing residue ITP files (default: martinize_dna_v3.0.0_itps/ relative to script)",
    )
    parser.add_argument(
        "--debug",
        action="store_true",
        help="Enable debug logging",
    )
    
    args = parser.parse_args()
    
    # Call the main martinization function
    martinize_dna(
        input_pdb=args.f,
        output_topology=args.ot,
        output_structure=args.os,
        force_field=args.ff,
        molecule_name=args.mol,
        merge_chains=args.merge,
        elastic_network=args.elastic,
        elastic_force=args.ef,
        elastic_lower=args.el,
        elastic_upper=args.eu,
        position_restraints=args.p,
        restraint_force=args.pf,
        debug=args.debug,
        itp_dir=args.itp_dir
    )


if __name__ == "__main__":
    main()
