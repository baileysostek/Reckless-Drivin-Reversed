#include <stdio.h>
#include <stdlib.h>
#include "mac_compat.h"
#include "initexit.h"
#include "screen.h"

void HandleError(int id)
{
	fprintf(stderr, "Fatal error: %d\n", id);
	Exit();
	exit(1);
}
