#!/usr/bin/env python3
"""
Setup script for VIAMD Python bindings.

This allows installation of the Python package via:
  pip install .

The actual compilation is handled by CMake/pybind11.
"""

from setuptools import setup, find_packages
import os

# Read version from CMake or fallback
version = "0.1.0"

# Read README if available
readme_path = os.path.join(os.path.dirname(__file__), "..", "README.md")
long_description = ""
if os.path.exists(readme_path):
    with open(readme_path, "r", encoding="utf-8") as f:
        long_description = f.read()

setup(
    name="pyviamd",
    version=version,
    author="VIAMD Development Team",
    author_email="mathieu.linares@kth.se",
    description="Python bindings for VIAMD - Visual Interactive Analysis of Molecular Dynamics",
    long_description=long_description,
    long_description_content_type="text/markdown",
    url="https://github.com/mathieulinares/viamd",
    project_urls={
        "Bug Tracker": "https://github.com/mathieulinares/viamd/issues",
        "Documentation": "https://github.com/mathieulinares/viamd/wiki",
        "Source Code": "https://github.com/mathieulinares/viamd",
    },
    classifiers=[
        "Development Status :: 3 - Alpha",
        "Intended Audience :: Science/Research",
        "Topic :: Scientific/Engineering :: Chemistry",
        "Topic :: Scientific/Engineering :: Visualization",
        "License :: OSI Approved :: MIT License",
        "Programming Language :: Python :: 3",
        "Programming Language :: Python :: 3.8",
        "Programming Language :: Python :: 3.9",
        "Programming Language :: Python :: 3.10",
        "Programming Language :: Python :: 3.11",
        "Programming Language :: Python :: 3.12",
        "Programming Language :: C++",
    ],
    keywords="molecular dynamics, visualization, analysis, chemistry, biophysics",
    python_requires=">=3.8",
    install_requires=[
        "numpy>=1.19.0",
    ],
    extras_require={
        "mdanalysis": ["MDAnalysis>=2.0.0"],
        "openmm": ["openmm>=7.7.0"],
        "full": ["MDAnalysis>=2.0.0", "openmm>=7.7.0", "matplotlib", "scipy"],
        "dev": ["pytest", "pytest-cov", "black", "flake8"],
    },
    # Note: The actual compiled module is installed by CMake
    # This setup.py is mainly for metadata and dependency management
    zip_safe=False,
    include_package_data=True,
)