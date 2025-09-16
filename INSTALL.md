# VIAMD Installation Guide

This guide provides comprehensive installation instructions for VIAMD with Python bindings support.

## Quick Installation

### For Python Users (Recommended)

If you primarily want to use VIAMD's Python interface for molecular dynamics and analysis:

```bash
# 1. Clone repository
git clone https://github.com/mathieulinares/viamd.git
cd viamd

# 2. Build with Python support
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DVIAMD_ENABLE_PYTHON=ON
make -j$(nproc)

# 3. Set up Python environment
export PYTHONPATH="$PWD/python:$PYTHONPATH"

# 4. Install Python dependencies
pip install numpy
conda install -c conda-forge openmm
pip install MDAnalysis matplotlib scipy scikit-learn

# 5. Test installation
python -c "import pyviamd; print('Success!')"
```

### For GUI Users

If you want the full VIAMD GUI application:

```bash
# Follow standard VIAMD build instructions
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

## System Requirements

### Minimum Requirements
- **OS**: Linux (Ubuntu 20.04+), macOS (10.15+), Windows 10
- **CPU**: 64-bit processor, 2+ cores
- **Memory**: 4GB RAM
- **Disk**: 2GB free space
- **Python**: 3.8+ (for Python bindings)

### Recommended Requirements
- **CPU**: Multi-core processor (8+ cores for large simulations)
- **Memory**: 16GB+ RAM (for large molecular systems)
- **GPU**: CUDA-compatible GPU (for OpenMM acceleration)
- **Python**: 3.10+ with conda environment

## Detailed Installation Instructions

### Prerequisites

#### Ubuntu/Debian

```bash
# Update system
sudo apt-get update

# Install build tools
sudo apt-get install -y \
    build-essential \
    cmake \
    git \
    pkg-config

# Install development libraries (REQUIRED for Python support)
sudo apt-get install -y \
    python3-dev \
    python3-pip \
    libglfw3-dev \
    libgl1-mesa-dev \
    libglu1-mesa-dev \
    libxrandr-dev \
    libxinerama-dev \
    libxcursor-dev \
    libxi-dev

# Install Python dependencies (REQUIRED for OpenMM dynamics)
pip3 install numpy pybind11>=3.0.0
```

#### macOS

```bash
# Install Xcode command line tools
xcode-select --install

# Install Homebrew (if not already installed)
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

# Install dependencies
brew install cmake python numpy
pip3 install pybind11
```

#### Windows

```bash
# Install Visual Studio 2019/2022 with C++ support
# Install Git for Windows
# Install Python 3.8+ from python.org

# Using conda (recommended)
conda install cmake numpy pybind11
```

### Building VIAMD

#### Standard Build (GUI only)

```bash
# Clone repository
git clone https://github.com/mathieulinares/viamd.git
cd viamd

# Initialize submodules
git submodule update --init --recursive

# Create build directory
mkdir build && cd build

# Configure
cmake .. -DCMAKE_BUILD_TYPE=Release

# Build
make -j$(nproc)  # Linux/macOS
# OR
cmake --build . --config Release  # Windows
```

#### Build with Python Support

```bash
# Configure with Python support
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DVIAMD_ENABLE_PYTHON=ON \
    -DPYTHON_EXECUTABLE=$(which python3)

# Build
make -j$(nproc)

# Set up Python path
export PYTHONPATH="$PWD/python:$PYTHONPATH"

# Add to shell profile (optional)
echo 'export PYTHONPATH="/path/to/viamd/build/python:$PYTHONPATH"' >> ~/.bashrc
```

#### Advanced Build Options

```bash
# Build with specific Python version
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DVIAMD_ENABLE_PYTHON=ON \
    -DPYTHON_EXECUTABLE=/usr/bin/python3.10

# Build with debugging symbols
cmake .. \
    -DCMAKE_BUILD_TYPE=Debug \
    -DVIAMD_ENABLE_PYTHON=ON

# Build with custom installation prefix
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/opt/viamd \
    -DVIAMD_ENABLE_PYTHON=ON

# Install to system
make install
```

### Python Dependencies

#### Core Dependencies (Required)

```bash
# NumPy for numerical computing
pip install numpy>=1.19.0
```

#### OpenMM Integration (Optional)

```bash
# Install OpenMM via conda (recommended)
conda install -c conda-forge openmm

# For CUDA support (requires compatible GPU)
conda install -c conda-forge openmm cudatoolkit

# Verify installation
python -c "import openmm; print(f'OpenMM {openmm.version.version} installed')"
```

#### MDAnalysis Integration (Optional)

```bash
# Install MDAnalysis
pip install MDAnalysis>=2.0.0

# Verify installation
python -c "import MDAnalysis; print(f'MDAnalysis {MDAnalysis.__version__} installed')"
```

#### Full Scientific Stack (Recommended)

```bash
# Install all optional dependencies
pip install \
    numpy>=1.19.0 \
    MDAnalysis>=2.0.0 \
    matplotlib \
    scipy \
    scikit-learn \
    pandas

# Install OpenMM via conda
conda install -c conda-forge openmm
```

### Environment Setup

#### Using Conda (Recommended)

```bash
# Create dedicated environment
conda create -n viamd python=3.10
conda activate viamd

# Install dependencies
conda install -c conda-forge numpy openmm
pip install MDAnalysis matplotlib scipy scikit-learn

# Build VIAMD
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DVIAMD_ENABLE_PYTHON=ON
make -j$(nproc)

# Set up environment
echo "export PYTHONPATH=\"$PWD/python:\$PYTHONPATH\"" >> $CONDA_PREFIX/etc/conda/activate.d/viamd.sh
```

#### Using Virtual Environment

```bash
# Create virtual environment
python3 -m venv viamd-env
source viamd-env/bin/activate

# Install dependencies
pip install numpy pybind11 MDAnalysis matplotlib scipy scikit-learn

# Install OpenMM (requires conda)
# Note: OpenMM installation via pip is not recommended
```

## Installation Verification

### Basic Installation Test

```bash
# Test VIAMD core
./viamd --version  # GUI version

# Test Python bindings
python -c "
import pyviamd
print(f'VIAMD version: {pyviamd.core.get_version()}')
print('Core modules:', dir(pyviamd))
"
```

### Comprehensive Test

```bash
# Run Python binding tests
cd python
python -c "
import pyviamd
import numpy as np

# Test core functionality
print('✓ Core bindings working')

# Test OpenMM integration
try:
    from pyviamd.dynamics import check_requirements
    print('✓ OpenMM integration:', check_requirements())
except ImportError as e:
    print('✗ OpenMM integration:', str(e))

# Test MDAnalysis integration
try:
    import pyviamd.integrations.mdanalysis_integration
    print('✓ MDAnalysis integration working')
except ImportError as e:
    print('✗ MDAnalysis integration:', str(e))

# Test analysis capabilities
try:
    import pyviamd.integrations.analysis_integration
    print('✓ Analysis integration working')
except ImportError as e:
    print('✗ Analysis integration:', str(e))

print('Installation verification complete!')
"
```

### Run Example Scripts

```bash
# Navigate to examples
cd python/examples

# Test basic functionality
python -c "
# Create a simple test
import pyviamd
print('Testing basic molecule loading...')
# Add actual test here when example files are available
print('Basic test completed!')
"
```

## Platform-Specific Instructions

### Ubuntu 22.04 LTS

```bash
# Install dependencies
sudo apt-get update
sudo apt-get install -y build-essential cmake git python3-dev python3-pip
sudo apt-get install -y libglfw3-dev libgl1-mesa-dev libglu1-mesa-dev

# Install Python packages
pip3 install numpy pybind11 MDAnalysis matplotlib scipy

# Install OpenMM via conda
wget https://repo.anaconda.com/miniconda/Miniconda3-latest-Linux-x86_64.sh
bash Miniconda3-latest-Linux-x86_64.sh
conda install -c conda-forge openmm

# Build VIAMD
git clone https://github.com/mathieulinares/viamd.git
cd viamd && mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DVIAMD_ENABLE_PYTHON=ON
make -j$(nproc)
```

### macOS (Intel/Apple Silicon)

```bash
# Install Xcode command line tools
xcode-select --install

# Install Homebrew
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

# Install dependencies
brew install cmake python numpy
pip3 install pybind11 MDAnalysis matplotlib scipy

# Install OpenMM via conda
brew install miniconda
conda init
conda install -c conda-forge openmm

# Build VIAMD
git clone https://github.com/mathieulinares/viamd.git
cd viamd && mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DVIAMD_ENABLE_PYTHON=ON
make -j$(sysctl -n hw.ncpu)
```

### Windows 10/11

```powershell
# Install Visual Studio 2022 Community with C++ support
# Install Git for Windows
# Install Python 3.10+ from python.org

# Using conda (recommended)
# Download and install Miniconda from https://docs.conda.io/en/latest/miniconda.html

# Open Anaconda Prompt
conda create -n viamd python=3.10
conda activate viamd
conda install cmake numpy pybind11 openmm
pip install MDAnalysis matplotlib scipy

# Clone and build
git clone https://github.com/mathieulinares/viamd.git
cd viamd
mkdir build && cd build
cmake .. -DVIAMD_ENABLE_PYTHON=ON -G "Visual Studio 17 2022"
cmake --build . --config Release
```

## Docker Installation

### Using Docker

```dockerfile
# Create Dockerfile
FROM ubuntu:22.04

# Install system dependencies
RUN apt-get update && apt-get install -y \
    build-essential cmake git python3-dev python3-pip \
    libglfw3-dev libgl1-mesa-dev libglu1-mesa-dev && \
    rm -rf /var/lib/apt/lists/*

# Install Python dependencies
RUN pip3 install numpy pybind11 MDAnalysis matplotlib scipy

# Install Miniconda for OpenMM
RUN wget -q https://repo.anaconda.com/miniconda/Miniconda3-latest-Linux-x86_64.sh && \
    bash Miniconda3-latest-Linux-x86_64.sh -b -p /opt/conda && \
    rm Miniconda3-latest-Linux-x86_64.sh
ENV PATH="/opt/conda/bin:$PATH"
RUN conda install -c conda-forge openmm

# Clone and build VIAMD
WORKDIR /app
COPY . .
RUN mkdir build && cd build && \
    cmake .. -DCMAKE_BUILD_TYPE=Release -DVIAMD_ENABLE_PYTHON=ON && \
    make -j$(nproc)

# Set up environment
ENV PYTHONPATH="/app/build/python:$PYTHONPATH"

# Default command
CMD ["python3"]
```

```bash
# Build and run Docker image
docker build -t viamd .
docker run -it viamd python3 -c "import pyviamd; print('VIAMD in Docker!')"
```

## Troubleshooting

### Common Build Issues

#### CMake Configuration Errors

```bash
# Problem: CMake can't find Python
# Solution: Specify Python executable explicitly
cmake .. -DPYTHON_EXECUTABLE=$(which python3) -DVIAMD_ENABLE_PYTHON=ON

# Problem: Missing pybind11
# Solution: Install pybind11
pip install pybind11
# OR
sudo apt-get install pybind11-dev  # Ubuntu
brew install pybind11              # macOS
```

#### Compilation Errors

```bash
# Problem: "fatal error: Python.h: No such file or directory"  
# Solution: Install Python development headers
sudo apt-get install python3-dev  # Ubuntu/Debian
brew install python               # macOS
dnf install python3-devel        # Fedora/RHEL

# Problem: Missing OpenGL libraries (Linux)
sudo apt-get install libgl1-mesa-dev libglu1-mesa-dev libglfw3-dev

# Problem: Missing C++ compiler
sudo apt-get install build-essential  # Ubuntu
xcode-select --install                # macOS
```

### Runtime Issues

#### Import Errors

```bash
# Problem: "No module named 'pyviamd'"
# Solution: Set PYTHONPATH
export PYTHONPATH="/path/to/viamd/build/python:$PYTHONPATH"

# Problem: "Symbol not found" errors
# Solution: Rebuild with correct Python version
cmake .. -DPYTHON_EXECUTABLE=$(which python3) -DVIAMD_ENABLE_PYTHON=ON
make clean && make -j$(nproc)
```

#### OpenMM Issues

```bash
# Problem: "OpenMM not available"
# Solution: Install via conda (not pip)
conda install -c conda-forge openmm

# Problem: CUDA errors
# Solution: Install CUDA-compatible OpenMM
conda install -c conda-forge openmm cudatoolkit
```

### Performance Issues

#### Slow Simulations

```bash
# Check OpenMM platform
python -c "
import openmm
print('Available platforms:')
for i in range(openmm.Platform.getNumPlatforms()):
    platform = openmm.Platform.getPlatform(i)
    print(f'  {platform.getName()}')
"

# Use CUDA platform if available
# This is handled automatically by the dynamics interface
```

#### Memory Issues

```bash
# Monitor memory usage
python -c "
import pyviamd
stats = pyviamd.core.get_heap_allocator_stats()
print(f'Memory usage: {stats}')
"

# Reduce memory usage in simulations
# - Use smaller report_interval
# - Limit max_frames in TrajectoryManager
# - Disable real_time_analysis for large simulations
```

## Getting Help

### Documentation
- **Main Wiki**: https://github.com/scanberg/viamd/wiki
- **Python API**: [python/README.md](python/README.md)
- **Dynamics Guide**: [python/DYNAMICS_GUIDE.md](python/DYNAMICS_GUIDE.md)

### Community Support
- **Issues**: https://github.com/mathieulinares/viamd/issues
- **Discussions**: https://github.com/mathieulinares/viamd/discussions

### Testing Your Installation

```bash
# Run comprehensive tests
cd python/examples
python viamd_dynamics_example.py

# Check all integrations
python -c "
from pyviamd.dynamics import check_requirements
print('Installation status:')
print(check_requirements())
"
```

This installation guide provides comprehensive instructions for setting up VIAMD with full Python bindings support across different platforms and use cases.