"""
Integration modules for VIAMD

This subpackage provides high-level integration modules that build upon
the core VIAMD Python bindings to provide seamless workflows with
external molecular dynamics and analysis packages.

Available integrations:
    - openmm_integration: Advanced OpenMM molecular dynamics workflows
    - mdanalysis_integration: Advanced MDAnalysis analysis workflows
"""

from . import openmm_integration

# Import MDAnalysis integration if available
try:
    from . import mdanalysis_integration
    HAS_MDANALYSIS_INTEGRATION = True
except ImportError:
    HAS_MDANALYSIS_INTEGRATION = False

__all__ = ['openmm_integration']

if HAS_MDANALYSIS_INTEGRATION:
    __all__.append('mdanalysis_integration')