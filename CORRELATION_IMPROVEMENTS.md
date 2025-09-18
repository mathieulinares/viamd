# Correlation Feature Improvements

## Overview
Enhanced the correlation window to provide better integration with the main application's property system and improved user experience, as requested in the problem statement.

## Problem Statement Requirements Met

### ✅ Always Display Scatter Plot
- Removed the "Generate Plot" button requirement
- Scatter plot is now always visible and updates automatically
- Shows helpful instructions when no properties are assigned

### ✅ Properties Menu Integration
- Added Properties menu similar to timeline and distribution windows
- Properties are now available in a dropdown menu accessible via menu bar
- Consistent interface with existing analysis windows

### ✅ Time-Defined Properties Only
- Filters properties to show only `Type_Temporal` (time-defined) properties
- Excludes `Type_Volume` and `Type_Distribution` properties as requested
- Only shows properties that make sense for correlation analysis

### ✅ Drag-and-Drop Functionality
- Properties can be dragged from the Properties menu
- Drop targets for both X and Y axes with clear visual feedback
- Implements the same drag-drop pattern as timeline and distribution windows
- Auto-updates scatter plot when properties are assigned

## Technical Implementation

### Files Modified
- `src/components/correlation/correlation.cpp` - Complete rewrite with 378 lines (vs 373 original)

### Key Architecture Changes
1. **Integration with Main Property System**: Now uses `app_state->display_properties` directly instead of maintaining separate property list
2. **Consistent UI Pattern**: Follows the same menu and drag-drop patterns as timeline/distribution windows
3. **Type Safety**: Only processes `DisplayProperty::Type_Temporal` properties
4. **Automatic Updates**: Scatter plot regenerates immediately when properties change

### New Features
- **Properties Menu**: Access to all temporal properties via menu bar
- **Visual Drop Targets**: Clear indicators showing where to drop properties
- **State Persistence**: Property selections saved/restored with workspace
- **Better Error Handling**: Clear error messages for incompatible properties
- **Enhanced Tooltips**: Point hover shows frame info and click-to-navigate

### User Workflow
1. Open Correlation window from main menu
2. Click Properties menu to see available temporal properties
3. Drag property from menu → drop on "X-Axis" area
4. Drag another property from menu → drop on "Y-Axis" area  
5. Scatter plot automatically appears showing correlation
6. Hover over points for details, click to jump to frame

## Benefits
- **Improved UX**: No manual plot generation required
- **Consistency**: Matches existing analysis window patterns
- **Better Integration**: Uses main application's property evaluation system
- **Time-Focused**: Only shows relevant time-defined properties
- **Always Visible**: Immediate visual feedback for correlations