#include <math.h>
#include <stdlib.h>
#include <time.h>

/* A C-program for TT800 : July 8th 1996 Version */
/* by M. Matsumoto, email: matumoto@math.keio.ac.jp */

#define N 25
#define M 7

unsigned long x[N];

double genrand()
{
    unsigned long y;
    static int k = 0;
    static unsigned long mag01[2]={
        0x0, 0x8ebfd028
    };
    if (k==N) {
      int kk;
      for (kk=0;kk<N-M;kk++) {
        x[kk] = x[kk+M] ^ (x[kk] >> 1) ^ mag01[x[kk] % 2];
      }
      for (; kk<N;kk++) {
        x[kk] = x[kk+(M-N)] ^ (x[kk] >> 1) ^ mag01[x[kk] % 2];
      }
      k=0;
    }
    y = x[k];
    y ^= (y << 7) & 0x2b5b2500;
    y ^= (y << 15) & 0xdb8b0000;
    y &= 0xffffffff;
    y ^= (y >> 16);
    k++;
    return( (double) y / (unsigned long) 0xffffffff);
}

void Randomize()
{
	int i;
	srand((unsigned int)time(NULL));
	for(i=0;i<N;i++)
		x[i]=(unsigned long)rand()|(unsigned long)rand()<<16;
}

float RanFl(float min,float max)
{
	return genrand()*(max-min)+min;
}

int RanInt(int min,int max)
{
	double ran;
	do ran=genrand(); while(ran==1);
	return floor(ran*(max-min)+min);
}

int RanProb(float prob)
{
	return genrand()<=prob;
}
