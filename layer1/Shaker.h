/* 
A* -------------------------------------------------------------------
B* This file contains source code for the PyMOL computer program
C* copyright 1998-2000 by Warren Lyford Delano of DeLano Scientific. 
D* -------------------------------------------------------------------
E* It is unlawful to modify or remove this copyright notice.
F* -------------------------------------------------------------------
G* Please see the accompanying LICENSE file for further information. 
H* -------------------------------------------------------------------
I* Additional authors of this source file include:
-* 
-* 
-*
Z* -------------------------------------------------------------------
*/
#ifndef _H_Shaker
#define _H_Shaker

#define cShakerDistBond 0
#define cShakerDistAngle 1
#define cShakerDistLimit 2

typedef struct {
  int at0,at1;
  int type;
  float targ,targ2;
} ShakerDistCon;

#define cShakerTorsAlkane 1
#define cShakerTors

typedef struct {
  int at0,at1,at2,at3;
  int type;
} ShakerTorsCon;

#define cShakerTorsSP3SP3      1
#define cShakerTorsDisulfide   2
 
typedef struct {
  int at0,at1,at2,at3;
  float targ;
} ShakerPyraCon;

typedef struct {
  int at0,at1,at2,at3;
} ShakerPlanCon;

typedef struct {
  int at0,at1,at2;
} ShakerLineCon;

typedef struct {
  ShakerDistCon *DistCon;
  int NDistCon;
  ShakerPyraCon *PyraCon;
  int NPyraCon;
  ShakerPlanCon *PlanCon;
  int NPlanCon;
  ShakerLineCon *LineCon;
  int NLineCon;
  ShakerTorsCon *TorsCon;
  int NTorsCon;
} CShaker;

CShaker *ShakerNew(void);
void ShakerReset(CShaker *I);
void ShakerAddDistCon(CShaker *I,int atom0,int atom1,float dist,int type);
void ShakerAddTorsCon(CShaker *I,int atom0,int atom1,int atom2,int atom3,int type);
void ShakerAddPyraCon(CShaker *I,int atom0,int atom1,int atom2,int atom3,float target);
void ShakerAddPlanCon(CShaker *I,int atom0,int atom1,int atom2,int atom3);

void ShakerAddLineCon(CShaker *I,int atom0,int atom1,int atom2);

float ShakerGetPyra(float *v0,float *v1,float *v2,float *v3);

/* the following fn's have been inlined in Sculpt.c  
float ShakerDoDist(float target,float *v0,float *v1,float *d0to1,float *d1to0,float wt);

float ShakerDoTors(int type, float *v0,float *v1,float *v2,float *v3,
                   float *p0,float *p1,float *p2,float *p3,float wt);

*/

float ShakerDoPyra(float target,float *v0,float *v1,float *v2,float *v3,
                   float *p0,float *p1,float *p2,float *p3,float wt);

float ShakerDoDistLimit(float target,float *v0,float *v1,float *d0to1,float *d1to0,float wt);

float ShakerDoLine(float *v0,float *v1,float *v2,
                   float *p0,float *p1,float *p2,float wt);

float ShakerDoPlan(float *v0,float *v1,float *v2,float *v3,
                   float *p0,float *p1,float *p2,float *p3,float wt);

                   
void ShakerFree(CShaker *I);

#endif


