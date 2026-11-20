# 0098. The clip launcher taken out

## Context

Asked (19 November, night): remove the clip launcher from the lower zone
("Launcher"); the musician isn't a fan of the feature. No one uses Kalibre
yet, so nothing depends on it.

## Decision

Taken out whole, not only hidden, so nothing is left to keep working:

- The lower zone's **Launcher** tab and its view (`App/LauncherView`), the
  **G** key, **View > Clip Launcher**, and the clip menu's **Put in the
  Launcher**.
- Behind them: the Controller's launching, the launch states kept across
  Undo, `song::` slots, scenes, launching and Back to Arrangement,
  Playback's launched slots, `Track::slots`, `Track::launch` and
  `Project::scenes`.
- A song saved with slots or scenes still opens: the file's `slots` and
  `scenes` are passed over. DAWproject export still writes its empty
  `Scenes`, which the format asks for.
- `ZoneTab` keeps its numbers (workspaces save them): 3, the launcher's, is
  left unused, and a workspace saved showing it opens on Devices.
- The lower zone's tabs are Devices, Editor, Mixer and Meters (and Video
  when the song has one). G is free for another command (Help > Keys).

## Consequences

- Tested: no Launcher tab in the window, G changes nothing, an old
  workspace's launcher opens Devices and its Meters still opens Meters. The
  two launcher tests (a launched slot, a captured scene) went with it.
- Sections, ripple, alias clips and groups (0037) are unchanged.
- Every picture in the guide drawn again; the launcher's removed (render 12).
