#ifndef __MAC_COMPAT_H__
#define __MAC_COMPAT_H__

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Mac integer types */
typedef uint8_t  UInt8;
typedef uint16_t UInt16;
typedef uint32_t UInt32;
typedef uint64_t UInt64;
typedef int8_t   SInt8;
typedef int16_t  SInt16;
typedef int32_t  SInt32;
typedef int64_t  SInt64;

/* Mac pointer types */
typedef char* Ptr;
typedef char** Handle;

/* Mac boolean */
typedef unsigned char Boolean;


/* Mac graphics types */
typedef struct { short top, left, bottom, right; } Rect;
typedef struct { short v, h; } Point;
typedef struct { unsigned short red, green, blue; } RGBColor;

/* Mac error type */
typedef SInt16 OSErr;
typedef UInt32 OSType;

/* GWorld stub - used in interface.c and screenfx.c */
typedef void* GWorldPtr;
typedef void* WindowPtr;

/* Mac version type used in initexit.c */
typedef struct {
    UInt8 majorRev;
    UInt8 minorAndBugRev;
    UInt8 stage;
    UInt8 nonRelRev;
} NumVersion;

/* Mac constants */
#ifndef nil
#define nil NULL
#endif

#ifndef true
#define true 1
#endif

#ifndef false
#define false 0
#endif

#ifndef PI
#define PI 3.1415
#endif

/* Mac error codes */
#define noErr 0
#define fnfErr (-43)
#define memFullErr (-108)
#define paramErr (-50)

/* Mac type aliases */
typedef unsigned long KeyMap[4];
#define GetKeys(km) memset(km, 0, sizeof(KeyMap))
#define Button() 0
typedef void* GDHandle;
#define srcCopy 0
typedef void* Pattern;
typedef void* PicHandle;
typedef void* BitMap;
#define GetQDGlobalsBlack(p)
#define GetGWorld(gw, gd)
#define SetGWorld(gw, gd)
#define NewGWorld(gw, depth, rect, clut, gd, flags) 0
#define DisposeGWorld(gw)
/* FillRect conflicts with Windows API */
#ifndef _WIN32
#define FillRect(r, p)
#endif
#define DrawPicture(pic, rect)
#define CopyBits(src, dst, srcR, dstR, mode, rgn)
#define GetPortBitMapForCopyBits(gw) NULL

/* Handle memory management */
Handle NewHandle(long size);
void DisposeHandle(Handle h);
long GetHandleSize(Handle h);
void SetHandleSize(Handle h, long newSize);
void HLock(Handle h);
void HLockHi(Handle h);
void HUnlock(Handle h);
OSErr HandToHand(Handle *h);
OSErr PtrToHand(const void *srcPtr, Handle *dstHndl, long size);

/* Pointer memory management */
Ptr NewPtr(long size);
Ptr NewPtrClear(long size);
void DisposePtr(Ptr p);

/* Memory utilities */
void BlockMoveData(const void *srcPtr, void *destPtr, long byteCount);
OSErr MemError(void);

/* String utilities */
void NumToString(long theNum, char theString[256]);
void StringToNum(const char theString[256], long *theNum);

/* Rect utility - renamed to avoid conflict with Windows API SetRect */
void MacSetRect(Rect *r, short left, short top, short right, short bottom);
#ifndef _WIN32
#define SetRect MacSetRect
#endif

/* Stubs for Mac APIs that are no-ops or simple replacements */
#define InitCursor()
/* ShowCursor conflicts with Windows API - always use MacShowCursor */
#define MacShowCursor() SDL_ShowCursor(1)
#define MacHideCursor() SDL_ShowCursor(0)
#ifndef _WIN32
#define ShowCursor() MacShowCursor()
#endif
#define ExitToShell() exit(0)
/* TickCount provided by platform_input.c via GetMSTime; stub for compatibility */
unsigned long TickCountCompat(void);
#define TickCount() TickCountCompat()
#define GetResource(type, id) NULL
#define ReleaseResource(h)
#define Get1Resource(type, id) NULL
#define CurResFile() 0
#define UpperString(s, diac)

/* Component / Sound Manager stubs */
typedef void* Component;
typedef void* ComponentDescription;
typedef void* SndChannelPtr;
typedef void* UniversalProcPtr;

#define kUnresolvedCFragSymbolAddress ((void*)-1)

/* Alert stubs */
typedef struct {
    int a,b;
    void* c;
    const char* d;
    void* e,*f;
    int g,h,i;
} AlertStdAlertParamRec;

#define kAlertStdAlertOKButton 1
#define kAlertStopAlert 0
#define kAlertNoteAlert 1
#define kWindowDefaultPosition 0
#define StandardAlert(a,b,c,d,e) (0)
#define DebugStr(s)
#define RegisterAppearanceClient() (0)

/* Mac event stubs */
#define kHighLevelEvent 0
typedef struct { int what; unsigned long message; unsigned long when; Point where; unsigned short modifiers; } EventRecord;
#define everyEvent 0xFFFF
#define keyCodeMask 0xFF00
#define keyDown 3
#define mouseDown 1

/* Gestalt stub */
#define gestaltSystemVersion 0
#define Gestalt(sel, resp) (*(resp) = 0x1050, 0)

/* Dialog stubs */
typedef void* DialogPtr;
#define GetNewDialog(id, storage, behind) NULL
#define DisposeDialog(d)
#define ModalDialog(filter, hit)
#define SetDialogDefaultItem(d, item) 0
#define SetDialogCancelItem(d, item) 0
#define GetDialogItem(d, item, type, h, box)
#define SetDialogItemText(h, str)
#define GetDialogItemText(h, str)
#define HiliteControl(h, val)
#define IsDialogEvent(e) 0
#define DialogSelect(e, d, hit) 0
#define StopAlert(id, filter) 0

/* Window stubs */
/* FindWindow conflicts with Windows API */
#ifndef _WIN32
#define FindWindow(pt, win) 0
#endif
#define DragWindow(w, pt, rect)
#define GetRegionBounds(rgn, rect)
#define GetGrayRgn() NULL
#define inDrag 4

/* WaitNextEvent stub */
#define WaitNextEvent(mask, event, sleep, rgn) 0

/* TEFromScrap stub */
#define TEFromScrap()

/* File Manager stubs */
typedef struct { short vRefNum; long parID; char name[64]; } FSSpec;
#define FSMakeFSSpec(v,d,name,spec) 0
#define FSpOpenDF(spec,perm,ref) (-1)
#define FSpDelete(spec) 0
#define FindFolder(v,type,create,foundV,foundD) (-1)
#define FSClose(ref)
#define FSRead(ref,count,buf) (-1)
#define FSWrite(ref,count,buf) (-1)
#define FSpCreate(spec,creator,type,script) 0
#define SetFPos(ref,mode,pos) 0
#define fsFromStart 1
#define fsWrPerm 2
#define fsRdPerm 1
#define kOnSystemDisk (-32768)
#define kPreferencesFolderType 'pref'
#define smSystemScript 0

/* Internet Config stubs */
#define ICStart(inst, creator) (-1)
#define ICLaunchURL(inst, hint, url, len, start, end) (-1)
#define ICStop(inst) 0
typedef void* ICInstance;

#endif /* __MAC_COMPAT_H__ */
