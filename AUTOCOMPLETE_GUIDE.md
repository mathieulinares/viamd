# VIAMD Script Editor Autocomplete Guide

## Overview
The VIAMD script editor now features intelligent autocomplete functionality that provides context-aware suggestions for writing molecular dynamics analysis scripts.

## Triggering Autocomplete

### Manual Trigger
- **Ctrl+Space**: Manually trigger suggestions at any cursor position

### Automatic Trigger
- **Smart activation**: Autocomplete appears automatically when typing 2+ identifier characters
- **Real-time updates**: Suggestions filter as you continue typing

## Navigation & Selection

### Keyboard Navigation
- **Up/Down arrows**: Navigate through suggestion list
- **Enter/Tab**: Insert selected completion
- **Escape**: Close autocomplete popup

### Visual Indicators
Suggestions are color-coded with icons for easy identification:
- **[K]** Keywords (purple): `and`, `or`, `not`, `in`, `out`
- **[F]** Built-in Functions (green): `distance`, `angle`, `sqrt`, `cos`, etc.
- **[D]** Dataset Items (cyan): Residue names, atom types, elements from your data

## Types of Suggestions

### 1. Keywords
Core language keywords for logical operations:
```viamd
s1 and s2    # logical AND
s1 or s2     # logical OR  
not s1       # logical NOT
```

### 2. Built-in Functions
Mathematical and molecular analysis functions:
```viamd
# Mathematical functions
sqrt(4)      # square root
cos(angle)   # cosine
abs(-5)      # absolute value

# Molecular functions  
distance(10, 30)           # distance between atoms
angle(1, 2, 3)            # angle between atoms
rmsd(selection)           # root mean square deviation
rdf(type1, type2, cutoff) # radial distribution function
```

### 3. Dataset-Specific Suggestions
Dynamically extracted from your loaded molecular data:

#### Residue Names
Based on residues present in your structure:
```viamd
resname("ALA")   # alanine residues
resname("VAL")   # valine residues
```

#### Atom Types
Based on atom names in your data:
```viamd
type("CA")   # alpha carbon atoms
type("N")    # nitrogen atoms
```

#### Chemical Elements
Based on elements present in your structure:
```viamd
element("C")   # carbon atoms
element("O")   # oxygen atoms
```

## Configuration

### Settings Menu
Access autocomplete settings via Script Editor → Settings:
- **Max Items**: Control how many suggestions are displayed (5-20)
- **Manual Trigger**: Button to trigger autocomplete manually
- **Status Display**: Shows completion count and autocomplete state

### Status Indicator
When the editor is focused, you'll see:
```
Completions: 45 | Press Ctrl+Space for autocomplete
```

## Smart Filtering

### Prefix Matching
- Case-insensitive filtering based on what you've typed
- Shows all items when no prefix is entered (Ctrl+Space on empty line)
- Filters to matching items as you type

### Relevance Sorting
1. **Exact matches** appear first
2. **Alphabetical ordering** for remaining items
3. **Type grouping** keeps similar items together

## Example Usage

```viamd
# Type "dist" then Ctrl+Space to see:
# [F] distance - Built-in function
# [F] distance_min - Built-in function  
# [F] distance_max - Built-in function

# Select with arrows and press Enter:
distance(10, 30)

# Type "ALA" to see residue suggestions:
# [D] ALA - residue name
resname("ALA")
```

## Dynamic Updates
The autocomplete system automatically refreshes when:
- New molecular data is loaded
- Different datasets are switched
- Trajectory files are opened

This ensures suggestions are always relevant to your current working data.

## Tips
1. **Start typing**: Don't wait for autocomplete, just start typing and it will appear
2. **Use prefixes**: Type the first few letters of what you're looking for
3. **Explore your data**: Use Ctrl+Space to discover what's available in your dataset
4. **Combine sources**: Mix built-in functions with your dataset-specific items

The autocomplete system makes VIAMD script writing more efficient and helps you discover available functions and data elements in your molecular systems.