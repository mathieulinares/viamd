# Script Editor Autocomplete Feature

This document describes the new autocomplete/suggestion functionality added to the VIAMD script editor.

## Overview

The script editor now provides intelligent autocomplete suggestions that help users discover and use available functions and keywords in the VIAMD scripting language.

## Features

### Triggering Autocomplete
- **Ctrl+Space**: Manually trigger autocomplete suggestions at the current cursor position
- **Automatic**: Suggestions update automatically as you type

### Navigation
- **Up/Down Arrow Keys**: Navigate through the suggestion list
- **Enter or Tab**: Insert the selected completion
- **Escape**: Close the autocomplete popup

### Suggestion Types
The autocomplete system provides two types of suggestions:

1. **Keywords** (marked with [K]): Language keywords like `and`, `or`, `not`, `in`, `out`
2. **Built-in Functions** (marked with [F]): VIAMD functions like:
   - Mathematical functions: `sqrt`, `cos`, `sin`, `abs`, `log`, etc.
   - Vector operations: `dot`, `cross`, `vec2`, `vec3`, `vec4`
   - Molecular analysis: `atoms`, `residues`, `chains`, `distance`, `angle`, `rdf`, `sdf`
   - Selection functions: `all`, `type`, `name`, `element`, `resname`, `within`
   - And many more...

### Smart Filtering
- Suggestions are filtered based on the current word being typed
- Case-insensitive matching
- Prefix-based filtering (shows functions that start with the typed text)
- Relevance-based sorting (exact matches first, then alphabetical)

### Visual Design
- Clean popup window with styled suggestions
- Color-coded icons to distinguish between keywords and functions
- Additional detail information for functions
- Responsive positioning near the cursor

## Usage Examples

### Basic Usage
1. Start typing a function name, e.g., `dist`
2. Press **Ctrl+Space** to see available completions
3. Use **Up/Down** arrows to select `distance`
4. Press **Enter** to insert the function

### Discovering Functions
1. Press **Ctrl+Space** in an empty area
2. Browse through all available functions and keywords
3. Select one to learn about available functionality

### Quick Completion
1. Type the first few letters of a known function, e.g., `ang`
2. The autocomplete will automatically show `angle` 
3. Press **Tab** to quickly complete

## Implementation Details

### New TextEditor Methods
- `ShowAutoComplete(bool)`: Show/hide the autocomplete popup
- `IsAutoCompleteShown()`: Check if autocomplete is currently visible
- `TriggerAutoComplete()`: Manually trigger autocomplete
- `SetAutoCompleteMaxItems(int)`: Configure maximum displayed items

### Integration
The autocomplete functionality is seamlessly integrated into the existing TextEditor:
- Updates automatically on text changes
- Hides when typing whitespace or when no matches are found
- Respects the current language definition (VIAMD)
- Works with existing keyboard shortcuts and editor features

## Technical Implementation

The autocomplete system:
1. **Builds completion items** from the VIAMD language definition on initialization
2. **Filters suggestions** based on the current word prefix
3. **Renders a popup window** with styled suggestions near the cursor
4. **Handles keyboard input** for navigation and selection
5. **Integrates with text editing** for seamless completion insertion

This feature significantly improves the user experience when writing VIAMD scripts by providing discoverability of available functions and reducing typing errors.