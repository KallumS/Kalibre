# 0045. Customisable keys and workspaces

## Decision

- **Keys**: every key the window answers is a named command with a default (`App/Keys`). The browser's Keys tab changes any: click, press the new key (Esc leaves it, Delete removes it). A key does one thing: giving it to a command takes it from another. Kept in Keys.json with the app; the menus show the keys as they are.
- **Workspaces**: View > Workspaces saves the layout (browser and its tab, the lower zone's tab and height, the editor, the lanes, the zoom, text size) by name and brings it back.
