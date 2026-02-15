#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "../libs/lzrw/lzrw3a.h"
static uint16_t rd16(const uint8_t *p){return(uint16_t)((p[0]<<8)|p[1]);}
static uint32_t rd32(const uint8_t *p){return((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];}

/* Simple PackBits decode */
static int unpack(const uint8_t *src, int srcLen, uint8_t *dst, int dstLen) {
    const uint8_t *se=src+srcLen; uint8_t *de=dst+dstLen;
    while(src<se&&dst<de){
        int8_t n=(int8_t)*src++;
        if(n>=0){int c=n+1;if(src+c>se||dst+c>de)break;memcpy(dst,src,c);src+=c;dst+=c;}
        else if(n!=-128){int r=1-n;uint8_t v=*src++;int i;for(i=0;i<r&&dst<de;i++)*dst++=v;}
    }
    return(int)(dst-(de-dstLen));
}

int main(int argc, char **argv) {
    FILE *f; long fs; uint8_t *fd,*dd,*wm; uint32_t us; uint64_t dl; struct compress_identity id;
    const uint8_t *p, *end; int i;
    if(argc<2)return 1;
    f=fopen(argv[1],"rb");if(!f)return 1;
    fseek(f,0,SEEK_END);fs=ftell(f);fseek(f,0,SEEK_SET);
    fd=malloc(fs);fread(fd,1,fs,f);fclose(f);
    us=rd32(fd);dd=malloc(us+1024);
    id=lzrw_identity();wm=malloc(id.memory);dl=0;
    lzrw3a_compress(COMPRESS_ACTION_DECOMPRESS,wm,fd+4,(uint32_t)(fs-4),dd,&dl);
    free(wm);free(fd);

    /* Find PackBitsRect opcode */
    p=dd; end=dd+dl;
    /* Skip PICT header + known opcodes to PackBitsRect */
    p+=2+8; /* size + picFrame */
    while(p<end-1){
        uint16_t op=rd16(p);p+=2;
        if(op==0x0098)break;
        if(op==0x0000)continue;
        if(op==0x0011){p+=2;continue;}
        if(op==0x0C00){p+=24;continue;}
        if(op==0x0001){uint16_t rs=rd16(p);p+=2;if(rs>2)p+=rs-2;continue;}
        if(op==0x00A1){p+=2;uint16_t len=rd16(p);p+=2;p+=len;continue;}
        if(op==0x001E)continue;
    }

    /* Parse PixMap */
    uint16_t rb=rd16(p);p+=2;
    int rbRaw=rb&0x3FFF;
    p+=8; /* bounds */
    p+=2+2+4+4+4+2+2+2+2+4+4+4; /* PixMap fields */
    /* Color table */
    p+=4; /* ctSeed */
    p+=2; /* ctFlags */
    uint16_t ctSize=rd16(p);p+=2;
    printf("ctSize=%d, skipping %d entries...\n",ctSize,ctSize+1);
    p+=(ctSize+1)*8;
    /* srcRect, dstRect, mode */
    p+=8+8+2;

    printf("Scanline data starts at offset %ld (remaining=%ld)\n",(long)(p-dd),(long)(end-p));

    /* Decode all 480 scanlines */
    uint8_t *scanline=malloc(rbRaw+256);
    int nonBlackLines=0;
    for(i=0;i<480;i++){
        if(p+2>end){printf("Out of data at line %d\n",i);break;}
        int bc;
        if(rbRaw>250){bc=rd16(p);p+=2;}else{bc=*p++;}
        if(bc<=0||p+bc>end){printf("Bad byteCount=%d at line %d (remaining=%ld)\n",bc,i,(long)(end-p));break;}

        memset(scanline,0,rbRaw);
        unpack(p,bc,scanline,rbRaw);
        p+=bc;

        /* Check if this line has non-0xFF pixels */
        int hasContent=0;
        int j;
        for(j=0;j<640;j++){if(scanline[j]!=0xFF){hasContent=1;break;}}
        if(hasContent){
            if(nonBlackLines<20)
                printf("Line %d: byteCount=%d, first non-0xFF at pixel %d (val=%d)\n",i,bc,j,scanline[j]);
            nonBlackLines++;
        }
    }
    printf("Total non-black lines: %d out of %d decoded\n",nonBlackLines,i);
    printf("Final stream pos: offset %ld (remaining=%ld)\n",(long)(p-dd),(long)(end-p));

    free(scanline);free(dd);
    return 0;
}
