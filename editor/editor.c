/*
 * editor.c  —  Minimal C-side helpers for the level editor.
 * The bulk of editor logic (ImGui UI + save/load) is in editor_ui.cpp.
 */
#include <stdio.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#endif

/* Ensure the "saves" directory exists. */
void EditorEnsureSavesDir(void)
{
#ifdef _WIN32
    _mkdir("saves");
#else
    mkdir("saves", 0755);
#endif
}
