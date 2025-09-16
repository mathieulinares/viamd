#!/bin/bash

# VIAMD OpenMM Dynamics Diagnostics Script
# This script helps diagnose why the OpenMM Dynamics window might not appear

echo "=== VIAMD OpenMM Dynamics Diagnostics ==="
echo

# Check if build directory exists
if [ ! -d "build" ]; then
    echo "❌ No build directory found. Please run:"
    echo "   mkdir build && cd build"
    echo "   cmake .. -DCMAKE_BUILD_TYPE=Release -DVIAMD_ENABLE_PYTHON=ON"
    echo "   make -j\$(nproc)"
    exit 1
fi

cd build

# Check CMake cache for Python setting
echo "🔍 Checking CMake configuration..."
if grep -q "VIAMD_ENABLE_PYTHON:BOOL=ON" CMakeCache.txt 2>/dev/null; then
    echo "✅ VIAMD_ENABLE_PYTHON is ON"
else
    echo "❌ VIAMD_ENABLE_PYTHON is not enabled or CMakeCache.txt not found"
    echo "   Please reconfigure with: cmake .. -DCMAKE_BUILD_TYPE=Release -DVIAMD_ENABLE_PYTHON=ON"
    exit 1
fi

# Check if OpenMM component was compiled
echo
echo "🔍 Checking if OpenMM dynamics component was compiled..."
if [ -f "CMakeFiles/viamd.dir/src/components/openmm_dynamics/openmm_dynamics.cpp.o" ]; then
    echo "✅ OpenMM dynamics component object file exists"
else
    echo "❌ OpenMM dynamics component was not compiled"
    echo "   Please run: make -j\$(nproc)"
    exit 1
fi

# Check if VIAMD binary exists
echo
echo "🔍 Checking if VIAMD binary exists..."
if [ -f "bin/viamd" ] || [ -f "viamd" ]; then
    echo "✅ VIAMD binary found"
    VIAMD_BIN="bin/viamd"
    if [ ! -f "$VIAMD_BIN" ]; then
        VIAMD_BIN="viamd"
    fi
else
    echo "❌ VIAMD binary not found"
    echo "   Please run: make -j\$(nproc)"
    exit 1
fi

# Check if Python dependencies are available
echo
echo "🔍 Checking Python dependencies..."
python3 -c "import numpy; print('✅ numpy available')" 2>/dev/null || echo "⚠️  numpy not found (pip install numpy)"
python3 -c "import pybind11; print('✅ pybind11 available')" 2>/dev/null || echo "⚠️  pybind11 not found (pip install pybind11)"

# Check if binary has Python symbols
echo
echo "🔍 Checking if VIAMD binary includes Python support..."
if nm "$VIAMD_BIN" 2>/dev/null | grep -q "Python\|pybind"; then
    echo "✅ VIAMD binary includes Python symbols"
else
    echo "❌ VIAMD binary does not include Python symbols"
    echo "   The binary was likely compiled without Python support"
    echo "   Please clean and rebuild:"
    echo "   make clean"
    echo "   cmake .. -DCMAKE_BUILD_TYPE=Release -DVIAMD_ENABLE_PYTHON=ON"
    echo "   make -j\$(nproc)"
    exit 1
fi

# Check if OpenMM symbols are present
echo
echo "🔍 Checking if OpenMM dynamics symbols are present..."
if nm "$VIAMD_BIN" 2>/dev/null | grep -q "OpenMM"; then
    echo "✅ VIAMD binary includes OpenMM dynamics symbols"
else
    echo "❌ VIAMD binary does not include OpenMM dynamics symbols"
    echo "   Please rebuild with:"
    echo "   make clean"
    echo "   make -j\$(nproc)"
    exit 1
fi

echo
echo "=== Diagnostics Summary ==="
echo "✅ All checks passed! The OpenMM Dynamics window should be available."
echo
echo "📋 Troubleshooting steps if window still not visible:"
echo "1. Start VIAMD: ./$VIAMD_BIN"
echo "2. Load a molecular structure (File -> Load File)"
echo "3. Go to Windows menu -> look for 'OpenMM Dynamics'"
echo "4. If not there, check the console for any error messages"
echo
echo "🔧 If the window is still missing, the issue might be:"
echo "- Runtime Python initialization failure"
echo "- Missing Python dependencies at runtime"
echo "- Display/GUI initialization issue"
echo
echo "💡 To test Python integration manually:"
echo "   export PYTHONPATH=\"\$PWD/python:\$PYTHONPATH\""
echo "   python3 -c \"import pyviamd; print('Python integration works!')\""