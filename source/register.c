#include <math.h>
#include <string.h>
#include "mac_compat.h"
#include "error.h"
#include "screen.h"
#include "interface.h"
#include "preferences.h"
#include "packs.h"
#include "initexit.h"

UInt32 gKey;
int gRegistered = 1; /* Always registered in SDL port */

int CheckRegi()
{
	gRegistered = 1;
	/* Key derived from Name="Free", Code="B3FB09B1EB" using original algorithm:
	 * seed = 0xEB -> 0xEBEBEBEB; codeNum = 0xB3FB09B1 ^ seed = 0x5810E25A;
	 * nameNum = "FREE" as BE UInt32 = 0x46524545; gKey = codeNum ^ nameNum */
	gKey = 0x1E42A71F;
	return gRegistered;
}

void Register(int fullscreen)
{
	(void)fullscreen;
	/* Registration not needed in SDL port */
}
