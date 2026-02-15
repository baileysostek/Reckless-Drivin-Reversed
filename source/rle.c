#include "mac_compat.h"
#include "endian_compat.h"
#include "screen.h"
#include "packs.h"
#include "error.h"
#include "preferences.h"

/* paramErr defined in mac_compat.h */

#define kEndShapeToken		0				// the end of shape maker
#define kLineStartToken		1				// the line start marker
#define kDrawPixelsToken	2				// the draw run marker
#define kSkipPixelsToken	3				// the skip pixels marker

/* Read RLE token: first byte is type, next 3 bytes are big-endian count */
static inline void ReadRLEToken(const UInt8 *p, UInt8 *tokenType, SInt32 *tokenData)
{
	*tokenType = p[0];
	*tokenData = ((SInt32)p[1] << 16) | ((SInt32)p[2] << 8) | (SInt32)p[3];
}

void DrawRLE8(int h,int v,int id)
{
	int rowBytes=gRowBytes;
	UInt8 *spritePos=GetSortedPackEntry(kPacksRLE,id,nil)+sizeof(Rect);
	UInt8 *lineStart=gBaseAddr+h+v*rowBytes;
	UInt8 *dst=lineStart;
	int stop=0;
	do
	{
		UInt8 tokenType; SInt32 tokenData;
		ReadRLEToken(spritePos, &tokenType, &tokenData);
		switch (tokenType)
		{
			case kDrawPixelsToken:
				{
					int i=0;
					UInt8 *src=spritePos+4;
					spritePos+=4+tokenData+(tokenData&3?(4-tokenData&3):0);
					while(tokenData-(int)sizeof(long)>=i)
					{
						*((long*)(dst+i))=*((long*)(src+i));
						i+=sizeof(long);
					}
					if(tokenData-(int)sizeof(short)>=i)
					{
						*((short*)(dst+i))=*((short*)(src+i));
						i+=sizeof(short);
					}
					if(tokenData-(int)sizeof(char)>=i)
						*((char*)(dst+i))=*((char*)(src+i));
					dst+=tokenData;
				}
				break;
			case kSkipPixelsToken:
				dst+=tokenData;
				spritePos+=4;
				break;
			case kLineStartToken:
				lineStart+=rowBytes;
				dst=lineStart;
				spritePos+=4;
				break;
			case kEndShapeToken:
				stop=true;
				break;
			default: 
				DoError(paramErr);
		}
	}
	while (!stop);
}

void DrawRLE16(int h,int v,int id)
{
	int rowBytes=gRowBytes;
	UInt8 *spritePos=GetSortedPackEntry(kPacksR16,id,nil)+sizeof(Rect);
	UInt8 *lineStart=gBaseAddr+h*2+v*rowBytes;
	UInt16 *dst=(UInt16*)lineStart;
	int stop=0;
	do
	{
		UInt8 tokenType; SInt32 tokenData;
		ReadRLEToken(spritePos, &tokenType, &tokenData);
		switch (tokenType)
		{
			case kDrawPixelsToken:
				{
					int i=0;
					UInt16 *src=(UInt16*)(spritePos+4);
					int tokenSize=tokenData*2;
					spritePos+=4+tokenSize+(tokenSize&3?(4-tokenSize&3):0);
					while(tokenData-2>=i)
					{
						*((long*)(dst+i))=*((long*)(src+i));
						i+=2;
					}
					if(tokenData-(int)sizeof(char)>=i)
						*((char*)(dst+i))=*((char*)(src+i));
					dst+=tokenData;
				}
				break;
			case kSkipPixelsToken:
				dst+=tokenData;
				spritePos+=4;
				break;
			case kLineStartToken:
				lineStart+=rowBytes;
				dst=lineStart;
				spritePos+=4;
				break;
			case kEndShapeToken:
				stop=true;
				break;
			default: 
				DoError(paramErr);
		}
	}
	while (!stop);
}

void DrawRLEYClip8(int h,int v,int id)
{
	int rowBytes=gRowBytes;
	UInt8 *spritePos=GetSortedPackEntry(kPacksRLE,id,nil)+sizeof(Rect);
	UInt8 *lineStart=gBaseAddr+h+v*rowBytes;
	UInt8 *dst=lineStart;
	int stop=0;
	do
	{
		UInt8 tokenType; SInt32 tokenData;
		ReadRLEToken(spritePos, &tokenType, &tokenData);
		switch (tokenType)
		{
			case kDrawPixelsToken:
				{
					int i=0;
					UInt8 *src=spritePos+4;
					spritePos+=4+tokenData+(tokenData&3?(4-tokenData&3):0);
					if(v>=0)
					{
						while(tokenData-(int)sizeof(long)>=i)
						{
							*((long*)(dst+i))=*((long*)(src+i));
							i+=sizeof(long);
						}
						if(tokenData-(int)sizeof(short)>=i)
						{
							*((short*)(dst+i))=*((short*)(src+i));
							i+=sizeof(short);
						}
						if(tokenData-(int)sizeof(char)>=i)
							*((char*)(dst+i))=*((char*)(src+i));
					}
					dst+=tokenData;
				}
				break;
			case kSkipPixelsToken:
				dst+=tokenData;
				spritePos+=4;
				break;
			case kLineStartToken:
				lineStart+=rowBytes;
				dst=lineStart;
				spritePos+=4;
				v++;
				if(v>=gYSize)return;
				break;
			case kEndShapeToken:
				stop=true;
				break;
			default: 
				DoError(paramErr);
		}
	}
	while (!stop);
}

void DrawRLEYClip16(int h,int v,int id)
{
	int rowBytes=gRowBytes;
	UInt8 *spritePos=GetSortedPackEntry(kPacksR16,id,nil)+sizeof(Rect);
	UInt8 *lineStart=gBaseAddr+h*2+v*rowBytes;
	UInt16 *dst=(UInt16*)lineStart;
	int stop=0;
	do
	{
		UInt8 tokenType; SInt32 tokenData;
		ReadRLEToken(spritePos, &tokenType, &tokenData);
		switch (tokenType)
		{
			case kDrawPixelsToken:
				{
					int i=0;
					UInt16 *src=(UInt16*)(spritePos+4);
					int tokenSize=tokenData*2;
					spritePos+=4+tokenSize+(tokenSize&3?(4-tokenSize&3):0);
					if(v>=0)
					{
						while(tokenData-2>=i)
						{
							*((long*)(dst+i))=*((long*)(src+i));
							i+=2;
						}
						if(tokenData-(int)sizeof(char)>=i)
							*((char*)(dst+i))=*((char*)(src+i));
					}
					dst+=tokenData;
				}
				break;
			case kSkipPixelsToken:
				dst+=tokenData;
				spritePos+=4;
				break;
			case kLineStartToken:
				lineStart+=rowBytes;
				dst=lineStart;
				spritePos+=4;
				v++;
				if(v>=gYSize)return;
				break;
			case kEndShapeToken:
				stop=true;
				break;
			default: 
				DoError(paramErr);
		}
	}
	while (!stop);
}

void DrawRLE(int h,int v,int id)
{
	if(gPrefs.hiColor)
		 DrawRLE16(h,v,id);
	else
		 DrawRLE8(h,v,id);
}

void DrawRLEYClip(int h,int v,int id)
{
	if(gPrefs.hiColor)
		 DrawRLEYClip16(h,v,id);
	else
		 DrawRLEYClip8(h,v,id);
}

