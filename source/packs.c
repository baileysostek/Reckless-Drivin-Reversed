#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "mac_compat.h"
#include "endian_compat.h"
#include "packs.h"
#include "lzrw.h"
#include "register.h"
#include "interface.h"

typedef struct{
	SInt16 id;
	SInt16 placeHolder;
	UInt32 offs;
}tPackHeader;
typedef tPackHeader **tPackHandle;

Handle gPacks[kNumPacks];
#define kUnCryptedHeader 256

static void SwapPackHeaders(Handle pack)
{
	tPackHeader *hdr = (tPackHeader*)*pack;
	int i, numEntries;
	long handleSize = GetHandleSize(pack);

	/* First entry's id field is the count of entries */
	hdr->id = SwapS16(hdr->id);
	hdr->offs = SwapU32(hdr->offs);
	numEntries = hdr->id;

	if(numEntries < 0 || (numEntries+1) * (long)sizeof(tPackHeader) > handleSize) {
		fprintf(stderr, "[SwapPackHeaders] ERROR: numEntries=%d is invalid for handleSize=%ld!\n",
			numEntries, handleSize);
		return;
	}

	for(i = 1; i <= numEntries; i++)
	{
		hdr[i].id = SwapS16(hdr[i].id);
		hdr[i].offs = SwapU32(hdr[i].offs);
	}
}

UInt32 CryptData(UInt32 *data,UInt32 len)
{
	UInt32 check=0;
	UInt32 beKey;

	/* CryptData XOR operates on big-endian words in the buffer.
	 * On little-endian, we need to swap gKey to big-endian for XOR,
	 * since the data is stored big-endian. */
	beKey = SwapU32(gKey);

	data+=kUnCryptedHeader/4;
	len-=kUnCryptedHeader;
	while(len>=4)
	{
		*data^=beKey;
		check+=*data;
		data++;
		len-=4;
   	}
	if(len)
	{
		UInt8 *bp = (UInt8*)data;
		bp[0] ^= (gKey>>24)&0xff;
		check += ((UInt32)bp[0])<<24;
		if(len>1)
		{
			bp[1] ^= (gKey>>16)&0xff;
			check += ((UInt32)bp[1])<<16;
			if(len>2)
			{
				bp[2] ^= (gKey>>8)&0xff;
				check += ((UInt32)bp[2])<<8;
			}
		}
	}
	return check;
}

/* Load a pack resource from assets/pack_NNN.bin */
static Handle LoadPackFromFile(int resID)
{
	char path[256];
	FILE *f;
	long size;
	Handle h;

	snprintf(path, sizeof(path), "assets/pack_%d.bin", resID);
	f = fopen(path, "rb");
	if(!f) return NULL;

	fseek(f, 0, SEEK_END);
	size = ftell(f);
	fseek(f, 0, SEEK_SET);

	h = NewHandle(size);
	if(!h) { fclose(f); return NULL; }

	fread(*h, 1, size, f);
	fclose(f);
	return h;
}

UInt32 LoadPack(int num)
{
	UInt32 check=0;
	if(!gPacks[num])
	{
		gPacks[num]=LoadPackFromFile(num+128);
		if(gPacks[num])
		{
			if(num>=kEncryptedPack||gLevelResFile)
				check=CryptData((UInt32*)*gPacks[num],GetHandleSize(gPacks[num]));
			LZRWDecodeHandle(&gPacks[num]);
			fprintf(stderr, "[LoadPack] Pack %d: %ld bytes\n", num, (long)GetHandleSize(gPacks[num]));
			HLockHi(gPacks[num]);
			SwapPackHeaders(gPacks[num]);
		}
		else
			fprintf(stderr, "[LoadPack] ERROR: Failed to load pack_%d.bin\n", num+128);
	}
	return check;
}

int CheckPack(int num,UInt32 check)
{
	/* Always return true in SDL port (game is registered) */
	(void)num;
	(void)check;
	return 1;
}

void UnloadPack(int num)
{
	if(gPacks[num])
	{
		DisposeHandle(gPacks[num]);
		gPacks[num]=nil;
	}
}

Ptr GetSortedPackEntry(int packNum,int entryID,int *size)
{
	tPackHeader *pack;
	int startId, idx;
	UInt32 offs;
	if(!gPacks[packNum]) {
		fprintf(stderr, "[GetSortedPackEntry] ERROR: gPacks[%d] is NULL!\n", packNum);
		return NULL;
	}
	pack=(tPackHeader*)*gPacks[packNum];
	if(!pack) {
		fprintf(stderr, "[GetSortedPackEntry] ERROR: *gPacks[%d] is NULL!\n", packNum);
		return NULL;
	}
	startId=pack[1].id;
	idx=entryID-startId+1;
	if(idx < 1 || idx > pack->id) {
		fprintf(stderr, "[GetSortedPackEntry] ERROR: idx %d out of range [1..%d]!\n", idx, pack->id);
		return NULL;
	}
	offs=pack[idx].offs;
	{
		long handleSize = GetHandleSize(gPacks[packNum]);
		if(offs > (UInt32)handleSize) {
			fprintf(stderr, "[GetSortedPackEntry] ERROR: offs=%u > handleSize=%ld for pack=%d entry=%d idx=%d numEntries=%d\n",
				offs, handleSize, packNum, entryID, idx, pack->id);
			return NULL;
		}
		if(size) {
			if(idx==pack->id)
				*size=handleSize-offs;
			else {
				UInt32 nextOffs=pack[idx+1].offs;
				if(nextOffs > (UInt32)handleSize || nextOffs < offs) {
					fprintf(stderr, "[GetSortedPackEntry] ERROR: nextOffs=%u invalid (offs=%u handleSize=%ld) pack=%d entry=%d\n",
						nextOffs, offs, handleSize, packNum, entryID);
					*size=handleSize-offs;
				} else
					*size=nextOffs-offs;
			}
		}
	}
	return (Ptr)pack+offs;
}

int ComparePackHeaders(const void *a,const void *b)
{
	return ((const tPackHeader*)a)->id-((const tPackHeader*)b)->id;
}

Ptr GetUnsortedPackEntry(int packNum,int entryID,int *size)
{
	tPackHeader *pack=(tPackHeader*)*gPacks[packNum];
	tPackHeader key,*found;
	UInt32 offs;
	key.id=entryID;
	found=(tPackHeader*)bsearch(&key,pack+1,pack->id,sizeof(tPackHeader),ComparePackHeaders);
	if(found)
	{
		offs=found->offs;
		if(size)
			if(pack->id==found-pack)
				*size=GetHandleSize(gPacks[packNum])-offs;
			else
				*size=(found+1)->offs-offs;
		return (Ptr)pack+offs;
	}
	else return 0;
}

int NumPackEntries(int num)
{
	return gPacks[num]?(**(tPackHandle)gPacks[num]).id:0;
}

/* Iterate all entries in a pack, calling callback for each.
 * callback receives: entry data pointer, entry size, entry id, user data */
void ForEachPackEntry(int packNum, void (*callback)(Ptr data, int size, int id, void *ctx), void *ctx)
{
	tPackHeader *pack;
	int i, n;
	if(!gPacks[packNum]) return;
	pack = (tPackHeader*)*gPacks[packNum];
	n = pack->id;
	for(i = 1; i <= n; i++) {
		UInt32 offs = pack[i].offs;
		int entrySize;
		if(i == n)
			entrySize = GetHandleSize(gPacks[packNum]) - offs;
		else
			entrySize = pack[i+1].offs - offs;
		callback((Ptr)pack + offs, entrySize, pack[i].id, ctx);
	}
}

UInt32 BlockChecksum(UInt32 *data,UInt32 len)
{
	UInt32 check=0;
	while(len>=4)
	{
		check+=*data++;
		len-=4;
	}
	return check;
}
