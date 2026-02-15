#ifndef __ERROR
#define __ERROR

#include "mac_compat.h"

void HandleError(int id);

static inline void DoError(OSErr id)
{
	if(id) HandleError(id);
}

#endif