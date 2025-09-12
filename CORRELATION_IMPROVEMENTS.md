# Correlation Feature Improvements

## Overview
Enhanced the correlation window to provide better integration with the main application's property system and improved user experience.

## Changes Made

### 1. Integration with Main Property System
- Removed the separate property management system in the correlation component
- Now uses the main application's `DisplayProperty` system directly
- Filters properties to show only `Type_Temporal` (time-defined) properties
- Excludes `Type_Volume` and `Type_Distribution` properties as requested

### 2. Drag-and-Drop Functionality
- Added a Properties menu similar to timeline and distribution windows
- Properties can be dragged from the menu and dropped onto X and Y axes
- Uses the same `DisplayPropertyDragDropPayload` pattern as other windows
- Visual feedback shows which property is assigned to each axis

### 3. Always-Visible Scatter Plot
- Removed the "Generate Plot" button
- Scatter plot is always visible and updates automatically when properties are assigned
- Shows helpful instructions when no properties are assigned
- Automatically regenerates data when properties change

### 4. Enhanced User Interface
- Added menu bar with Properties menu
- Clear visual indicators for X and Y axis assignments
- Drop targets for both axes with descriptive text
- Better error handling and user feedback

### 5. State Persistence
- Property selections are now saved and restored with workspace
- Maintains selected X and Y properties across sessions

## Technical Details

### Files Modified
- `src/components/correlation/correlation.cpp` - Complete rewrite of the correlation component

### Key Implementation Changes
1. **Property Integration**: Uses `app_state->display_properties` instead of custom property system
2. **Drag-Drop**: Implements `CORRELATION_DND` payload type for property transfer
3. **Auto-Update**: Scatter plot updates immediately when properties are assigned via drag-drop
4. **Type Filtering**: Only shows temporal properties in the menu (time-defined data)

### User Workflow
1. Open Correlation window from main menu
2. Navigate to Properties menu to see available temporal properties
3. Drag a property from the menu and drop it on "X-Axis" area
4. Drag another property from the menu and drop it on "Y-Axis" area
5. Scatter plot automatically appears showing the correlation
6. Click on points to jump to specific frames in the animation

## Benefits
- Consistent interface with timeline and distribution windows
- More intuitive drag-and-drop workflow
- Better integration with existing property evaluation system
- Always-visible plot provides immediate feedback
- Only shows relevant time-defined properties