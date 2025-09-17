"""
Integration modules for VIAMD

This subpackage provides high-level integration modules that build upon
the core VIAMD Python bindings to provide seamless workflows with
external molecular dynamics and analysis packages.

Available integrations:
    - openmm_integration: Advanced OpenMM molecular dynamics workflows
    - mdanalysis_integration: Advanced MDAnalysis analysis workflows
    - analysis_integration: Advanced molecular analysis and data processing
    - visualization_integration: Advanced visualization and plotting tools
    - ml_integration: Machine learning workflows for molecular data
"""

from . import openmm_integration

# Import MDAnalysis integration if available
try:
    from . import mdanalysis_integration
    HAS_MDANALYSIS_INTEGRATION = True
except ImportError:
    HAS_MDANALYSIS_INTEGRATION = False

# Import advanced analysis integration
try:
    from . import analysis_integration
    HAS_ANALYSIS_INTEGRATION = True
except ImportError:
    HAS_ANALYSIS_INTEGRATION = False

# Import visualization integration
try:
    from . import visualization_integration
    HAS_VISUALIZATION_INTEGRATION = True
except ImportError:
    HAS_VISUALIZATION_INTEGRATION = False

# Import ML integration
try:
    from . import ml_integration
    HAS_ML_INTEGRATION = True
except ImportError:
    HAS_ML_INTEGRATION = False

__all__ = ['openmm_integration']

if HAS_MDANALYSIS_INTEGRATION:
    __all__.append('mdanalysis_integration')

if HAS_ANALYSIS_INTEGRATION:
    __all__.append('analysis_integration')

if HAS_VISUALIZATION_INTEGRATION:
    __all__.append('visualization_integration')

if HAS_ML_INTEGRATION:
    __all__.append('ml_integration')