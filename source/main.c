#include <SDL.h>
#include "initexit.h"
#include "gameframe.h"
#include "interface.h"
#include "gameinitexit.h"

#ifdef __APPLE__
#include <unistd.h>
#include <libgen.h>
#include <mach-o/dyld.h>

static void SetBundleWorkingDirectory(void)
{
	char execPath[4096];
	uint32_t size = sizeof(execPath);
	if (_NSGetExecutablePath(execPath, &size) == 0) {
		char *dir = dirname(execPath);
		/* Go from MacOS/ up to Resources/ */
		char resources[4096];
		snprintf(resources, sizeof(resources), "%s/../Resources", dir);
		chdir(resources);
	}
}
#endif

int main(int argc, char *argv[])
{
	(void)argc;
	(void)argv;
#ifdef __APPLE__
	SetBundleWorkingDirectory();
#endif
	Init();
	while(!gExit)
		if(gGameOn) GameFrame();
		else Eventloop();
	Exit();
	return 0;
}
