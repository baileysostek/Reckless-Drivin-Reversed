#ifndef __GAMEINITEXIT
#define __GAMEINITEXIT

#include "roads.h"

extern int gLevelID;
extern int gGameOn;
extern int gPlayerCarID;
extern int gSelectedCarID;  /* session car choice (set in Prefs, applied at StartGame) */

void DisposeLevel();
void StartGame(int);
int LoadLevel();
void EndGame();

/* Editor support: load road + fixed object data without starting a game.
 * Returns a malloc'd tRoad copy; also sets gLevelData/gTrackUp/gTrackDown.
 * outObjs receives a malloc'd tObjectPos[] (caller must free).
 * Both road and objects are byte-swapped to native order. */
tRoad EditorLoadRoadData(int levelID, UInt32 *outLen,
                         tObjectPos **outObjs, UInt32 *outNumObjs);

void EditorUnloadLevelPack(int levelID);

#endif