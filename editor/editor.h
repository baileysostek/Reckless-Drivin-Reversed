/*
 * editor.h  —  Level editor C-compatible API.
 * Include from both C and C++ files.
 */
#ifndef EDITOR_H
#define EDITOR_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Open the level editor for the given level ID (0-indexed, same as gLevelID).
 * Blocks until the user exits the editor, then returns.
 */
void EditorEnter(int levelID);

/* Unload the level pack after the editor has copied all data.
 * This prevents a double byte-swap when LoadLevel loads the same pack later. */
void EditorUnloadLevelPack(int levelID);

#ifdef __cplusplus
}
#endif

#endif /* EDITOR_H */
