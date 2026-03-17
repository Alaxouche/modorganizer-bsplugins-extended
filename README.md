# Bethesda Plugin Manager - Extended for Mod Organizer
Manages plugin load order for Bethesda Game Studios game engines

Rework and recompilation of many functions of the base mo2 addon for more stability, bug fixing and removal of annoying features.
basically most of what was done:
- Improved stability and startup reliability for the plugin panel
- Added safer and clearer group management tools
- Added `Reset Groups` to clear group structure without changing load order
- Added `Merge Group` to combine two groups in one action
- Added `Clean Groups` to remove empty groups and a new prompt on right click "Remove Group..." 
- Added optional confirmation prompts for risky actions (removing separator etc)
- Improved drag and drop behavior for full group moves
- Improved performance on large load orders
- Fixed several context menu and group action visibility issues
- Added restore behavior for load order snapshots after external runs
- Added protection against unwanted external load order changes 
- Added option to fully disable the group system
- Restaured the "Lock Load Order Position" option for the plugins. Base Bsplugin.dll removed this option
- Compiled and compatible for Mod Organizer 2.5.2