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

#include"os_predef.h"
#include"os_std.h"
#include"os_gl.h"

#include"Base.h"
#include"Debug.h"
#include"Parse.h"
#include"OOMac.h"
#include"Vector.h"
#include"MemoryDebug.h"
#include"Err.h"
#include"Map.h"
#include"Selector.h"
#include"ObjectMolecule.h"
#include"Ortho.h"
#include"Util.h"
#include"Vector.h"
#include"Selector.h"
#include"Matrix.h"
#include"Scene.h"
#include"P.h"
#include"PConv.h"
#include"Executive.h"
#include"Setting.h"
#include"Sphere.h"
#include"main.h"
#include"CGO.h"
#include"Raw.h"
#include"Editor.h"
#include"Selector.h"
#include"Sculpt.h"

#define cMaxNegResi 100

#define wcopy ParseWordCopy
#define nextline ParseNextLine
#define ncopy ParseNCopy
#define nskip ParseNSkip

#define cResvMask 0x7FFF

void ObjectMoleculeRender(ObjectMolecule *I,int frame,CRay *ray,Pickable **pick,int pass);
void ObjectMoleculeCylinders(ObjectMolecule *I);
CoordSet *ObjectMoleculeMMDStr2CoordSet(char *buffer,AtomInfoType **atInfoPtr);

CoordSet *ObjectMoleculePDBStr2CoordSet(char *buffer,AtomInfoType **atInfoPtr,
                                        char **restart,char *segi_override);

CoordSet *ObjectMoleculePMO2CoordSet(CRaw *pmo,AtomInfoType **atInfoPtr,int *restart);

void ObjectMoleculeAppendAtoms(ObjectMolecule *I,AtomInfoType *atInfo,CoordSet *cset);
CoordSet *ObjectMoleculeMOLStr2CoordSet(char *buffer,AtomInfoType **atInfoPtr);

void ObjectMoleculeFree(ObjectMolecule *I);
void ObjectMoleculeUpdate(ObjectMolecule *I);
int ObjectMoleculeGetNFrames(ObjectMolecule *I);

void ObjectMoleculeDescribeElement(ObjectMolecule *I,int index,char *buffer);

void ObjectMoleculeSeleOp(ObjectMolecule *I,int sele,ObjectMoleculeOpRec *op);

int ObjectMoleculeConnect(ObjectMolecule *I,BondType **bond,AtomInfoType *ai,CoordSet *cs,int searchFlag);
void ObjectMoleculeTransformTTTf(ObjectMolecule *I,float *ttt,int state);
static int BondInOrder(BondType *a,int b1,int b2);
static int BondCompare(BondType *a,BondType *b);

CoordSet *ObjectMoleculeChemPyModel2CoordSet(PyObject *model,AtomInfoType **atInfoPtr);

int ObjectMoleculeGetAtomGeometry(ObjectMolecule *I,int state,int at);
void ObjectMoleculeBracketResidue(ObjectMolecule *I,AtomInfoType *ai,int *st,int *nd);

int ObjectMoleculeFindOpenValenceVector(ObjectMolecule *I,int state,int index,float *v);
void ObjectMoleculeAddSeleHydrogens(ObjectMolecule *I,int sele);


CoordSet *ObjectMoleculeXYZStr2CoordSet(char *buffer,AtomInfoType **atInfoPtr);
CSetting **ObjectMoleculeGetSettingHandle(ObjectMolecule *I,int state);
void ObjectMoleculeInferAmineGeomFromBonds(ObjectMolecule *I,int state);
CoordSet *ObjectMoleculeTOPStr2CoordSet(char *buffer,
                                        AtomInfoType **atInfoPtr);

ObjectMolecule *ObjectMoleculeReadTOPStr(ObjectMolecule *I,char *TOPStr,int frame,int discrete);

#define MAX_BOND_DIST 50

#if 0
static void dump_jxn(char *lab,char *q)
{
  char cc[MAXLINELEN];  
  printf("\n%s %p\n",lab,q);
  q=q-150;
  q=nextline(q);
  ncopy(cc,q,100);
  printf("0[%s]\n",cc);
  q=nextline(q);
  ncopy(cc,q,100);
  printf("1[%s]\n",cc);
  q=nextline(q);
  ncopy(cc,q,100);
  printf("2[%s]\n",cc);
  q=nextline(q);
  ncopy(cc,q,100);
  printf("3[%s]\n",cc);

}
#endif

static char *skip_fortran(int num,int per_line,char *p)
{
  int a,b;
  b=0;
  for(a=0;a<num;a++) {
    if((++b)==per_line) {
      b=0;
      p=nextline(p);
    }
  }  
  if(b) p=nextline(p);
  return(p);
}
/*========================================================================*/

ObjectMolecule *ObjectMoleculeLoadTRJFile(ObjectMolecule *I,char *fname,int frame,
                                          int interval,int average,int start,
                                          int stop,int max,char *sele,int image)
{
  int ok=true;
  FILE *f;
  char *buffer,*p,*p_save;
  char cc[MAXLINELEN];  
  int n_read;
  int to_go;
  int skip_first_line = true;
  int periodic=false;
  float f0,f1,f2,f3,*fp;
  float box[3],pre[3],post[3];
  float r_cent[3],r_trans[3];
  int r_act,r_val,r_cnt;
  float *r_fp_start=NULL,*r_fp_stop=NULL;
  int a,b,c,i;
  int *to,at_i;
  int zoom_flag=false;
  int cnt=0;
  int n_avg=0;
  int icnt;
  int ncnt=0;
  int sele0 = SelectorIndexByName(sele);
  int *xref = NULL;
  CoordSet *cs = NULL;
  if(interval<1)
    interval=1;

  icnt=interval;
  #define BUFSIZE 4194304
  #define GETTING_LOW 10000

  f=fopen(fname,"rb");
  if(!f)
	 ok=ErrMessage("ObjectMoleculeLoadTOPFile","Unable to open file!");
  else
	 {
      if(!I->CSTmpl) {
        PRINTFB(FB_Errors,FB_ObjectMolecule)
          " ObjMolLoadTRJFile: Missing topology"
          ENDFB;
        return(I);
      }
      cs=CoordSetCopy(I->CSTmpl);

      if(sele0>=0) { /* build array of cross-references */
        xref = Alloc(int,I->NAtom);
        c=0;
        for(a=0;a<I->NAtom;a++) {
          if(SelectorIsMember(I->AtomInfo[a].selEntry,sele0)) {
            xref[a]=c++;
          } else {
            xref[a]=-1;
          }
        }

        for(a=0;a<I->NAtom;a++) { /* now terminate the excluded references */
          if(xref[a]<0) {
            cs->AtmToIdx[a]=-1;
          } 
        }

        to=cs->IdxToAtm;
        c=0;
        for(a=0;a<cs->NIndex;a++) { /* now fix IdxToAtm, AtmToIdx,
                                       and remap xref to coordinate space */         
          at_i = cs->IdxToAtm[a];
          if(cs->AtmToIdx[at_i]>=0) {
            *(to++)=at_i;
            cs->AtmToIdx[at_i]=c;
            xref[a]=c;
            c++;
          } else {
            xref[a]=-1;
          }
        }

        cs->NIndex=c;
        cs->IdxToAtm = Realloc(cs->IdxToAtm,int,cs->NIndex+1);
        VLASize(cs->Coord,float,cs->NIndex*3);
      }
      PRINTFB(FB_ObjectMolecule,FB_Blather) 
        " ObjMolLoadTRJFile: Loading from '%s'.\n",fname
        ENDFB;
      buffer = (char*)mmalloc(BUFSIZE+1); /* 1 MB read buffer */
      p = buffer;
      buffer[0]=0;
      n_read = 0;
      to_go=0;
      a = 0;
      b = 0;
      c = 0;
      f1=0.0;
      f2=0.0;
      while(1)
        {
          to_go = n_read-(p-buffer);
          if(to_go<GETTING_LOW) 
            if(!feof(f)) {
              if(to_go) 
                memcpy(buffer,p,to_go);
              n_read = fread(buffer+to_go,1,BUFSIZE-to_go,f);              
              n_read = to_go + n_read;
              buffer[n_read]=0;
              p = buffer;
              if(skip_first_line) {
                p=nextline(p);
                skip_first_line=false;
              }
              to_go = n_read-(p-buffer);
            }
          if(!to_go) break;
          p=ncopy(cc,p,8);
          if((++b)==10) {
            b=0;
            p=nextline(p);
          }
          f0 = f1;
          f1 = f2;
          if(sscanf(cc,"%f",&f2)==1) {
            if((++c)==3) {
              c=0;
              if((cnt+1)>=start) {
                if(icnt<=1) {
                  if(xref) { 
                    if(xref[a]>=0)
                      fp=cs->Coord+3*xref[a];
                    else 
                      fp=NULL;
                  } else {
                    fp=cs->Coord+3*a;
                  }
                  if(fp) {
                    if(n_avg) {
                      *(fp++)+=f0;
                      *(fp++)+=f1;
                      *(fp++)+=f2;
                    } else {
                      *(fp++)=f0;
                      *(fp++)=f1;
                      *(fp++)=f2;
                    }
                  }
                }
              }
              if((++a)==I->NAtom) {
                
                cnt++;
                a=0;
                if(b) p=nextline(p);
                b=0;
                
                c=0;
                periodic=true;
                p_save=p;
                p = ncopy(cc,p,8);
                if(sscanf(cc,"%f",&box[0])!=1) 
                  periodic=false;
                p = ncopy(cc,p,8);
                if(sscanf(cc,"%f",&box[1])!=1)
                  periodic=false;
                p = ncopy(cc,p,8);
                if(sscanf(cc,"%f",&box[2])!=1)
                  periodic=false;
                p = ncopy(cc,p,8);
                if(sscanf(cc,"%f",&f3)==1) { /* not a periodic box record */
                  periodic=false;
                  p=p_save;
                } else if(periodic) {
                  if(!cs->PeriodicBox)
                    cs->PeriodicBox=CrystalNew();
                  cs->PeriodicBox->Dim[0] = box[0];
                  cs->PeriodicBox->Dim[1] = box[1];
                  cs->PeriodicBox->Dim[2] = box[2];
                  pre[0]=box[0]*1000.0;
                  pre[1]=box[1]*1000.0;
                  pre[2]=box[2]*1000.0;
                  if(cs->PeriodicBoxType==cCSet_Octahedral) {
                    pre[0]+=box[0]*0.5;
                    pre[1]+=box[1]*0.5;
                    pre[2]+=box[2]*0.5;
                    post[0]=-box[0]*0.5;
                    post[1]=-box[1]*0.5;
                    post[2]=-box[2]*0.5;
                  } else {
                    post[0]=0.0;
                    post[1]=0.0;
                    post[2]=0.0;
                  }
                  p=nextline(p);
                  b=0;
                }
                
                if((stop>0)&&(cnt>=stop))
                  break;
                if(cnt>=start) {
                  icnt--;                      
                  if(icnt>0) {
                    PRINTFB(FB_Details,FB_ObjectMolecule)
                      " ObjectMolecule: skipping set %d...\n",cnt
                      ENDFB;
                  } else {
                    icnt=interval;
                    n_avg++;
                  }
                  
                  if(icnt==interval) {
                    if(n_avg<average) {
                      PRINTFB(FB_Details,FB_ObjectMolecule)
                        " ObjectMolecule: averaging set %d...\n",cnt
                        ENDFB;
                    } else {
                      
                      /* compute average */
                      if(n_avg>1) {
                        fp=cs->Coord;
                        for(i=0;i<cs->NIndex;i++) {
                          *(fp++)/=n_avg;
                          *(fp++)/=n_avg;
                          *(fp++)/=n_avg;
                        }
                      }
                      if(periodic&&image) { /* Perform residue-based period image transformation */
                        i = 0;
                        r_cnt = 0;
                        r_act = 0; /* 0 unspec, 1=load, 2=image, 3=leave*/
                        r_val = -1;
                        while(r_act!=3) {
                          if(i>=cs->NIndex) {
                            if(r_cnt)
                              r_act = 2; 
                            else
                              r_act = 3;
                          }
                          if(r_act==0) {
                            /* start new residue */
                            r_cnt = 0;
                            r_act = 1; /* now load */
                          }
                          if(r_act==1) {
                            if(i<cs->NIndex) {
                              
                              /* is there a coordinate for atom? */
                              if(xref) { 
                                if(xref[i]>=0)
                                  fp=cs->Coord+3*xref[i];
                                else 
                                  fp=NULL;
                              } else {
                                fp=cs->Coord+3*i;
                              }
                              if(fp) { /* yes there is... */
                                if(r_cnt) {
                                  if(r_val!=I->AtomInfo[cs->IdxToAtm[i]].resv) {
                                    r_act=2; /* end of residue-> time to image */
                                  } else {
                                    r_cnt++;
                                    r_cent[0]+=*(fp++);
                                    r_cent[1]+=*(fp++);
                                    r_cent[2]+=*(fp++);
                                    r_fp_stop = fp; /* stop here */
                                    i++;
                                  }
                                } else {
                                  r_val = I->AtomInfo[cs->IdxToAtm[i]].resv;
                                  r_cnt++;
                                  r_fp_start = fp; /* start here */
                                  r_cent[0]=*(fp++);
                                  r_cent[1]=*(fp++);
                                  r_cent[2]=*(fp++);
                                  r_fp_stop = fp; /* stop here */
                                  i++;
                                }
                              } else {
                                i++;
                              }
                            } else {
                              r_act=2; /* image */
                            }
                          }

                          if(r_act==2) { /* time to image */
                            if(r_cnt) {
                              r_cent[0]/=r_cnt;
                              r_cent[1]/=r_cnt;
                              r_cent[2]/=r_cnt;
                              r_trans[0]=fmod(pre[0]+r_cent[0],box[0])+post[0];
                              r_trans[1]=fmod(pre[1]+r_cent[1],box[1])+post[1];
                              r_trans[2]=fmod(pre[2]+r_cent[2],box[2])+post[2];
                              r_trans[0]-=r_cent[0];
                              r_trans[1]-=r_cent[1];
                              r_trans[2]-=r_cent[2];
                              fp=r_fp_start;
                              while(fp<r_fp_stop) {
                                *(fp++)+=r_trans[0];
                                *(fp++)+=r_trans[1];
                                *(fp++)+=r_trans[2];
                              }
                            }
                            r_act=0; /* reset */ 
                            r_cnt=0;
                          }
                        }
                      }

                      /* add new coord set */
                      if(cs->fInvalidateRep)
                        cs->fInvalidateRep(cs,cRepAll,cRepInvRep);
                      if(frame<0) frame=I->NCSet;
                      if(!I->NCSet) {
                        zoom_flag=true;
                      }
                      
                      VLACheck(I->CSet,CoordSet*,frame);
                      if(I->NCSet<=frame) I->NCSet=frame+1;
                      if(I->CSet[frame]) I->CSet[frame]->fFree(I->CSet[frame]);
                      I->CSet[frame] = cs;
                      ncnt++;
                      
                      if(average<2) {
                        PRINTFB(FB_Details,FB_ObjectMolecule)
                          " ObjectMolecule: read set %d into state %d...\n",cnt,frame+1
                          ENDFB;
                      } else {
                        PRINTFB(FB_Details,FB_ObjectMolecule)
                          " ObjectMolecule: averaging set %d...\n",cnt
                          ENDFB;
                        PRINTFB(FB_Details,FB_ObjectMolecule)
                          " ObjectMolecule: average loaded into state %d...\n",frame+1
                          ENDFB;
                      }
                      frame++;
                      cs = CoordSetCopy(cs);
                      n_avg=0;
                      if((stop>0)&&(cnt>=stop))
                        break;
                      if((max>0)&&(ncnt>=max))
                        break;
                    }
                  }
                } else {
                  PRINTFB(FB_Details,FB_ObjectMolecule)
                    " ObjectMolecule: skipping set %d...\n",cnt
                    ENDFB;
                }
              }
            }
          } else {
            PRINTFB(FB_Errors,FB_ObjectMolecule)
              " ObjMolLoadTRJFile: atom/coordinate mismatch.\n"
              ENDFB;
            break;
          }
        }
      FreeP(xref);
		mfree(buffer);
	 }
  if(cs)
    cs->fFree(cs);
  SceneChanged();
  SceneCountFrames();
  if(zoom_flag) 
    if(SettingGet(cSetting_auto_zoom)) {
      ExecutiveWindowZoom(I->Obj.Name,0.0,-1); /* auto zoom (all states) */
    }
  
  return(I);
}
ObjectMolecule *ObjectMoleculeLoadRSTFile(ObjectMolecule *I,char *fname,int frame)

{
  int ok=true;
  FILE *f;
  char *buffer,*p;
  char cc[MAXLINELEN];  
  float f0,f1,f2,*fp;
  int a,b,c;
  int zoom_flag=false;
  CoordSet *cs = NULL;
  int size;

  #define BUFSIZE 4194304
  #define GETTING_LOW 10000

  f=fopen(fname,"rb");
  if(!f)
	 ok=ErrMessage("ObjectMoleculeLoadRSTFile","Unable to open file!");
  else
	 {
      if(!I->CSTmpl) {
        PRINTFB(FB_Errors,FB_ObjectMolecule)
          " ObjMolLoadTRJFile: Missing topology"
          ENDFB;
        return(I);
      }
      cs=CoordSetCopy(I->CSTmpl);
      PRINTFB(FB_ObjectMolecule,FB_Blather) 
        " ObjMolLoadTRJFile: Loading from '%s'.\n",fname
        ENDFB;


		fseek(f,0,SEEK_END);
      size=ftell(f);
		fseek(f,0,SEEK_SET);

		buffer=(char*)mmalloc(size+255);
		ErrChkPtr(buffer);
		p=buffer;
		fseek(f,0,SEEK_SET);
		fread(p,size,1,f);
		p[size]=0;
		fclose(f);

      p=nextline(p);
      p=nextline(p);

      a = 0;
      b = 0;
      c = 0;
      f1=0.0;
      f2=0.0;
      while(*p)
        {
          p=ncopy(cc,p,12);
          if((++b)==6) {
            b=0;
            p=nextline(p);
          }
          f0 = f1;
          f1 = f2;
          if(sscanf(cc,"%f",&f2)==1) {
            if((++c)==3) {
              c=0;
              fp=cs->Coord+3*a;
              *(fp++)=f0;
              *(fp++)=f1;
              *(fp++)=f2;
              
              if((++a)==I->NAtom) {
                a=0;
                if(b) p=nextline(p);
                b=0;
                /* add new coord set */
                if(cs->fInvalidateRep)
                  cs->fInvalidateRep(cs,cRepAll,cRepInvRep);
                if(frame<0) frame=I->NCSet;
                if(!I->NCSet) {
                  zoom_flag=true;
                }
                
                VLACheck(I->CSet,CoordSet*,frame);
                if(I->NCSet<=frame) I->NCSet=frame+1;
                if(I->CSet[frame]) I->CSet[frame]->fFree(I->CSet[frame]);
                I->CSet[frame] = cs;
                
                PRINTFB(FB_Details,FB_ObjectMolecule)
                  " ObjectMolecule: read coordinates into state %d...\n",frame+1
                  ENDFB;
               
                cs = CoordSetCopy(cs);
                break;
              }
            }
          } else {
            PRINTFB(FB_Errors,FB_ObjectMolecule)
              " ObjMolLoadTRJFile: atom/coordinate mismatch.\n"
              ENDFB;
            break;
          }
        }
		mfree(buffer);
	 }
  if(cs)
    cs->fFree(cs);
  
  SceneChanged();
  SceneCountFrames();
  if(zoom_flag) 
    if(SettingGet(cSetting_auto_zoom)) {
      ExecutiveWindowZoom(I->Obj.Name,0.0,-1); /* auto zoom (all states) */
    }
  
  return(I);
}
static char *findflag(char *p,char *flag,char *format)
{

  char cc[MAXLINELEN];
  char pat[MAXLINELEN] = "%FLAG ";
  int l;

  PRINTFD(FB_ObjectMolecule)
    " findflag: flag %s format %s\n",flag,format
    ENDFD;

  strcat(pat,flag);
  l=strlen(pat);
  while(*p) {
    p=ncopy(cc,p,l);
    if(WordMatch(cc,pat,true)<0) {
      p=nextline(p);
      break;
    }
    p=nextline(p);
    if(!*p) {
      PRINTFB(FB_ObjectMolecule,FB_Errors)
        " ObjectMolecule-Error: Unrecognized file format (can't find '%s').\n",pat
        ENDFB;
    }
  }

  strcpy(pat,"%FORMAT(");
  strcat(pat,format);
  strcat(pat,")");
  l=strlen(pat);
  while(*p) {
    p=ncopy(cc,p,l);
    if(WordMatch(cc,pat,true)<0) {
      p=nextline(p);
      break; 
    }
    p=nextline(p);
    if(!*p) {
      PRINTFB(FB_ObjectMolecule,FB_Errors)
        " ObjectMolecule-Error: Unrecognized file format (can't find '%s').\n",pat
        ENDFB;
    }
      
  }
  return(p);
}

#define nextline_top nextline

/*========================================================================*/
CoordSet *ObjectMoleculeTOPStr2CoordSet(char *buffer,
                                        AtomInfoType **atInfoPtr)
{
  char *p;
  int nAtom;
  int a,b,c,bi,last_i,at_i,aa,rc;
  float *coord = NULL;
  float *f;
  CoordSet *cset = NULL;
  AtomInfoType *atInfo = NULL,*ai;
  BondType *bond=NULL,*bd;
  int nBond=0;
  int auto_show_lines = SettingGet(cSetting_auto_show_lines);
  int auto_show_nonbonded = SettingGet(cSetting_auto_show_nonbonded);
  int amber7 = false;

  WordType title;
  ResName *resn;

  char cc[MAXLINELEN];
  int ok=true;
  int i0,i1,i2;

  /* trajectory parameters */

  int NTYPES,NBONH,MBONA,NTHETH,MTHETA;
  int NPHIH,MPHIA,NHPARM,NPARM,NNB,NRES;
  int NBONA,NTHETA,NPHIA,NUMBND,NUMANG,NPTRA;
  int NATYP,NPHB,IFPERT,NBPER,NGPER,NDPER;
  int MBPER,MGPER,MDPER,IFBOX,NMXRS,IFCAP;
  int NEXTRA,IPOL=0;
  int wid,col;

  
  AtomInfoPrimeColors();
  cset = CoordSetNew();  

  p=buffer;
  nAtom=0;
  if(atInfoPtr)
	 atInfo = *atInfoPtr;
  if(!atInfo)
    ErrFatal("TOPStr2CoordSet","need atom information record!");
  /* failsafe for old version..*/

  ncopy(cc,p,8);
  if(strcmp(cc,"%VERSION")==0) {
    amber7=true;
    PRINTFB(FB_ObjectMolecule,FB_Details)
      " ObjectMolecule: Attempting to read Amber7 topology file.\n"
      ENDFB;
  } else {
    PRINTFB(FB_ObjectMolecule,FB_Details)
      " ObjectMolecule: Assuming this is an Amber6 topology file.\n"
      ENDFB;
  }
  
  /* read title */
  if(amber7) {
    p = findflag(p,"TITLE","20a4");
  }

  p=ncopy(cc,p,20);
  title[0]=0;
  sscanf(cc,"%s",title);
  p=nextline_top(p);

  if(amber7) {

    p = findflag(p,"POINTERS","10I8");

    p=ncopy(cc,p,8); ok = ok && (sscanf(cc,"%d",&nAtom)==1);
    p=ncopy(cc,p,8); ok = ok && (sscanf(cc,"%d",&NTYPES)==1);
    p=ncopy(cc,p,8); ok = ok && (sscanf(cc,"%d",&NBONH)==1);
    p=ncopy(cc,p,8); ok = ok && (sscanf(cc,"%d",&MBONA)==1);
    p=ncopy(cc,p,8); ok = ok && (sscanf(cc,"%d",&NTHETH)==1);

    p=ncopy(cc,p,8); ok = ok && (sscanf(cc,"%d",&MTHETA)==1);
    p=ncopy(cc,p,8); ok = ok && (sscanf(cc,"%d",&NPHIH)==1);
    p=ncopy(cc,p,8); ok = ok && (sscanf(cc,"%d",&MPHIA)==1);
    p=ncopy(cc,p,8); ok = ok && (sscanf(cc,"%d",&NHPARM)==1);
    p=ncopy(cc,p,8); ok = ok && (sscanf(cc,"%d",&NPARM)==1);

    p=nextline_top(p);

    p=ncopy(cc,p,8); ok = ok && (sscanf(cc,"%d",&NNB)==1);
    p=ncopy(cc,p,8); ok = ok && (sscanf(cc,"%d",&NRES)==1);
    p=ncopy(cc,p,8); ok = ok && (sscanf(cc,"%d",&NBONA)==1);
    p=ncopy(cc,p,8); ok = ok && (sscanf(cc,"%d",&NTHETA)==1);
    p=ncopy(cc,p,8); ok = ok && (sscanf(cc,"%d",&NPHIA)==1);

    p=ncopy(cc,p,8); ok = ok && (sscanf(cc,"%d",&NUMBND)==1);
    p=ncopy(cc,p,8); ok = ok && (sscanf(cc,"%d",&NUMANG)==1);
    p=ncopy(cc,p,8); ok = ok && (sscanf(cc,"%d",&NPTRA)==1);
    p=ncopy(cc,p,8); ok = ok && (sscanf(cc,"%d",&NATYP)==1);
    p=ncopy(cc,p,8); ok = ok && (sscanf(cc,"%d",&NPHB)==1);

    p=nextline_top(p);

    p=ncopy(cc,p,8); ok = ok && (sscanf(cc,"%d",&IFPERT)==1);
    p=ncopy(cc,p,8); ok = ok && (sscanf(cc,"%d",&NBPER)==1);
    p=ncopy(cc,p,8); ok = ok && (sscanf(cc,"%d",&NGPER)==1);
    p=ncopy(cc,p,8); ok = ok && (sscanf(cc,"%d",&NDPER)==1);
    p=ncopy(cc,p,8); ok = ok && (sscanf(cc,"%d",&MBPER)==1);

    p=ncopy(cc,p,8); ok = ok && (sscanf(cc,"%d",&MGPER)==1);
    p=ncopy(cc,p,8); ok = ok && (sscanf(cc,"%d",&MDPER)==1);
    p=ncopy(cc,p,8); ok = ok && (sscanf(cc,"%d",&IFBOX)==1);
    p=ncopy(cc,p,8); ok = ok && (sscanf(cc,"%d",&NMXRS)==1);
    p=ncopy(cc,p,8); ok = ok && (sscanf(cc,"%d",&IFCAP)==1);
    
    p=nextline_top(p);
    p=ncopy(cc,p,8); ok = ok && (sscanf(cc,"%d",&NEXTRA)==1);

  } else {
    
    p=ncopy(cc,p,6); ok = ok && (sscanf(cc,"%d",&nAtom)==1);
    p=ncopy(cc,p,6); ok = ok && (sscanf(cc,"%d",&NTYPES)==1);
    p=ncopy(cc,p,6); ok = ok && (sscanf(cc,"%d",&NBONH)==1);
    p=ncopy(cc,p,6); ok = ok && (sscanf(cc,"%d",&MBONA)==1);
    p=ncopy(cc,p,6); ok = ok && (sscanf(cc,"%d",&NTHETH)==1);
    p=ncopy(cc,p,6); ok = ok && (sscanf(cc,"%d",&MTHETA)==1);
    
    p=ncopy(cc,p,6); ok = ok && (sscanf(cc,"%d",&NPHIH)==1);
    p=ncopy(cc,p,6); ok = ok && (sscanf(cc,"%d",&MPHIA)==1);
    p=ncopy(cc,p,6); ok = ok && (sscanf(cc,"%d",&NHPARM)==1);
    p=ncopy(cc,p,6); ok = ok && (sscanf(cc,"%d",&NPARM)==1);
    p=ncopy(cc,p,6); ok = ok && (sscanf(cc,"%d",&NNB)==1);
    p=ncopy(cc,p,6); ok = ok && (sscanf(cc,"%d",&NRES)==1);
    
    p=nextline_top(p);
    
    p=ncopy(cc,p,6); ok = ok && (sscanf(cc,"%d",&NBONA)==1);
    p=ncopy(cc,p,6); ok = ok && (sscanf(cc,"%d",&NTHETA)==1);
    p=ncopy(cc,p,6); ok = ok && (sscanf(cc,"%d",&NPHIA)==1);
    p=ncopy(cc,p,6); ok = ok && (sscanf(cc,"%d",&NUMBND)==1);
    p=ncopy(cc,p,6); ok = ok && (sscanf(cc,"%d",&NUMANG)==1);
    p=ncopy(cc,p,6); ok = ok && (sscanf(cc,"%d",&NPTRA)==1);
    
    p=ncopy(cc,p,6); ok = ok && (sscanf(cc,"%d",&NATYP)==1);
    p=ncopy(cc,p,6); ok = ok && (sscanf(cc,"%d",&NPHB)==1);
    p=ncopy(cc,p,6); ok = ok && (sscanf(cc,"%d",&IFPERT)==1);
    p=ncopy(cc,p,6); ok = ok && (sscanf(cc,"%d",&NBPER)==1);
    p=ncopy(cc,p,6); ok = ok && (sscanf(cc,"%d",&NGPER)==1);
    p=ncopy(cc,p,6); ok = ok && (sscanf(cc,"%d",&NDPER)==1);
    
    p=nextline_top(p);
    
    p=ncopy(cc,p,6); ok = ok && (sscanf(cc,"%d",&MBPER)==1);
    p=ncopy(cc,p,6); ok = ok && (sscanf(cc,"%d",&MGPER)==1);
    p=ncopy(cc,p,6); ok = ok && (sscanf(cc,"%d",&MDPER)==1);
    p=ncopy(cc,p,6); ok = ok && (sscanf(cc,"%d",&IFBOX)==1);
    p=ncopy(cc,p,6); ok = ok && (sscanf(cc,"%d",&NMXRS)==1);
    p=ncopy(cc,p,6); ok = ok && (sscanf(cc,"%d",&IFCAP)==1);
    
    p=ncopy(cc,p,6); if(sscanf(cc,"%d",&NEXTRA)!=1) NEXTRA=0;

  }

  switch(IFBOX) {
  case 2:
    cset->PeriodicBoxType = cCSet_Octahedral;
    break;
  case 1:
    cset->PeriodicBoxType = cCSet_Orthogonal;
    break;
  case 0:
  default:
    cset->PeriodicBoxType = cCSet_NoPeriodicity;
    break;
  }

  p=nextline_top(p);

  if(!ok) {
    ErrMessage("TOPStrToCoordSet","Error reading counts lines");
  } else {
    PRINTFB(FB_ObjectMolecule,FB_Blather)
      " TOPStr2CoordSet: read counts line nAtom %d NBONA %d NBONH %d\n",
      nAtom,NBONA,NBONH
      ENDFB;
  }

  if(ok) {  
    VLACheck(atInfo,AtomInfoType,nAtom);

    if(amber7) {
      p = findflag(p,"ATOM_NAME","20a4");
    }
    /* read atoms */

    b=0;
    for(a=0;a<nAtom;a++) {
      p=ncopy(cc,p,4);
      ai=atInfo+a;
      if(!sscanf(cc,"%s",ai->name))
        ai->name[0]=0;
      if((++b)==20) {
        b=0;
        p=nextline_top(p);
      }
    }
  
    if(b) p=nextline_top(p);

    if(!ok) {
      ErrMessage("TOPStrToCoordSet","Error reading atom names");
    } else {
      PRINTFB(FB_ObjectMolecule,FB_Blather)
        " TOPStr2CoordSet: read atom names.\n"
        ENDFB;
    }

    /* read charges */

    if(amber7) {
      p = findflag(p,"CHARGE","5E16.8");
    }

    b=0;
    for(a=0;a<nAtom;a++) {
      p=ncopy(cc,p,16);
      ai=atInfo+a;
      if(!sscanf(cc,"%f",&ai->partialCharge))
        ok=false;
      else {
        ai->partialCharge/=18.2223; /* convert to electron charge */
      }
      if((++b)==5) {
        b=0;
        p=nextline_top(p);
      }
    }

    if(!ok) {
      ErrMessage("TOPStrToCoordSet","Error reading charges");
    } else {
      PRINTFB(FB_ObjectMolecule,FB_Blather)
        " TOPStr2CoordSet: read charges.\n"
        ENDFB;
    }
    if(b) p=nextline_top(p);
  
    if(!amber7) {
      /* skip masses */
      
      p=skip_fortran(nAtom,5,p);
    }

    /* read LJ atom types */

    if(amber7) {
      p = findflag(p,"ATOM_TYPE_INDEX","10I8");
      col=10;
      wid=8;
    } else {
      col=12;
      wid=6;
    }

    b=0;
    for(a=0;a<nAtom;a++) {
      p=ncopy(cc,p,wid);
      ai=atInfo+a;
      if(!sscanf(cc,"%d",&ai->customType))
        ok=false;
      if((++b)==col) {
        b=0;
        p=nextline_top(p);
      }
    }
    if(b) p=nextline_top(p);

    if(!ok) {
      ErrMessage("TOPStrToCoordSet","Error LJ atom types");
    } else {
      PRINTFB(FB_ObjectMolecule,FB_Blather)
        " TOPStr2CoordSet: read LJ atom types.\n"
        ENDFB;
    }

    if(!amber7) {
      /* skip excluded atom counts */
      
      p=skip_fortran(nAtom,12,p);
      
      /* skip NB param arrays */
      
      p=skip_fortran(NTYPES*NTYPES,12,p);
    }

    /* read residue labels */

    if(amber7) {
      p = findflag(p,"RESIDUE_LABEL","20a4");
    }

    resn = Alloc(ResName,NRES);

    b=0;
    for(a=0;a<NRES;a++) {
      p=ncopy(cc,p,4);
      if(!sscanf(cc,"%s",resn[a]))
        resn[a][0]=0;
      if((++b)==20) {
        b=0;
        p=nextline_top(p);
      }
    }
    if(b) p=nextline_top(p);

    if(!ok) {
      ErrMessage("TOPStrToCoordSet","Error reading residue labels");
    } else {
      PRINTFB(FB_ObjectMolecule,FB_Blather)
        " TOPStr2CoordSet: read residue labels.\n"
        ENDFB;
    }

    /* read residue assignments */

    if(amber7) {
      p = findflag(p,"RESIDUE_POINTER","10I8");
      col=10;
      wid=8;
    } else {
      col=12;
      wid=6;
    }

    b=0;
    last_i=0;
    rc=0;
    for(a=0;a<NRES;a++) {
      p=ncopy(cc,p,wid);
      if(sscanf(cc,"%d",&at_i))
        {
          if(last_i)
            for(aa=(last_i-1);aa<(at_i-1);aa++) {
              ai = atInfo+aa;
              strcpy(ai->resn,resn[a-1]);
              ai->resv=rc;
              sprintf(ai->resi,"%d",rc);
            }
          rc++;
          last_i=at_i;
        }
      if((++b)==col) {
        b=0;
        p=nextline_top(p);
      }
    }
    if(b) p=nextline_top(p);
    if(last_i)
      for(aa=(last_i-1);aa<nAtom;aa++) {
        ai = atInfo+aa;
        strcpy(ai->resn,resn[NRES-1]);
        ai->resv=rc;
        sprintf(ai->resi,"%d",rc);
      }
    rc++;

    if(!ok) {
      ErrMessage("TOPStrToCoordSet","Error reading residues");
    } else {
      PRINTFB(FB_ObjectMolecule,FB_Blather)
        " TOPStr2CoordSet: read residues.\n"
        ENDFB;
    }

    FreeP(resn);

    if(!amber7) {  
      /* skip bond force constants */

      p=skip_fortran(NUMBND,5,p);
      
      /* skip bond lengths */

      p=skip_fortran(NUMBND,5,p);
      
      /* skip angle force constant */
      
      p=skip_fortran(NUMANG,5,p);
      
      /* skip angle eq */
      
      p=skip_fortran(NUMANG,5,p);
      
      /* skip dihedral force constant */
      
      p=skip_fortran(NPTRA,5,p);
      
      /* skip dihedral periodicity */
      
      p=skip_fortran(NPTRA,5,p);
      
      /* skip dihedral phases */
      
      p=skip_fortran(NPTRA,5,p);
      
      /* skip SOLTYs */
      
      p=skip_fortran(NATYP,5,p);
      
      /* skip LJ terms r12 */
      
      p=skip_fortran((NTYPES*(NTYPES+1))/2,5,p);
      
      /* skip LJ terms r6 */
      
      p=skip_fortran((NTYPES*(NTYPES+1))/2,5,p);
      
    }

    /* read bonds */

    if(amber7) {
      p = findflag(p,"BONDS_INC_HYDROGEN","10I8");
      col=10;
      wid=8;
    } else {
      col=12;
      wid=6;
    }
    
    nBond = NBONH + NBONA;

    bond=VLAlloc(BondType,nBond);
  
    bi = 0;
  

    b=0;
    c=0;
    i0=0;
    i1=0;
    for(a=0;a<3*NBONH;a++) {
      p=ncopy(cc,p,wid);
      i2=i1;
      i1=i0;
      if(!sscanf(cc,"%d",&i0))
        ok=false;
      if((++c)==3) {
        c=0;
        bd=bond+bi;
        bd->index[0]=(abs(i2)/3);
        bd->index[1]=(abs(i1)/3);
        bd->order=1;
        bd->stereo=0;
        bd->id = bi+1;
        bi++;
      }
      if((++b)==col) {
        b=0;
        p=nextline_top(p);
      }
    }
    if(b) p=nextline_top(p);

    if(!ok) {
      ErrMessage("TOPStrToCoordSet","Error hydrogen containing bonds");
    } else {
      PRINTFB(FB_ObjectMolecule,FB_Blather)
        " TOPStr2CoordSet: read %d hydrogen containing bonds.\n",NBONH
        ENDFB;
    }

    if(amber7) {
      p = findflag(p,"BONDS_WITHOUT_HYDROGEN","10I8");
      col=10;
      wid=8;
    } else {
      col=12;
      wid=6;
    }

    b=0;
    c=0;
    for(a=0;a<3*NBONA;a++) {
      p=ncopy(cc,p,wid);
      i2=i1;
      i1=i0;
      if(!sscanf(cc,"%d",&i0))
        ok=false;
      if((++c)==3) {
        c=0;
        bd=bond+bi;
        bd->index[0]=(abs(i2)/3);
        bd->index[1]=(abs(i1)/3);
        bd->order=0;
        bd->stereo=0;
        bd->id = bi+1;
        bi++;
      }
      if((++b)==col) {
        b=0;
        p=nextline_top(p);
      }
    }
    if(b) p=nextline_top(p);

    if(!ok) {
      ErrMessage("TOPStrToCoordSet","Error hydrogen free bonds");
    } else {
      PRINTFB(FB_ObjectMolecule,FB_Blather)
        " TOPStr2CoordSet: read %d hydrogen free bonds.\n",NBONA
        ENDFB;
    }

    if(!amber7) {
      /* skip hydrogen angles */
      
      p=skip_fortran(4*NTHETH,12,p);
      
      /* skip non-hydrogen angles */
      
      p=skip_fortran(4*NTHETA,12,p);
      
      /* skip hydrogen dihedrals */
      
      p=skip_fortran(5*NPHIH,12,p);
      
      /* skip non hydrogen dihedrals */
      
      p=skip_fortran(5*NPHIA,12,p);
      
      /* skip nonbonded exclusions */
      
      p=skip_fortran(NNB,12,p);
      
      /* skip hydrogen bonds ASOL */
      
      p=skip_fortran(NPHB,5,p);
      
      /* skip hydrogen bonds BSOL */
      
      p=skip_fortran(NPHB,5,p);
      
      /* skip HBCUT */
      
      p=skip_fortran(NPHB,5,p);
      
    }
    /* read AMBER atom types */

    if(amber7) {
      p = findflag(p,"AMBER_ATOM_TYPE","20a4");
    }

    b=0;
    for(a=0;a<nAtom;a++) {
      p=ncopy(cc,p,4);
      ai=atInfo+a;
      if(!sscanf(cc,"%s",ai->textType))
        ok=false;
      if((++b)==20) {
        b=0;
        p=nextline_top(p);
      }
    }
    if(b) p=nextline_top(p);

    if(!ok) {
      ErrMessage("TOPStrToCoordSet","Error reading atom types");
    } else {
      PRINTFB(FB_ObjectMolecule,FB_Blather)
        " TOPStr2CoordSet: read atom types.\n"
        ENDFB;
    }

    if(!amber7) {
      /* skip TREE classification */
      
      p=skip_fortran(nAtom,20,p);
      
      /* skip tree joining information */
      
      p=skip_fortran(nAtom,12,p);
      
      /* skip last atom rotated blah blah blah */
      
      p=skip_fortran(nAtom,12,p);

      if(IFBOX>0) {

        int IPTRES,NSPM,NSPSOL;

        p=ncopy(cc,p,12); ok = ok && sscanf(cc,"%d",&IPTRES);
        p=ncopy(cc,p,12); ok = ok && sscanf(cc,"%d",&NSPM);
        p=ncopy(cc,p,12); ok = ok && sscanf(cc,"%d",&NSPSOL);
    
        p=nextline_top(p);

        /* skip pressuem scaling */

        p=skip_fortran(NSPM,12,p);
  
        /* skip periodic box */

        p=nextline_top(p);

      }
      
      if(IFCAP>0) {
        p=nextline_top(p);
        p=nextline_top(p);
        p=nextline_top(p);
      }

      if(IFPERT>0) {

        /* skip perturbed bond atoms */

        p=skip_fortran(2*NBPER,12,p);    

        /* skip perturbed bond atom pointers */

        p=skip_fortran(2*NBPER,12,p);    

        /* skip perturbed angles */

        p=skip_fortran(3*NGPER,12,p);    

        /* skip perturbed angle pointers */

        p=skip_fortran(2*NGPER,12,p);    

        /* skip perturbed dihedrals */

        p=skip_fortran(4*NDPER,12,p);    

        /* skip perturbed dihedral pointers */

        p=skip_fortran(2*NDPER,12,p);    

        /* skip residue names */

        p=skip_fortran(NRES,20,p);    

        /* skip atom names */

        p=skip_fortran(nAtom,20,p);    

        /* skip atom symbols */

        p=skip_fortran(nAtom,20,p);    

        /* skip unused field */

        p=skip_fortran(nAtom,5,p);    

        /* skip perturbed flags */

        p=skip_fortran(nAtom,12,p);    

        /* skip LJ atom flags */

        p=skip_fortran(nAtom,12,p);    

        /* skip perturbed charges */

        p=skip_fortran(nAtom,5,p);    

      }

      if(IPOL>0) {

        /* skip atomic polarizabilities */

        p=skip_fortran(nAtom,5,p);    

      }

      if((IPOL>0) && (IFPERT>0)) {

        /* skip atomic polarizabilities */

        p=skip_fortran(nAtom,5,p);    
    
      }
    }
    /* for future reference 

%FLAG LES_NTYP
%FORMAT(10I8)
%FLAG LES_TYPE
%FORMAT(10I8)
%FLAG LES_FAC
%FORMAT(5E16.8)
%FLAG LES_CNUM
%FORMAT(10I8)
%FLAG LES_ID
%FORMAT(10I8)

Here is the additional information for LES topology formats:
First, if NPARM ==1, LES entries are in topology (NPARM is the 10th
entry in the initial list of control parameters); otherwise the standard
format applies.
So, with NPARM=1, you just need to read a few more things at the very
end of topology file:
LES_NTYP (format: I6) ... one number, number of LES types
and four arrays:
LES_TYPE (12I6) ... NATOM integer entries
LES_FAC (E16.8) ... LES_NTYPxLES_NTYP float entries
LES_CNUM (12I6) ... NATOM integer entries
LES_ID (12I6)   ... NATOM integer entries

and that's it. Your parser must have skipped this information because it
was at the end of the file. Maybe that's good enough.



     */

    coord=VLAlloc(float,3*nAtom);

    f=coord;
    for(a=0;a<nAtom;a++) {
      *(f++)=0.0;
      *(f++)=0.0;
      *(f++)=0.0;
      ai = atInfo + a;
      ai->id = a+1; /* assign 1-based identifiers */
      AtomInfoAssignParameters(ai);
      ai->color=AtomInfoGetColor(ai);
      for(c=0;c<cRepCnt;c++) {
        ai->visRep[c] = false;
      }
      ai->visRep[cRepLine] = auto_show_lines; /* show lines by default */
      ai->visRep[cRepNonbonded] = auto_show_nonbonded; /* show lines by default */
    }
  }
  if(ok) {
    cset->NIndex=nAtom;
    cset->Coord=coord;
    cset->TmpBond=bond;
    cset->NTmpBond=nBond;
  } else {
    if(cset) 
      cset->fFree(cset);
  }
  if(atInfoPtr)
	 *atInfoPtr = atInfo;
  
  return(cset);
}

/*========================================================================*/
ObjectMolecule *ObjectMoleculeReadTOPStr(ObjectMolecule *I,char *TOPStr,int frame,int discrete)
{
  CoordSet *cset = NULL;
  AtomInfoType *atInfo;
  int ok=true;
  int isNew = true;
  unsigned int nAtom = 0;

  if(!I) 
	 isNew=true;
  else 
	 isNew=false;

  if(ok) {

	 if(isNew) {
		I=(ObjectMolecule*)ObjectMoleculeNew(discrete);
		atInfo = I->AtomInfo;
		isNew = true;
	 } else { /* never */
		atInfo=VLAMalloc(10,sizeof(AtomInfoType),2,true); /* autozero here is important */
		isNew = false;
	 }
	 cset=ObjectMoleculeTOPStr2CoordSet(TOPStr,&atInfo);	 
	 nAtom=cset->NIndex;
  }

  /* include coordinate set */
  if(ok) {
    cset->Obj = I;
    cset->fEnumIndices(cset);
    if(cset->fInvalidateRep)
      cset->fInvalidateRep(cset,cRepAll,cRepInvRep);
    if(isNew) {		
      I->AtomInfo=atInfo; /* IMPORTANT to reassign: this VLA may have moved! */
    } else {
      ObjectMoleculeMerge(I,atInfo,cset,false,cAIC_AllMask); /* NOTE: will release atInfo */
    }
    if(isNew) I->NAtom=nAtom;
    /* 
       if(frame<0) frame=I->NCSet;
       VLACheck(I->CSet,CoordSet*,frame);
       if(I->NCSet<=frame) I->NCSet=frame+1;
       if(I->CSet[frame]) I->CSet[frame]->fFree(I->CSet[frame]);
       I->CSet[frame] = cset;
    */

    if(isNew) I->NBond = ObjectMoleculeConnect(I,&I->Bond,I->AtomInfo,cset,false);
    if(cset->Symmetry&&(!I->Symmetry)) {
      I->Symmetry=SymmetryCopy(cset->Symmetry);
      SymmetryAttemptGeneration(I->Symmetry);
    }

    if(I->CSTmpl)
      if(I->CSTmpl->fFree)
        I->CSTmpl->fFree(I->CSTmpl);
    I->CSTmpl = cset; /* save template coordinate set */

    SceneCountFrames();
    ObjectMoleculeExtendIndices(I);
    ObjectMoleculeSort(I);
    ObjectMoleculeUpdateIDNumbers(I);
    ObjectMoleculeUpdateNonbonded(I);
  }
  return(I);
}

ObjectMolecule *ObjectMoleculeLoadTOPFile(ObjectMolecule *obj,char *fname,int frame,int discrete)
{
  ObjectMolecule *I=NULL;
  int ok=true;
  FILE *f;
  long size;
  char *buffer,*p;

  f=fopen(fname,"rb");
  if(!f)
	 ok=ErrMessage("ObjectMoleculeLoadTOPFile","Unable to open file!");
  else
	 {
      PRINTFB(FB_ObjectMolecule,FB_Blather) 
        " ObjectMoleculeLoadTOPFile: Loading from %s.\n",fname
        ENDFB;
		
		fseek(f,0,SEEK_END);
      size=ftell(f);
		fseek(f,0,SEEK_SET);

		buffer=(char*)mmalloc(size+255);
		ErrChkPtr(buffer);
		p=buffer;
		fseek(f,0,SEEK_SET);
		fread(p,size,1,f);
		p[size]=0;
		fclose(f);

		I=ObjectMoleculeReadTOPStr(obj,buffer,frame,discrete);

		mfree(buffer);
	 }

  return(I);
}

void ObjectMoleculeSculptClear(ObjectMolecule *I)
{
  PRINTFD(FB_ObjectMolecule)
    " ObjectMoleculeSculptClear: entered.\n"
    ENDFD;

  if(I->Sculpt) SculptFree(I->Sculpt);
  I->Sculpt=NULL;
}

void ObjectMoleculeSculptImprint(ObjectMolecule *I,int state)
{
  PRINTFD(FB_ObjectMolecule)
    " ObjectMoleculeUpdateSculpt: entered.\n"
    ENDFD;

  if(!I->Sculpt) I->Sculpt = SculptNew();
  SculptMeasureObject(I->Sculpt,I,state);
}

void ObjectMoleculeSculptIterate(ObjectMolecule *I,int state,int n_cycle)
{
  PRINTFD(FB_ObjectMolecule)
    " ObjectMoleculeIterateSculpt: entered.\n"
    ENDFD;
  if(I->Sculpt) {
    SculptIterateObject(I->Sculpt,I,state,n_cycle);
  }
}

void ObjectMoleculeUpdateIDNumbers(ObjectMolecule *I)
{
  int a;
  int max;
  AtomInfoType *ai;
  BondType *b;

  if(I->AtomCounter<0) {
    max=-1;
    ai=I->AtomInfo;
    for(a=0;a<I->NAtom;a++) {
      if(ai->id>max)
        max=ai->id;
      ai++;
    }
    I->AtomCounter=max+1;
  }
  ai=I->AtomInfo;
  for(a=0;a<I->NAtom;a++) {
    if(ai->id<0) 
      ai->id=I->AtomCounter++;
    ai++;
  }

  if(I->BondCounter<0) {
    max=-1;
    b=I->Bond;
    for(a=0;a<I->NBond;a++) {
      if(b->id>max) 
        max=b->id;
      b++;
    }
    I->BondCounter=max+1;
  }
  b=I->Bond;
  for(a=0;a<I->NBond;a++) {
    if(!b->id) 
      b->id=I->BondCounter++;
    b++;
  }
}

CoordSet *ObjectMoleculePMO2CoordSet(CRaw *pmo,AtomInfoType **atInfoPtr,int *restart)
{
  int nAtom,nBond;
  int a;
  float *coord = NULL;
  CoordSet *cset = NULL;
  AtomInfoType *atInfo = NULL,*ai;
  AtomInfoType068 *atInfo068 = NULL;
  AtomInfoType076 *atInfo076 = NULL;
  AtomInfoType083 *atInfo083 = NULL;
  BondType *bond=NULL;
  BondType068 *bond068=NULL;
  BondType083 *bond083=NULL;

  int ok=true;
  int auto_show_lines;
  int auto_show_nonbonded;
  int type,size;
  float *spheroid=NULL;
  float *spheroid_normal=NULL;
  int sph_info[2];
  int version;
  auto_show_lines = SettingGet(cSetting_auto_show_lines);
  auto_show_nonbonded = SettingGet(cSetting_auto_show_nonbonded);
  AtomInfoPrimeColors();

  *restart=false;
  nAtom=0;
  nBond=0;
  if(atInfoPtr)
	 atInfo = *atInfoPtr;
  
  type = RawGetNext(pmo,&size,&version);
  if(type!=cRaw_AtomInfo1) {
    ok=false;
  } else { /* read atoms */
    PRINTFD(FB_ObjectMolecule)
      " ObjectMolPMO2CoordSet: loading atom info %d bytes = %8.3f\n",size,((float)size)/sizeof(AtomInfoType)
      ENDFD;
    if(version<66) {
      PRINTFB(FB_ObjectMolecule,FB_Errors)
        " ObjectMolecule: unsupported binary file (version %d). aborting.\n",
        version
        ENDFB;
      ok=false;
    } else if(version<69) { /* legacy atom format */
      nAtom = size/sizeof(AtomInfoType068);
      atInfo068 = Alloc(AtomInfoType068,nAtom);
      ok = RawReadInto(pmo,cRaw_AtomInfo1,size,(char*)atInfo068);
      VLACheck(atInfo,AtomInfoType,nAtom);
      UtilExpandArrayElements(atInfo068,atInfo,nAtom,
                              sizeof(AtomInfoType068),sizeof(AtomInfoType));
      FreeP(atInfo068);
    } else if(version<77) { /* legacy atom format */
      nAtom = size/sizeof(AtomInfoType076);
      atInfo076 = Alloc(AtomInfoType076,nAtom);
      ok = RawReadInto(pmo,cRaw_AtomInfo1,size,(char*)atInfo076);
      VLACheck(atInfo,AtomInfoType,nAtom);
      UtilExpandArrayElements(atInfo076,atInfo,nAtom,
                              sizeof(AtomInfoType076),sizeof(AtomInfoType));
      FreeP(atInfo076);
      
    } else if(version<84) { /* legacy atom format */
      nAtom = size/sizeof(AtomInfoType083);
      atInfo083 = Alloc(AtomInfoType083,nAtom);
      ok = RawReadInto(pmo,cRaw_AtomInfo1,size,(char*)atInfo083);
      VLACheck(atInfo,AtomInfoType,nAtom);
      UtilExpandArrayElements(atInfo083,atInfo,nAtom,
                              sizeof(AtomInfoType083),sizeof(AtomInfoType));
      FreeP(atInfo083);
      
    } else {
      nAtom = size/sizeof(AtomInfoType);
      VLACheck(atInfo,AtomInfoType,nAtom);
      ok = RawReadInto(pmo,cRaw_AtomInfo1,size,(char*)atInfo);
    }
  }
  if(ok) {
    PRINTFD(FB_ObjectMolecule)
      " ObjectMolPMO2CoordSet: loading coordinates\n"
      ENDFD;
    coord = (float*)RawReadVLA(pmo,cRaw_Coords1,sizeof(float),5,false);
    if(!coord)
      ok=false;
  }
  type = RawGetNext(pmo,&size,&version);
  if(type==cRaw_SpheroidInfo1) {

    PRINTFD(FB_ObjectMolecule)
      " ObjectMolPMO2CoordSet: loading spheroid\n"
      ENDFD;

    ok = RawReadInto(pmo,cRaw_SpheroidInfo1,sizeof(int)*2,(char*)sph_info);
    if(ok) {

    PRINTFD(FB_ObjectMolecule)
      " ObjectMolPMO2CoordSet: loading spheroid size %d nsph %d\n",sph_info[0],sph_info[1]
      ENDFD;

      spheroid = (float*)RawReadPtr(pmo,cRaw_Spheroid1,&size);
      if(!spheroid)
        ok=false;

      PRINTFD(FB_ObjectMolecule)
        " ObjectMolPMO2CoordSet: loaded spheroid %p size %d \n",spheroid,size
        ENDFD;

    }
    if(ok) {
      spheroid_normal = (float*)RawReadPtr(pmo,cRaw_SpheroidNormals1,&size);
      if(!spheroid_normal)
        ok=false;
      }
      PRINTFD(FB_ObjectMolecule)
        " ObjectMolPMO2CoordSet: loaded spheroid %p size %d \n",spheroid_normal,size
        ENDFD;

    } 
  if(ok) 
      type = RawGetNext(pmo,&size,&version);    
  if(ok) {
    
    PRINTFD(FB_ObjectMolecule)
      " ObjectMolPMO2CoordSet: loading bonds\n"
      ENDFD;

    if(type!=cRaw_Bonds1) {
      ok=false;
    } else {
      if(ok) {

        /* legacy bond format */
        if(version<66) {
          PRINTFB(FB_ObjectMolecule,FB_Errors)
            " ObjectMolecule: unsupported binary file (version %d). aborting.\n",
            version
            ENDFB;
          ok=false;
        } else if(version<69) { /* legacy atom format */
          nBond = size/sizeof(BondType068);
          bond068 = Alloc(BondType068,nBond);
          ok = RawReadInto(pmo,cRaw_Bonds1,nBond*sizeof(BondType068),(char*)bond068);
          bond=VLAlloc(BondType,nBond);
          UtilExpandArrayElements(bond068,bond,nBond,
                                  sizeof(BondType068),sizeof(BondType));
          FreeP(bond068);
          for(a=0;a<nBond;a++) bond[a].id=-1; /* initialize identifiers */
        } else if(version<84) {
          nBond = size/sizeof(BondType083);
          bond083 = Alloc(BondType083,nBond);
          ok = RawReadInto(pmo,cRaw_Bonds1,nBond*sizeof(BondType083),(char*)bond083);

          bond=VLAlloc(BondType,nBond);
          UtilExpandArrayElements(bond083,bond,nBond,
                                  sizeof(BondType083),sizeof(BondType));
          FreeP(bond083);
          
          bond=(BondType*)RawReadVLA(pmo,cRaw_Bonds1,sizeof(BondType),5,false);
          nBond = VLAGetSize(bond);
        }
        
        PRINTFD(FB_ObjectMolecule)
          " ObjectMolPMO2CoordSet: found %d bonds\n",nBond
          ENDFD;

        if(Feedback(FB_ObjectMolecule,FB_Debugging)) {
          for(a=0;a<nBond;a++)
            printf(" ObjectMoleculeConnect: bond %d ind0 %d ind1 %d order %d\n",
                   a,bond[a].index[0],bond[a].index[1],bond[a].order);
        }
        
      }
    }
  }

  if(ok) {
    ai=atInfo;
    for(a=0;a<nAtom;a++) {
      ai->selEntry=0;
      ai++;
    }
	 cset = CoordSetNew();
	 cset->NIndex=nAtom;
	 cset->Coord=coord;
	 cset->NTmpBond=nBond;
	 cset->TmpBond=bond;
    if(spheroid) {
      cset->Spheroid=spheroid;
      cset->SpheroidNormal=spheroid_normal;
      cset->SpheroidSphereSize=sph_info[0];
      cset->NSpheroid = sph_info[1];

    }
  } else {
	 VLAFreeP(bond);
	 VLAFreeP(coord);
    FreeP(spheroid);
    FreeP(spheroid_normal);
  }
  if(atInfoPtr)
	 *atInfoPtr = atInfo;
  if(ok) {
    type = RawGetNext(pmo,&size,&version);
    if(type==cRaw_AtomInfo1)
      *restart=true;
  }
  return(cset);
}
/*========================================================================*/
ObjectMolecule *ObjectMoleculeReadPMO(ObjectMolecule *I,CRaw *pmo,int frame,int discrete)
{

  CoordSet *cset = NULL;
  AtomInfoType *atInfo;
  int ok=true;
  int isNew = true;
  unsigned int nAtom = 0;
  int restart =false;
  int repeatFlag = true;
  int successCnt = 0;

  while(repeatFlag) {
    repeatFlag = false;
  
    if(!I) 
      isNew=true;
    else 
      isNew=false;
    
    if(ok) {
      
      if(isNew) {
        I=(ObjectMolecule*)ObjectMoleculeNew(discrete);
        atInfo = I->AtomInfo;
        isNew = true;
      } else {
        atInfo=VLAMalloc(10,sizeof(AtomInfoType),2,true); /* autozero here is important */
        isNew = false;
      }

      cset = ObjectMoleculePMO2CoordSet(pmo,&atInfo,&restart);

      if(isNew) {		
        I->AtomInfo=atInfo; /* IMPORTANT to reassign: this VLA may have moved! */
      }
      if(cset) 
        nAtom=cset->NIndex;
      else
        ok=false;
    }
    
    /* include coordinate set */
    if(ok) {
      cset->Obj = I;
      cset->fEnumIndices(cset);
      if(cset->fInvalidateRep)
        cset->fInvalidateRep(cset,cRepAll,cRepInvRep);
      if(!isNew) {		
        ObjectMoleculeMerge(I,atInfo,cset,true,cAIC_AllMask); /* NOTE: will release atInfo */
      }
      if(isNew) I->NAtom=nAtom;
      if(frame<0) frame=I->NCSet;
      VLACheck(I->CSet,CoordSet*,frame);
      if(I->NCSet<=frame) I->NCSet=frame+1;
      if(I->CSet[frame]) I->CSet[frame]->fFree(I->CSet[frame]);
      I->CSet[frame] = cset;
      if(isNew) I->NBond = ObjectMoleculeConnect(I,&I->Bond,I->AtomInfo,cset,false);

      if(cset->Symmetry&&(!I->Symmetry)) {
        I->Symmetry=SymmetryCopy(cset->Symmetry);
        SymmetryAttemptGeneration(I->Symmetry);
      }
      SceneCountFrames();
      ObjectMoleculeExtendIndices(I);
      ObjectMoleculeSort(I);
      ObjectMoleculeUpdateIDNumbers(I);
      ObjectMoleculeUpdateNonbonded(I);
      successCnt++;
      if(successCnt>1) {
        if(successCnt==2){
          PRINTFB(FB_ObjectMolecule,FB_Actions)
            " ObjectMolReadPMO: read model %d\n",1
            ENDFB;
            }
        PRINTFB(FB_ObjectMolecule,FB_Actions)
          " ObjectMolReadPMO: read model %d\n",successCnt
          ENDFB;
      }
    }
    if(restart) {
      repeatFlag=true;
      frame=frame+1;
      restart=false;
    }
  }
  return(I);
  
  }
/*========================================================================*/
ObjectMolecule *ObjectMoleculeLoadPMOFile(ObjectMolecule *obj,char *fname,int frame,int discrete)
{
  ObjectMolecule *I=NULL;
  int ok=true;
  CRaw *raw;
    
  raw = RawOpenRead(fname);
  if(!raw)
	 ok=ErrMessage("ObjectMoleculeLoadPMOFile","Unable to open file!");
  else
	 {
      PRINTFB(FB_ObjectMolecule,FB_Blather)
        " ObjectMoleculeLoadPMOFile: Loading from %s.\n",fname
        ENDFB;
		
		I=ObjectMoleculeReadPMO(obj,raw,frame,discrete);
      RawFree(raw);
	 }
  
  return(I);
}


/*========================================================================*/
int ObjectMoleculeMultiSave(ObjectMolecule *I,char *fname,int state,int append)
{
  /* version 1 writes atominfo, coords, spheroid, bonds */
  CRaw *raw = NULL;
  int ok=true;
  int a,c,a1,a2,b1,b2;
  BondType *b;
  CoordSet *cs;
  BondType *bondVLA = NULL;
  AtomInfoType *aiVLA = NULL;
  int start,stop;
  int nBond;
  int sph_info[2];
  PRINTFD(FB_ObjectMolecule)
    " ObjectMoleculeMultiSave-Debug: entered '%s' state=%d\n",fname,state
    ENDFD;
    
  if(append) {
    raw = RawOpenWrite(fname);
  } else {
    raw = RawOpenAppend(fname);
  }
  if(raw) {
    aiVLA = VLAMalloc(1000,sizeof(AtomInfoType),5,true);
    bondVLA = VLAlloc(BondType,4000);
    if(state<0) {
      start=0;
      stop=I->NCSet;
    } else {
      start=state;
      if(start<0)
        start=0;
      stop=state+1;
      if(stop>I->NCSet)
        stop=I->NCSet;
    }
    for(a=start;a<stop;a++) {

      PRINTFD(FB_ObjectMolecule)
        " ObjectMMSave-Debug: state %d\n",a
        ENDFD;

      cs=I->CSet[a];
      if(cs) {
        VLACheck(aiVLA,AtomInfoType,cs->NIndex);
        nBond=0;

        /* write atoms */
        for(c=0;c<cs->NIndex;c++) {
          a1 = cs->IdxToAtm[c]; /* always valid */
          aiVLA[c]=I->AtomInfo[a1];
        }
        if(ok) ok = RawWrite(raw,cRaw_AtomInfo1,sizeof(AtomInfoType)*cs->NIndex,0,(char*)aiVLA);
        
        /* write coords */
        if(ok) ok = RawWrite(raw,cRaw_Coords1,sizeof(float)*3*cs->NIndex,0,(char*)cs->Coord);

        /* write spheroid (if one exists) */
        if(cs->Spheroid&&cs->SpheroidNormal) {
          sph_info[0]=cs->SpheroidSphereSize;
          sph_info[1]=cs->NSpheroid;
          if(ok) ok = RawWrite(raw,cRaw_SpheroidInfo1,sizeof(int)*2,0,(char*)sph_info);          
          if(ok) ok = RawWrite(raw,cRaw_Spheroid1,sizeof(float)*cs->NSpheroid,0,(char*)cs->Spheroid);          
          if(ok) ok = RawWrite(raw,cRaw_SpheroidNormals1,sizeof(float)*3*cs->NSpheroid,0,(char*)cs->SpheroidNormal); 
          PRINTFD(FB_ObjectMolecule)
            " ObjectMolPMO2CoorSet: saved spheroid size %d %d\n",cs->SpheroidSphereSize,cs->NSpheroid
            ENDFD;
         
        }
        
        /* write bonds */
        b=I->Bond;
        for(c=0;c<I->NBond;c++) {
          b1 = b->index[0];
          b2 = b->index[1];
          if(I->DiscreteFlag) {
            if((cs==I->DiscreteCSet[b1])&&(cs==I->DiscreteCSet[b2])) {
              a1=I->DiscreteAtmToIdx[b1];
              a2=I->DiscreteAtmToIdx[b2];
            } else {
              a1=-1;
              a2=-1;
            }
          } else {
            a1=cs->AtmToIdx[b1];
            a2=cs->AtmToIdx[b2];
          }
          if((a1>=0)&&(a2>=0)) { 
            nBond++;
            VLACheck(bondVLA,BondType,nBond);
            bondVLA[nBond-1]=*b;
            bondVLA[nBond-1].index[0] = a1;
            bondVLA[nBond-1].index[1] = a2;
          }
          b++;

        }
        if(ok) ok = RawWrite(raw,cRaw_Bonds1,sizeof(BondType)*nBond,0,(char*)bondVLA);
      }
    }
  }
  if(raw) RawFree(raw);
  VLAFreeP(aiVLA);
  VLAFreeP(bondVLA);
  return(ok);
}
/*========================================================================*/
int ObjectMoleculeGetPhiPsi(ObjectMolecule *I,int ca,float *phi,float *psi,int state)
{
  int np=-1;
  int cm=-1;
  int c=-1;
  int n=-1;
  int result = false;
  AtomInfoType *ai;
  int n0,at;
  float v_ca[3];
  float v_n[3];
  float v_c[3];
  float v_cm[3];
  float v_np[3];

  ai=I->AtomInfo;

  if((ai[ca].name[0]=='C')&&(ai[ca].name[1]=='A'))
    {
      ObjectMoleculeUpdateNeighbors(I);
      
      /* find C */
      n0 = I->Neighbor[ca]+1;
      while(I->Neighbor[n0]>=0) {
        at = I->Neighbor[n0];
        if((ai[at].name[0]=='C')&&(ai[at].name[1]==0)) {
          c=at;
          break;
        }
        n0+=2;
      }
      
      /* find N */
      n0 = I->Neighbor[ca]+1;
      while(I->Neighbor[n0]>=0) {
        at = I->Neighbor[n0];
        if((ai[at].name[0]=='N')&&(ai[at].name[1]==0)) {
          n=at;
          break;
        }
        n0+=2;
      }
      
      /* find NP */
      if(c>=0) {
        n0 = I->Neighbor[c]+1;
        while(I->Neighbor[n0]>=0) {
          at = I->Neighbor[n0];
          if((ai[at].name[0]=='N')&&(ai[at].name[1]==0)) {
            np=at;
            break;
          }
        n0+=2;
        }
      }
      
      /* find CM */
      if(n>=0) {
        n0 = I->Neighbor[n]+1;
        while(I->Neighbor[n0]>=0) {
          at = I->Neighbor[n0];
          if((ai[at].name[0]=='C')&&(ai[at].name[1]==0)) {
            cm=at;
            break;
          }
          n0+=2;
        }
      }
      if((ca>=0)&&(np>=0)&&(c>=0)&&(n>=0)&&(cm>=0)) {
        if(ObjectMoleculeGetAtomVertex(I,state,ca,v_ca)&&
           ObjectMoleculeGetAtomVertex(I,state,n,v_n)&&
           ObjectMoleculeGetAtomVertex(I,state,c,v_c)&&
           ObjectMoleculeGetAtomVertex(I,state,cm,v_cm)&&
           ObjectMoleculeGetAtomVertex(I,state,np,v_np)) {

          (*phi)=rad_to_deg(get_dihedral3f(v_c,v_ca,v_n,v_cm));
          (*psi)=rad_to_deg(get_dihedral3f(v_np,v_c,v_ca,v_n));
          result=true;
        }
      }
    }
  return(result);
}
/*========================================================================*/
int ObjectMoleculeCheckBondSep(ObjectMolecule *I,int a0,int a1,int dist)
{
  int result = false;
  int n0;
  int stack[MAX_BOND_DIST+1];
  int history[MAX_BOND_DIST+1];
  int depth=0;
  int distinct;
  int a;
  if(dist>MAX_BOND_DIST)
    return false;
  
  ObjectMoleculeUpdateNeighbors(I);

  PRINTFD(FB_ObjectMolecule)
    " CBS-Debug: %s %d %d %d\n",I->Obj.Name,a0,a1,dist
    ENDFD;
  depth = 1;
  history[depth]=a0;
  stack[depth] = I->Neighbor[a0]+1; /* go to first neighbor */
  while(depth) { /* keep going until we've traversed tree */
    while(I->Neighbor[stack[depth]]>=0) /* end of branches? go back up one bond */
      {
        n0 = I->Neighbor[stack[depth]]; /* get current neighbor index */
        stack[depth]+=2; /* set up next neighbor */
        distinct=true; /* check to see if current candidate is distinct from ancestors */
        for(a=1;a<depth;a++) {
          if(history[a]==n0)
            distinct=false;
        }
        if(distinct) {
          if(depth<dist) { /* are not yet at the proper distance? */
            if(distinct) {
              depth++; 
              stack[depth] = I->Neighbor[n0]+1; /* then keep moving outward */
              history[depth] = n0;
            }
          } else if(n0==a1) /* otherwise, see if we have a match */
            result = true;
        }
      }
    depth--;
  }
  PRINTFD(FB_ObjectMolecule)
    " CBS-Debug: result %d\n",result
    ENDFD;
  return result;
}
/*========================================================================*/
void ObjectGotoState(ObjectMolecule *I,int state)
{
  if((I->NCSet>1)||(!SettingGet(cSetting_static_singletons))) {
    if(state>I->NCSet)
      state = I->NCSet-1;
    if(state<0)
      state = I->NCSet-1;
    SceneSetFrame(0,state);
  }
}
/*========================================================================*/
CSetting **ObjectMoleculeGetSettingHandle(ObjectMolecule *I,int state)
{
  
  if(state<0) {
    return(&I->Obj.Setting);
  } else if(state<I->NCSet) {
    if(I->CSet[state]) {
      return(&I->CSet[state]->Setting);
    } else {
      return(NULL);
    }
  } else {
    return(NULL);
  }
}
/*========================================================================*/
int ObjectMoleculeSetStateTitle(ObjectMolecule *I,int state,char *text)
{
  int result=false;
  if(state<0) state=I->NCSet-1;
  if(state>=I->NCSet) {
    PRINTFB(FB_ObjectMolecule,FB_Errors)
      "Error: invalid state %d\n",state +1
      ENDFB;
    
  } else if(!I->CSet[state]) {
    PRINTFB(FB_ObjectMolecule,FB_Errors)
      "Error: empty state %d\n",state +1
      ENDFB;
  } else {
    UtilNCopy(I->CSet[state]->Name,text,sizeof(WordType));
    result=true;
  }
  return(result);
}

/*========================================================================*/
char *ObjectMoleculeGetStateTitle(ObjectMolecule *I,int state)
{
  char *result=NULL;
  if(state<0) state=I->NCSet-1;
  if(state>=I->NCSet) {
    PRINTFB(FB_ObjectMolecule,FB_Errors)
      "Error: invalid state %d\n",state +1
      ENDFB;
  } else if(!I->CSet[state]) {
    PRINTFB(FB_ObjectMolecule,FB_Errors)
      "Error: empty state %d\n",state +1
      ENDFB;
  } else {
    result = I->CSet[state]->Name;
  }
  return(result);
}

/*========================================================================*/
void ObjectMoleculeRenderSele(ObjectMolecule *I,int curState,int sele)
{
  CoordSet *cs;
  int a,at;

  if(PMGUI) {
    if(curState>=0) {
      if(curState<I->NCSet) {
        if(I->CSet[curState]) {
          cs=I->CSet[curState];
          for(a=0;a<cs->NIndex;a++) {
            at=cs->IdxToAtm[a]; /* should work for both discrete and non-discrete objects */
            if(SelectorIsMember(I->AtomInfo[at].selEntry,sele))
              glVertex3fv(cs->Coord+3*a);
          }
        }
      } else if(SettingGet(cSetting_static_singletons)) {
        if(I->NCSet==1) {
          cs=I->CSet[0];
          if(cs) {
            for(a=0;a<cs->NIndex;a++) {
              at=cs->IdxToAtm[a]; /* should work for both discrete and non-discrete objects */
              if(SelectorIsMember(I->AtomInfo[at].selEntry,sele))
                glVertex3fv(cs->Coord+3*a);
            }
          }
        }
      }
    } else { /* all states */
      for(curState=0;curState<I->NCSet;curState++) {
        if(I->CSet[curState]) {
          cs=I->CSet[curState];
          for(a=0;a<cs->NIndex;a++) {
            at=cs->IdxToAtm[a]; /* should work for both discrete and non-discrete objects */
            if(SelectorIsMember(I->AtomInfo[at].selEntry,sele))
              glVertex3fv(cs->Coord+3*a);
          }
        }
      }
    }
  }
}

/*========================================================================*/
CoordSet *ObjectMoleculeXYZStr2CoordSet(char *buffer,AtomInfoType **atInfoPtr)
{
  char *p;
  int nAtom;
  int a,c;
  float *coord = NULL;
  CoordSet *cset = NULL;
  AtomInfoType *atInfo = NULL,*ai;
  char cc[MAXLINELEN];
  int atomCount;
  BondType *bond=NULL;
  int nBond=0;
  int b1,b2;
  WordType tmp_name;
  int auto_show_lines = SettingGet(cSetting_auto_show_lines);
  int auto_show_nonbonded = SettingGet(cSetting_auto_show_nonbonded);
  BondType *ii;


  AtomInfoPrimeColors();

  p=buffer;
  nAtom=0;
  atInfo = *atInfoPtr;
  
  p=ncopy(cc,p,6);  
  if(!sscanf(cc,"%d",&nAtom)) nAtom=0;
  p=nskip(p,2);
  p=ncopy(tmp_name,p,sizeof(WordType)-1);
  p=nextline_top(p);
      
  coord=VLAlloc(float,3*nAtom);

  if(atInfo)
	 VLACheck(atInfo,AtomInfoType,nAtom);
  
  nBond=0;
  bond=VLAlloc(BondType,6*nAtom);  
  ii=bond;

  PRINTFB(FB_ObjectMolecule,FB_Blather)
	 " ObjectMoleculeReadXYZ: Found %i atoms...\n",nAtom
    ENDFB;

  a=0;
  atomCount=0;
  
  while(*p)
	 {
      ai=atInfo+atomCount;
      
      p=ncopy(cc,p,6);
      if(!sscanf(cc,"%d",&ai->id)) break;
      
      p=nskip(p,2);/* to 12 */
      p=ncopy(cc,p,3); 
      if(!sscanf(cc,"%s",ai->name)) ai->name[0]=0;
      
      ai->alt[0]=0;
      strcpy(ai->resn,"UNK");
      ai->chain[0] = 0;
      
      ai->resv=atomCount+1;
      sprintf(ai->resi,"%d",ai->resv);
      
      p=ncopy(cc,p,12);
      sscanf(cc,"%f",coord+a);
      p=ncopy(cc,p,12);
      sscanf(cc,"%f",coord+(a+1));
      p=ncopy(cc,p,12);
      sscanf(cc,"%f",coord+(a+2));
      
      ai->q=1.0;
      ai->b=0.0;
      
      ai->segi[0]=0;
      ai->elem[0]=0; /* let atom info guess/infer atom type */
      
      for(c=0;c<cRepCnt;c++) {
        ai->visRep[c] = false;
      }
      ai->visRep[cRepLine] = auto_show_lines; /* show lines by default */
      ai->visRep[cRepNonbonded] = auto_show_nonbonded; /* show lines by default */
      
      p=ncopy(cc,p,6);
      sscanf(cc,"%d",&ai->customType);
      
      /* in the absense of external tinker information, assume hetatm */
      
      ai->hetatm=1;
      
      AtomInfoAssignParameters(ai);
      ai->color=AtomInfoGetColor(ai);
      
      b1 = atomCount;
      for(c=0;c<6;c++) {
        p=ncopy(cc,p,6);
        if (!cc[0]) 
          break;
        if(!sscanf(cc,"%d",&b2))
          break;
        if(b1<(b2-1)) {
          nBond++;
          ii->index[0] = b1;
          ii->index[1] = b2-1;
          ii->order = 1; /* missing bond order information */
          ii->stereo = 0;
          ii->id = -1; /* no serial number */
        }
      }
      
      PRINTFD(FB_ObjectMolecule) 
        " ObjectMolecule-DEBUG: %s %s %s %s %8.3f %8.3f %8.3f %6.2f %6.2f %s\n",
        ai->name,ai->resn,ai->resi,ai->chain,
        *(coord+a),*(coord+a+1),*(coord+a+2),ai->b,ai->q,
        ai->segi
        ENDFD;
      
      a+=3;
      atomCount++;
      if(atomCount>=nAtom)
        break;
      p=nextline_top(p);
    }

  PRINTFB(FB_ObjectMolecule,FB_Blather) 
   " XYZStr2CoordSet: Read %d bonds.\n",nBond
    ENDFB;

  cset = CoordSetNew();
  cset->NIndex=nAtom;
  cset->Coord=coord;
  cset->TmpBond=bond;
  cset->NTmpBond=nBond;
  strcpy(cset->Name,tmp_name);
  if(atInfoPtr)
	 *atInfoPtr = atInfo;
  return(cset);
}

/*========================================================================*/
ObjectMolecule *ObjectMoleculeReadXYZStr(ObjectMolecule *I,char *PDBStr,int frame,int discrete)
{
  CoordSet *cset = NULL;
  AtomInfoType *atInfo;
  int ok=true;
  int isNew = true;
  unsigned int nAtom = 0;

  if(!I) 
	 isNew=true;
  else 
	 isNew=false;

  if(ok) {

	 if(isNew) {
		I=(ObjectMolecule*)ObjectMoleculeNew(discrete);
		atInfo = I->AtomInfo;
		isNew = true;
	 } else {
		atInfo=VLAMalloc(10,sizeof(AtomInfoType),2,true); /* autozero here is important */
		isNew = false;
	 }
	 cset=ObjectMoleculeXYZStr2CoordSet(PDBStr,&atInfo);	 
	 nAtom=cset->NIndex;
  }

  /* include coordinate set */
  if(ok) {
    cset->Obj = I;
    cset->fEnumIndices(cset);
    if(cset->fInvalidateRep)
      cset->fInvalidateRep(cset,cRepAll,cRepInvRep);
    if(isNew) {		
      I->AtomInfo=atInfo; /* IMPORTANT to reassign: this VLA may have moved! */
    } else {
      ObjectMoleculeMerge(I,atInfo,cset,false,cAIC_IDMask); /* NOTE: will release atInfo */
    }

    if(isNew) I->NAtom=nAtom;
    if(frame<0) frame=I->NCSet;
    VLACheck(I->CSet,CoordSet*,frame);
    if(I->NCSet<=frame) I->NCSet=frame+1;
    if(I->CSet[frame]) I->CSet[frame]->fFree(I->CSet[frame]);
    I->CSet[frame] = cset;
    if(isNew) I->NBond = ObjectMoleculeConnect(I,&I->Bond,I->AtomInfo,cset,false);
    if(cset->Symmetry&&(!I->Symmetry)) {
      I->Symmetry=SymmetryCopy(cset->Symmetry);
      SymmetryAttemptGeneration(I->Symmetry);
    }
    SceneCountFrames();
    ObjectMoleculeExtendIndices(I);
    ObjectMoleculeSort(I);
    ObjectMoleculeUpdateIDNumbers(I);
    ObjectMoleculeUpdateNonbonded(I);
  }
  return(I);
}
/*========================================================================*/
ObjectMolecule *ObjectMoleculeLoadXYZFile(ObjectMolecule *obj,char *fname,int frame,int discrete)
{
  ObjectMolecule *I=NULL;
  int ok=true;
  FILE *f;
  long size;
  char *buffer,*p;

  f=fopen(fname,"rb");
  if(!f)
	 ok=ErrMessage("ObjectMoleculeLoadXYZFile","Unable to open file!");
  else
	 {
      PRINTFB(FB_ObjectMolecule,FB_Blather) 
        " ObjectMoleculeLoadXYZFile: Loading from %s.\n",fname
        ENDFB;
		
		fseek(f,0,SEEK_END);
      size=ftell(f);
		fseek(f,0,SEEK_SET);

		buffer=(char*)mmalloc(size+255);
		ErrChkPtr(buffer);
		p=buffer;
		fseek(f,0,SEEK_SET);
		fread(p,size,1,f);
		p[size]=0;
		fclose(f);

		I=ObjectMoleculeReadXYZStr(obj,buffer,frame,discrete);

		mfree(buffer);
	 }

  return(I);
}


/*========================================================================*/
int ObjectMoleculeAreAtomsBonded(ObjectMolecule *I,int i0,int i1)
{
  int result=false;
  int a;
  BondType *b;
  b=I->Bond;
  for (a=0;a<I->NBond;a++) {
    if(i0==b->index[0]) {
      if(i1==b->index[1]) {
        result=true;
        break;
      }
    }
    if(i1==b->index[0]) {
      if(i0==b->index[1]) {
        result=true;
        break;
      }
    }
    b++;
  }
  return(result);
}
/*========================================================================*/
void ObjectMoleculeRenameAtoms(ObjectMolecule *I,int force)
{
  AtomInfoType *ai;
  int a;
  if(force) {
    ai=I->AtomInfo;
    for(a=0;a<I->NAtom;a++)
      (ai++)->name[0]=0;
  }
  AtomInfoUniquefyNames(NULL,0,I->AtomInfo,I->NAtom);  
}
/*========================================================================*/
void ObjectMoleculeAddSeleHydrogens(ObjectMolecule *I,int sele)
{
  int a,b;
  int n,nn;
  CoordSet *cs;
  CoordSet *tcs;
  int seleFlag=false;
  AtomInfoType *ai,*nai,fakeH;
  int repeatFlag=false;
  int nH;
  int *index;
  float v[3],v0[3];
  float d;

  UtilZeroMem(&fakeH,sizeof(AtomInfoType));
  fakeH.protons=1;
  ai=I->AtomInfo;
  for(a=0;a<I->NAtom;a++) {
    if(SelectorIsMember(ai->selEntry,sele)) {
      seleFlag=true;
      break;
    }
    ai++;
  }
  if(seleFlag) {
    if(!ObjectMoleculeVerifyChemistry(I)) {
      ErrMessage(" AddHydrogens","missing chemical geometry information.");
    } else if(I->DiscreteFlag) {
      ErrMessage(" AddHydrogens","can't modify a discrete object.");
    } else {

      repeatFlag=true;
      while(repeatFlag) {
        repeatFlag=false;
        nH = 0;
        ObjectMoleculeUpdateNeighbors(I);
        nai = (AtomInfoType*)VLAMalloc(1000,sizeof(AtomInfoType),1,true);        
        ai=I->AtomInfo;
        for(a=0;a<I->NAtom;a++) {
          if(SelectorIsMember(ai->selEntry,sele)) {
            n = I->Neighbor[a];
            nn = I->Neighbor[n++];
            if(nn<ai->valence) {
              VLACheck(nai,AtomInfoType,nH);
              UtilNCopy((nai+nH)->elem,"H",2);
              (nai+nH)->geom=cAtomInfoSingle;
              (nai+nH)->valence=1;
              (nai+nH)->temp1 = a; /* borrowing this field temporarily */
              ObjectMoleculePrepareAtom(I,a,nai+nH);
              nH++;
            }
          }
          ai++;
        }

        if(nH) {

          repeatFlag=true;
          cs = CoordSetNew();
          cs->Coord = VLAlloc(float,nH*3);
          cs->NIndex=nH;

          index = Alloc(int,nH);
          for(a=0;a<nH;a++) {
            index[a] = (nai+a)->temp1;
          }
          
          if(cs->fEnumIndices) cs->fEnumIndices(cs);

          cs->TmpLinkBond = VLAlloc(BondType,nH);
          for(a=0;a<nH;a++) {
            cs->TmpLinkBond[a].index[0] = (nai+a)->temp1;
            cs->TmpLinkBond[a].index[1] = a;
            cs->TmpLinkBond[a].order = 1;
            cs->TmpLinkBond[a].stereo = 0;
            cs->TmpLinkBond[a].id = -1;
          }
          cs->NTmpLinkBond = nH;

          AtomInfoUniquefyNames(I->AtomInfo,I->NAtom,nai,nH);

          ObjectMoleculeMerge(I,nai,cs,false,cAIC_AllMask); /* will free nai and cs->TmpLinkBond  */
          ObjectMoleculeExtendIndices(I);
          ObjectMoleculeUpdateNeighbors(I);

          for(b=0;b<I->NCSet;b++) { /* add coordinate into the coordinate set */
            tcs = I->CSet[b];
            if(tcs) {
              for(a=0;a<nH;a++) {
                ObjectMoleculeGetAtomVertex(I,b,index[a],v0);
                ObjectMoleculeFindOpenValenceVector(I,b,index[a],v);
                d = AtomInfoGetBondLength(I->AtomInfo+index[a],&fakeH);
                scale3f(v,d,v);
                add3f(v0,v,cs->Coord+3*a);
              }
              CoordSetMerge(tcs,cs);  
            }
          }
          FreeP(index);
          if(cs->fFree)
            cs->fFree(cs);
          ObjectMoleculeSort(I);
          ObjectMoleculeUpdateIDNumbers(I);
        } else
          VLAFreeP(nai);
      }
    }
  }
}


/*========================================================================*/
void ObjectMoleculeFuse(ObjectMolecule *I,int index0,ObjectMolecule *src,int index1,int mode)
{
  int a,b;
  AtomInfoType *ai0,*ai1,*nai;
  int n,nn;
  int at0=-1;
  int at1=-1;
  int a0,a1;
  int hydr1=-1;
  int anch1=-1;
  int ca0,ch0;
  BondType *b0,*b1;
  float *backup = NULL;
  float d,*f0,*f1;
  float va0[3],vh0[3],va1[3],vh1[3];
  float x0[3],y0[3],z0[3];
  float x1[3],y1[3],z1[3];
  float x[3],y[3],z[3];
  float t[3],t2[3];
  CoordSet *cs=NULL,*scs=NULL;
  int state1 = 0;
  CoordSet *tcs;
  int edit=1;
  OrthoLineType sele1,sele2,s1,s2;

  ObjectMoleculeUpdateNeighbors(I);
  ObjectMoleculeUpdateNeighbors(src);

  /* make sure each link point has only one neighbor */

  ai0=I->AtomInfo;
  ai1=src->AtomInfo;
  switch(mode) {
  case 0: /* fusing by replacing hydrogens */
    
    n = I->Neighbor[index0];
    nn = I->Neighbor[n++];
    if(nn==1)
      at0 = I->Neighbor[n];
    
    n = src->Neighbor[index1];
    nn = src->Neighbor[n++];
    if(nn==1)
      at1 = src->Neighbor[n];
    
    if(src->NCSet) {
      scs = src->CSet[state1];
      anch1 = scs->AtmToIdx[at1];
      hydr1 = scs->AtmToIdx[index1];
    }
    break;
  case 1: /* fuse merely by drawing a bond */
    at0 = index0;
    at1 = index1;
    
    if(src->NCSet) {
      scs = src->CSet[state1];
      anch1 = scs->AtmToIdx[at1];
    }

    break;
  }
  
  if((at0>=0)&&(at1>=0)&&scs&&(anch1>=0)) { /* have anchors and source coordinate set */

    nai=(AtomInfoType*)VLAMalloc(src->NAtom,sizeof(AtomInfoType),1,true);
    
    /* copy atoms and atom info into a 1:1 direct mapping */

    cs = CoordSetNew();
    cs->Coord = VLAlloc(float,scs->NIndex*3);
    cs->NIndex = scs->NIndex;
    for(a=0;a<scs->NIndex;a++) {
      copy3f(scs->Coord+a*3,cs->Coord+a*3);
      a1 = scs->IdxToAtm[a];
      *(nai+a) = *(ai1+a1);
      (nai+a)->selEntry=0; /* avoid duplicating selection references -> leads to hangs ! */
      (nai+a)->temp1=0; /* clear marks */
    }

    nai[at1].temp1=2; /* mark the connection point */
    
    /* copy internal bond information*/

    cs->TmpBond = VLAlloc(BondType,src->NBond);
    b1 = src->Bond;
    b0 = cs->TmpBond;
    cs->NTmpBond=0;
    for(a=0;a<src->NBond;a++) {
      a0 = scs->AtmToIdx[b1->index[0]];
      a1 = scs->AtmToIdx[b1->index[1]];
      if((a0>=0)&&(a1>=0)) {
        *b0=*b1;
        b0->index[0] = a0;
        b0->index[1] = a1;
        b0++;
        cs->NTmpBond++;
      }
      b1++;
    }

    backup = Alloc(float,cs->NIndex*3); /* make untransformed copy of coordinate set */
    for(a=0;a<cs->NIndex;a++) {
      copy3f(cs->Coord+a*3,backup+a*3);
    }
    
    switch(mode) {
    case 0:
      nai[hydr1].deleteFlag=true;
      I->AtomInfo[index0].deleteFlag=true;
      copy3f(backup+3*anch1,va1);
      copy3f(backup+3*hydr1,vh1);
      subtract3f(va1,vh1,x1); /* note reverse dir from above */
      get_system1f3f(x1,y1,z1);
      break;
    case 1:
      copy3f(backup+3*anch1,va1);
      ObjectMoleculeFindOpenValenceVector(src,state1,at1,x1);
      scale3f(x1,-1.0,x1);
      get_system1f3f(x1,y1,z1);      
      break;
    }

    /* set up the linking bond */

    cs->TmpLinkBond = VLAlloc(BondType,1);
    cs->NTmpLinkBond = 1;
    cs->TmpLinkBond->index[0] = at0;
    cs->TmpLinkBond->index[1] = anch1;
    cs->TmpLinkBond->order = 1;
    cs->TmpLinkBond->stereo = 0;
    cs->TmpLinkBond->id = -1;
    
    if(cs->fEnumIndices) cs->fEnumIndices(cs);

    d = AtomInfoGetBondLength(ai0+at0,ai1+at1);

    AtomInfoUniquefyNames(I->AtomInfo,I->NAtom,nai,cs->NIndex);

    /* set up tags which will enable use to continue editing bond */

    if(edit) {
      for(a=0;a<I->NAtom;a++) {
        I->AtomInfo[a].temp1=0;
      }
      I->AtomInfo[at0].temp1=1;
    }

    ObjectMoleculeMerge(I,nai,cs,false,cAIC_AllMask); /* will free nai, cs->TmpBond and cs->TmpLinkBond  */

    ObjectMoleculeExtendIndices(I);
    ObjectMoleculeUpdateNeighbors(I);
    for(a=0;a<I->NCSet;a++) { /* add coordinate into the coordinate set */
      tcs = I->CSet[a];
      if(tcs) {
        switch(mode) {
        case 0:
          ca0 = tcs->AtmToIdx[at0]; /* anchor */
          ch0 = tcs->AtmToIdx[index0]; /* hydrogen */

          if((ca0>=0)&&(ch0>=0)) {
            copy3f(tcs->Coord+3*ca0,va0);
            copy3f(tcs->Coord+3*ch0,vh0);
            subtract3f(vh0,va0,x0);
            get_system1f3f(x0,y0,z0);

          }
          break;
        case 1:
          ca0 = tcs->AtmToIdx[at0]; /* anchor */

          if(ca0>=0) {
            ObjectMoleculeFindOpenValenceVector(I,a,at0,x0);
            copy3f(tcs->Coord+3*ca0,va0);
            get_system1f3f(x0,y0,z0);
            
          }
          break;
        }
        scale3f(x0,d,t2);
        add3f(va0,t2,t2);
        
        f0=backup;
        f1=cs->Coord;
        for(b=0;b<cs->NIndex;b++) { /* brute force transformation */
          subtract3f(f0,va1,t);
          scale3f(x0,dot_product3f(t,x1),x);
          scale3f(y0,dot_product3f(t,y1),y);
          scale3f(z0,dot_product3f(t,z1),z);
          add3f(x,y,y);
          add3f(y,z,f1);
          add3f(t2,f1,f1);
          f0+=3;
          f1+=3;
        }
        CoordSetMerge(tcs,cs); 
      }
    }
    switch(mode) {
    case 0:
      ObjectMoleculePurge(I);
      break;
    }
    ObjectMoleculeSort(I);
    ObjectMoleculeUpdateIDNumbers(I);
    if(edit) { /* edit the resulting bond */
      at0=-1;
      at1=-1;
      for(a=0;a<I->NAtom;a++) {
        if(I->AtomInfo[a].temp1==1)
          at0=a;
        if(I->AtomInfo[a].temp1==2)
          at1=a;
      }
      if((at0>=0)&&(at1>=0)) {
        sprintf(sele1,"%s`%d",I->Obj.Name,at1+1); /* points outward... */
        sprintf(sele2,"%s`%d",I->Obj.Name,at0+1);
        SelectorGetTmp(sele1,s1);
        SelectorGetTmp(sele2,s2);
        EditorSelect(s1,s2,NULL,NULL,false);
        SelectorFreeTmp(s1);
        SelectorFreeTmp(s2);
      }
    }
  }
  if(cs)
    if(cs->fFree)
      cs->fFree(cs);
  FreeP(backup);
}
/*========================================================================*/
int ObjectMoleculeVerifyChemistry(ObjectMolecule *I)
{
  int result=false;
  AtomInfoType *ai;
  int a;
  int flag;
  ai=I->AtomInfo;
  flag=true;
  for(a=0;a<I->NAtom;a++) {
    if(!ai->chemFlag) {
      flag=false;
    }
    ai++;
  }
  if(!flag) {
    if(I->CSet[0]) { /* right now this stuff is locked to state 0 */
      ObjectMoleculeInferChemFromBonds(I,0);
      ObjectMoleculeInferChemFromNeighGeom(I,0);
      /*      ObjectMoleculeInferChemForProtein(I,0);*/
    }
    flag=true;
    ai=I->AtomInfo;
    for(a=0;a<I->NAtom;a++) {
      if(!ai->chemFlag) {
        flag=false;
        break;
      }
      ai++;
    }
  }
  if(flag)
    result=true;
  return(result);
}
/*========================================================================*/
void ObjectMoleculeAttach(ObjectMolecule *I,int index,AtomInfoType *nai)
{
  int a;
  AtomInfoType *ai;
  int n,nn;
  float v[3],v0[3],d;
  CoordSet *cs;

  ObjectMoleculeUpdateNeighbors(I);
  ai=I->AtomInfo+index;
  n = I->Neighbor[index];
  nn = I->Neighbor[n++];
  
  cs = CoordSetNew();
  cs->Coord = VLAlloc(float,3);
  cs->NIndex=1;
  cs->TmpLinkBond = VLAlloc(BondType,1);
  cs->NTmpLinkBond = 1;
  cs->TmpLinkBond->index[0]=index;
  cs->TmpLinkBond->index[1]=0;
  cs->TmpLinkBond->order=1;
  cs->TmpLinkBond->stereo=0;
  
  cs->TmpLinkBond->id = -1;
  if(cs->fEnumIndices) cs->fEnumIndices(cs);
  ObjectMoleculePrepareAtom(I,index,nai);
  d = AtomInfoGetBondLength(ai,nai);
  ObjectMoleculeMerge(I,nai,cs,false,cAIC_AllMask); /* will free nai and cs->TmpLinkBond  */
  ObjectMoleculeExtendIndices(I);
  ObjectMoleculeUpdateNeighbors(I);
  for(a=0;a<I->NCSet;a++) { /* add atom to each coordinate set */
    if(I->CSet[a]) {
      ObjectMoleculeGetAtomVertex(I,a,index,v0);
      ObjectMoleculeFindOpenValenceVector(I,a,index,v);
      scale3f(v,d,v);
      add3f(v0,v,cs->Coord);
      CoordSetMerge(I->CSet[a],cs); 
    }
  }
  ObjectMoleculeSort(I);
  ObjectMoleculeUpdateIDNumbers(I);
  if(cs->fFree)
    cs->fFree(cs);
  
}
/*========================================================================*/
int ObjectMoleculeFillOpenValences(ObjectMolecule *I,int index)
{
  int a;
  AtomInfoType *ai,*nai;
  int n,nn;
  int result=0;
  int flag = true;
  float v[3],v0[3],d;
  CoordSet *cs;

  if((index>=0)&&(index<=I->NAtom)) {  
    while(1) {
      ObjectMoleculeUpdateNeighbors(I);
      ai=I->AtomInfo+index;
      n = I->Neighbor[index];
      nn = I->Neighbor[n++];
      
      if((nn>=ai->valence)||(!flag))
        break;
      flag=false;

      cs = CoordSetNew();
      cs->Coord = VLAlloc(float,3);
      cs->NIndex=1;
      cs->TmpLinkBond = VLAlloc(BondType,1);
      cs->NTmpLinkBond = 1;
      cs->TmpLinkBond->index[0]=index;
      cs->TmpLinkBond->index[1]=0;
      cs->TmpLinkBond->order=1;
      cs->TmpLinkBond->stereo=0;
      
      cs->TmpLinkBond->id = -1;
      if(cs->fEnumIndices) cs->fEnumIndices(cs);
      nai = (AtomInfoType*)VLAMalloc(1,sizeof(AtomInfoType),1,true);
      UtilNCopy(nai->elem,"H",2);
      nai->geom=cAtomInfoSingle;
      nai->valence=1;
      ObjectMoleculePrepareAtom(I,index,nai);
      d = AtomInfoGetBondLength(ai,nai);
      ObjectMoleculeMerge(I,nai,cs,false,cAIC_AllMask); /* will free nai and cs->TmpLinkBond  */
      ObjectMoleculeExtendIndices(I);
      ObjectMoleculeUpdateNeighbors(I);
      for(a=0;a<I->NCSet;a++) { /* add atom to each coordinate set */
        if(I->CSet[a]) {
          ObjectMoleculeGetAtomVertex(I,a,index,v0);
          ObjectMoleculeFindOpenValenceVector(I,a,index,v);
          scale3f(v,d,v);
          add3f(v0,v,cs->Coord);
          CoordSetMerge(I->CSet[a],cs); 
        }
      }
      if(cs->fFree)
        cs->fFree(cs);
      result++;
      flag=true;
    }
  }
  ObjectMoleculeUpdateIDNumbers(I);
  return(result);
}
/*========================================================================*/
int ObjectMoleculeFindOpenValenceVector(ObjectMolecule *I,int state,int index,float *v)
{
  #define MaxOcc 100
  CoordSet *cs;
  int nOcc = 0;
  float occ[MaxOcc*3];
  int n;
  int a1;
  float v0[3],v1[3],n0[3],t[3];
  int result = false;
  AtomInfoType *ai,*ai1;
  float y[3],z[3];

  /* default is +X */
  v[0]=1.0;
  v[1]=0.0;
  v[2]=0.0;
  
  if(state<0) state=0;
  if(I->NCSet==1) state=0;
  state = state % I->NCSet;
  cs = I->CSet[state];
  if(cs) {
    if((index>=0)&&(index<=I->NAtom)) {
      ai=I->AtomInfo+index;
      if(ObjectMoleculeGetAtomVertex(I,state,index,v0)) {              
        ObjectMoleculeUpdateNeighbors(I);
        n = I->Neighbor[index];
        n++; /* skip count */
        while(1) { /* look for an attached non-hydrogen as a base */
          a1 = I->Neighbor[n];
          n+=2; 
          if(a1<0) break;
          ai1=I->AtomInfo+a1;
          if(ObjectMoleculeGetAtomVertex(I,state,a1,v1)) {        
            subtract3f(v1,v0,n0);
            normalize3f(n0);
            copy3f(n0,occ+3*nOcc);
            nOcc++; 
            if(nOcc==MaxOcc) /* safety valve */
              break;
          }
        }
        if((!nOcc)||(nOcc>4)||(ai->geom==cAtomInfoNone)) {
          get_random3f(v);
        } else {
          switch(nOcc) {
          case 1:
            switch(ai->geom) {
            case cAtomInfoTetrahedral:
              get_system1f3f(occ,y,z);
              scale3f(occ,-0.334,v);
              scale3f(z,  0.943,t);
              add3f(t,v,v);              
              break;
            case cAtomInfoPlaner:
              get_system1f3f(occ,y,z);
              scale3f(occ,-0.500,v);
              scale3f(z,      0.866,t);
              add3f(t,v,v);
              break;
            case cAtomInfoLinear:
              scale3f(occ,-1.0,v);
              break;
            default:
              get_random3f(v); /* hypervalent */
              break;
            }
            break;
          case 2:
            switch(ai->geom) {
            case cAtomInfoTetrahedral:
              add3f(occ,occ+3,t);
              get_system2f3f(t,occ,z);
              scale3f(t,-1.0,v);
              scale3f(z,1.41,t);
              add3f(t,v,v);              
              break;
            case cAtomInfoPlaner:
            add3f(occ,occ+3,t);
              scale3f(t,-1.0,v);
              break;
            default:
              get_random3f(v); /* hypervalent */
              break;
            }
            break;
          case 3:
            switch(ai->geom) {
            case cAtomInfoTetrahedral:
              add3f(occ,occ+3,t);
              add3f(occ+6,t,t);
              scale3f(t,-1.0,v);
              break;
            default:
              get_random3f(v); /* hypervalent */
              break;
              
            }
            break;
          case 4:
            get_random3f(v); /* hypervalent */
            break;
          }
        }
      }
    }
  }
  normalize3f(v);
  return(result); /* return false if we're using the default */
#undef MaxOcc

}
/*========================================================================*/
void ObjectMoleculeCreateSpheroid(ObjectMolecule *I,int average)
{
  CoordSet *cs;
  float *spheroid = NULL;
  int a,b,c,a0;
  SphereRec *sp;
  float *spl;
  float *v,*v0,*s,*f,ang,min_dist,*max_sq;
  int *i;
  float *center = NULL;
  float d0[3],n0[3],d1[3],d2[3];
  float p0[3],p1[3],p2[3];
  int t0,t1,t2,bt0,bt1,bt2;
  float dp,l,*fsum = NULL;
  float *norm = NULL;
  float spheroid_smooth;
  float spheroid_fill;
  float spheroid_ratio=0.1; /* minimum ratio of width over length */
  float spheroid_minimum = 0.02; /* minimum size - to insure valid normals */
  int row,*count=NULL,base;
  int nRow;
  int first=0;
  int last=0;
  int current;
  int cscount;
  int n_state=0;
  sp=Sphere1;
  
  nRow = I->NAtom*sp->nDot;


  center=Alloc(float,I->NAtom*3);
  count=Alloc(int,I->NAtom);
  fsum=Alloc(float,nRow);
  max_sq = Alloc(float,I->NAtom);

  spl=spheroid;

  spheroid_smooth=SettingGet(cSetting_spheroid_smooth);
  spheroid_fill=SettingGet(cSetting_spheroid_fill);
  /* first compute average coordinate */

  if(average<1)
    average=I->NCSet;
  current=0;
  cscount=0;
  while(current<I->NCSet) {
    if(I->CSet[current]) {
      if(!cscount)
        first=current;
      cscount++;
      last=current+1;
    }
    
    if(cscount==average)
      {
        PRINTFB(FB_ObjectMolecule,FB_Details)
          " ObjectMolecule: computing spheroid from states %d to %d.\n",
                 first+1,last
          ENDFB;

        spheroid=Alloc(float,nRow);
        
        v=center;
        i = count;
        for(a=0;a<I->NAtom;a++) {
          *(v++)=0.0;
          *(v++)=0.0;
          *(v++)=0.0;
          *(i++)=0;
        }

        for(b=first;b<last;b++) {
          cs=I->CSet[b];
          if(cs) {
            v = cs->Coord;
            for(a=0;a<cs->NIndex;a++) {
              a0=cs->IdxToAtm[a];
              v0 = center+3*a0;
              add3f(v,v0,v0);
              (*(count+a0))++;
              v+=3;
            }
          }
        }

        i=count;
        v=center;
        for(a=0;a<I->NAtom;a++) 
          if(*i) {
            (*(v++))/=(*i);
            (*(v++))/=(*i);
            (*(v++))/=(*i++);
          } else {
            v+=3;
            i++;
          }

        /* now go through and compute radial distances */

        f = fsum;
        s = spheroid;
        for(a=0;a<nRow;a++) {
          *(f++)=0.0;
          *(s++)=0.0; 
        }

        v = max_sq;
        for(a=0;a<I->NAtom;a++)
          *(v++)=0.0;

        for(b=first;b<last;b++) {
          cs=I->CSet[b];
          if(cs) {
            v = cs->Coord;
            for(a=0;a<cs->NIndex;a++) {
              a0=cs->IdxToAtm[a];
              base = (a0*sp->nDot);
              v0 = center+(3*a0);
              subtract3f(v,v0,d0); /* subtract from average */
              l = lengthsq3f(d0);
              if(l>max_sq[a0])
                max_sq[a0]=l;
              if(l>0.0) {
                scale3f(d0,1.0/sqrt(l),n0);
                for(c=0;c<sp->nDot;c++) { /* average over spokes */
                  dp=dot_product3f(sp->dot[c].v,n0);
                  row = base + c;
                  if(dp>=0.0) {
                    ang = acos(dp);
                    ang=(ang/spheroid_smooth)*(cPI/2.0); 
                    if(ang>spheroid_fill)
                      ang=spheroid_fill;
                    /* take envelop to zero over that angle */
                    if(ang<=(cPI/2.0)) {
                      dp = cos(ang);
                      fsum[row] += dp*dp;
                      spheroid[row] += l*dp*dp*dp;
                    }
                  }
                }
              }
              v+=3;
            }
          }
        }

        f=fsum;
        s=spheroid;
        for(a=0;a<I->NAtom;a++) {
          min_dist = spheroid_ratio*sqrt(max_sq[a]);
          if(min_dist<spheroid_minimum)
            min_dist=spheroid_minimum;
          for(b=0;b<sp->nDot;b++) {
            if(*f>R_SMALL4) {
              (*s)=sqrt((*s)/(*(f++))); /* we put the "rm" in "rms" */
            } else {
              f++;
            }
            if(*s<min_dist)
              *s=min_dist;
            s++;
          }
        }

        /* set frame 0 coordinates to the average */

         cs=I->CSet[first];
         if(cs) {
           v = cs->Coord;
           for(a=0;a<cs->NIndex;a++) {
             a0=cs->IdxToAtm[a];
             v0 = center+3*a0;
             copy3f(v0,v);
             v+=3;
           }
         }

        /* now compute surface normals */

        norm = Alloc(float,nRow*3);
        for(a=0;a<nRow;a++) {
          zero3f(norm+a*3);
        }
        for(a=0;a<I->NAtom;a++) {
          base = a*sp->nDot;
          for(b=0;b<sp->NTri;b++) {
            t0 = sp->Tri[b*3  ];
            t1 = sp->Tri[b*3+1];
            t2 = sp->Tri[b*3+2];
            bt0 = base + t0;
            bt1 = base + t1;
            bt2 = base + t2;
            copy3f(sp->dot[t0].v,p0);
            copy3f(sp->dot[t1].v,p1);
            copy3f(sp->dot[t2].v,p2);
            /*      scale3f(sp->dot[t0].v,spheroid[bt0],p0);
                    scale3f(sp->dot[t1].v,spheroid[bt1],p1);
                    scale3f(sp->dot[t2].v,spheroid[bt2],p2);*/
            subtract3f(p1,p0,d1);
            subtract3f(p2,p0,d2);
            cross_product3f(d1,d2,n0);
            normalize3f(n0);
            v = norm+bt0*3;
            add3f(n0,v,v);
            v = norm+bt1*3;
            add3f(n0,v,v);
            v = norm+bt2*3;
            add3f(n0,v,v);
          }
        }

        f=norm;
        for(a=0;a<I->NAtom;a++) {
          base = a*sp->nDot;
          for(b=0;b<sp->nDot;b++) {
            normalize3f(f);
            f+=3;
          }
        }
  
        if(I->CSet[first]) {
          I->CSet[first]->Spheroid=spheroid;
          I->CSet[first]->SpheroidNormal=norm;
          I->CSet[first]->NSpheroid=nRow;
        } else {
          FreeP(spheroid);
          FreeP(norm);
        }

        for(b=first+1;b<last;b++) { 
          cs=I->CSet[b];
          if(cs) {
            if(cs->fFree)
              cs->fFree(cs);
          }
          I->CSet[b]=NULL;
        }
        
        if(n_state!=first) {
          I->CSet[n_state]=I->CSet[first];
          I->CSet[first]=NULL;
        }
        n_state++;

        cscount=0;
      }
    current++;
  }
  I->NCSet=n_state;
  FreeP(center);
  FreeP(count);
  FreeP(fsum);
  FreeP(max_sq);

  ObjectMoleculeInvalidate(I,cRepSphere,cRepInvProp);
}
/*========================================================================*/
void ObjectMoleculeReplaceAtom(ObjectMolecule *I,int index,AtomInfoType *ai)
{
  if((index>=0)&&(index<=I->NAtom)) {
    memcpy(I->AtomInfo+index,ai,sizeof(AtomInfoType));
    ObjectMoleculeInvalidate(I,cRepAll,cRepInvAtoms);
    /* could we put in a refinement step here? */
  }
}
/*========================================================================*/
void ObjectMoleculePrepareAtom(ObjectMolecule *I,int index,AtomInfoType *ai)
{
  /* match existing properties of the old atom */
  int a;
  AtomInfoType *ai0;

  if((index>=0)&&(index<=I->NAtom)) {
    ai0=I->AtomInfo + index;
    ai->resv=ai0->resv;
    ai->hetatm=ai0->hetatm;
    ai->flags=ai0->flags;
    ai->geom=ai0->geom; /* ?*/
    strcpy(ai->chain,ai0->chain);
    strcpy(ai->alt,ai0->alt);
    strcpy(ai->resi,ai0->resi);
    strcpy(ai->segi,ai0->segi);
    strcpy(ai->resn,ai0->resn);    
    if((ai->elem[0]==ai0->elem[0])&&(ai->elem[1]==ai0->elem[1]))
      ai->color=ai0->color;
    else
      ai->color=AtomInfoGetColor(ai);
    for(a=0;a<cRepCnt;a++)
      ai->visRep[a]=ai0->visRep[a];
    ai->id=-1;
    AtomInfoUniquefyNames(I->AtomInfo,I->NAtom,ai,1);
    AtomInfoAssignParameters(ai);
  }
}
/*========================================================================*/
void ObjectMoleculePreposReplAtom(ObjectMolecule *I,int index,
                                   AtomInfoType *ai)
{
  int n;
  int a1;
  AtomInfoType *ai1;
  float v0[3],v1[3],v[3];
  float d0[3],d,n0[3];
  int cnt;
  float t[3],sum[3];
  int a;
  int ncycle;
  ObjectMoleculeUpdateNeighbors(I);
  for(a=0;a<I->NCSet;a++) {
    if(I->CSet[a]) {
      if(ObjectMoleculeGetAtomVertex(I,a,index,v0)) {
        copy3f(v0,v); /* default is direct superposition */
        ncycle=-1;
        while(ncycle) {
          cnt = 0;
          n = I->Neighbor[index];
          n++; /* skip count */
          zero3f(sum);
          while(1) { /* look for an attached non-hydrogen as a base */
            a1 = I->Neighbor[n];
            n+=2;
            if(a1<0) break;
            ai1=I->AtomInfo+a1;
            if(ai1->protons!=1) 
              if(ObjectMoleculeGetAtomVertex(I,a,a1,v1)) {        
                d = AtomInfoGetBondLength(ai,ai1);
                subtract3f(v0,v1,n0);
                normalize3f(n0);
                scale3f(n0,d,d0);
                add3f(d0,v1,t);
                add3f(t,sum,sum);
                cnt++;
              }
          }
          if(cnt) {
            scale3f(sum,1.0/cnt,sum);
            copy3f(sum,v0);
            if((cnt>1)&&(ncycle<0))
              ncycle=5;
          }
          ncycle=abs(ncycle)-1;
        }
        if(cnt) copy3f(sum,v);
        ObjectMoleculeSetAtomVertex(I,a,index,v);            
      }
    }
  }
}
/*========================================================================*/
void ObjectMoleculeSaveUndo(ObjectMolecule *I,int state,int log)
{
  CoordSet *cs;

  FreeP(I->UndoCoord[I->UndoIter]);
  I->UndoState[I->UndoIter]=-1;
  if(state<0) state=0;
  if(I->NCSet==1) state=0;
  state = state % I->NCSet;
  cs = I->CSet[state];
  if(cs) {
    I->UndoCoord[I->UndoIter] = Alloc(float,cs->NIndex*3);
    memcpy(I->UndoCoord[I->UndoIter],cs->Coord,sizeof(float)*cs->NIndex*3);
    I->UndoState[I->UndoIter]=state;
    I->UndoNIndex[I->UndoIter] = cs->NIndex;
  }
  I->UndoIter=cUndoMask&(I->UndoIter+1);
  ExecutiveSetLastObjectEdited((CObject*)I);
  if(log) {
    OrthoLineType line;
    if(SettingGet(cSetting_logging)) {
      sprintf(line,"cmd.push_undo(\"%s\",%d)\n",I->Obj.Name,state+1);
      PLog(line,cPLog_no_flush);
    }
  }

}
/*========================================================================*/
void ObjectMoleculeUndo(ObjectMolecule *I,int dir)
{
  CoordSet *cs;
  int state;

  FreeP(I->UndoCoord[I->UndoIter]);
  I->UndoState[I->UndoIter]=-1;
  state=SceneGetState();
  if(state<0) state=0;
  if(I->NCSet==1) state=0;
  state = state % I->NCSet;
  cs = I->CSet[state];
  if(cs) {
    I->UndoCoord[I->UndoIter] = Alloc(float,cs->NIndex*3);
    memcpy(I->UndoCoord[I->UndoIter],cs->Coord,sizeof(float)*cs->NIndex*3);
    I->UndoState[I->UndoIter]=state;
    I->UndoNIndex[I->UndoIter] = cs->NIndex;
  }

  I->UndoIter=cUndoMask&(I->UndoIter+dir);
  if(!I->UndoCoord[I->UndoIter])
    I->UndoIter=cUndoMask&(I->UndoIter-dir);

  if(I->UndoState[I->UndoIter]>=0) {
    state=I->UndoState[I->UndoIter];
    if(state<0) state=0;
    
    if(I->NCSet==1) state=0;
    state = state % I->NCSet;
    cs = I->CSet[state];
    if(cs) {
      if(cs->NIndex==I->UndoNIndex[I->UndoIter]) {
        memcpy(cs->Coord,I->UndoCoord[I->UndoIter],sizeof(float)*cs->NIndex*3);
        I->UndoState[I->UndoIter]=-1;
        FreeP(I->UndoCoord[I->UndoIter]);
        if(cs->fInvalidateRep)
          cs->fInvalidateRep(cs,cRepAll,cRepInvCoord);
        SceneChanged();
      }
    }
  }
}
/*========================================================================*/
int ObjectMoleculeAddBond(ObjectMolecule *I,int sele0,int sele1,int order)
{
  int a1,a2;
  AtomInfoType *ai1,*ai2;
  int s1,s2;
  int c = 0;
  BondType *bnd;

  ai1=I->AtomInfo;
  for(a1=0;a1<I->NAtom;a1++) {
    s1=ai1->selEntry;
    if(SelectorIsMember(s1,sele0)) {
      ai2=I->AtomInfo;
      for(a2=0;a2<I->NAtom;a2++) {
        s2=ai2->selEntry;
        if(SelectorIsMember(s2,sele1)) {
          {
            VLACheck(I->Bond,BondType,I->NBond);
            bnd = I->Bond+(I->NBond);
            bnd->index[0]=a1;
            bnd->index[1]=a2;                      
            bnd->order=order;
            bnd->stereo = 0;
            bnd->id=-1;
            I->NBond++;
            c++;
            I->AtomInfo[a1].chemFlag=false;
            I->AtomInfo[a2].chemFlag=false;
          }
        }
        ai2++;
      }
    }
    ai1++;
  }
  if(c) {
    ObjectMoleculeInvalidate(I,cRepLine,cRepInvBonds);
    ObjectMoleculeInvalidate(I,cRepCyl,cRepInvBonds);
    ObjectMoleculeInvalidate(I,cRepNonbonded,cRepInvBonds);
    ObjectMoleculeInvalidate(I,cRepNonbondedSphere,cRepInvBonds);
    ObjectMoleculeInvalidate(I,cRepRibbon,cRepInvBonds);
    ObjectMoleculeInvalidate(I,cRepCartoon,cRepInvBonds);
    ObjectMoleculeUpdateIDNumbers(I);
  }
  return(c);    
}
/*========================================================================*/
int ObjectMoleculeAdjustBonds(ObjectMolecule *I,int sele0,int sele1,int mode,int order)
{
  int a0,a1;
  int offset=0;
  BondType *b0;
  int both;
  int s;
  int a;

  offset=0;
  b0=I->Bond;
  for(a=0;a<I->NBond;a++) {
    a0=b0->index[0];
    a1=b0->index[1];
    
    both=0;
    s=I->AtomInfo[a0].selEntry;
    if(SelectorIsMember(s,sele0))
      both++;
    s=I->AtomInfo[a1].selEntry;
    if(SelectorIsMember(s,sele1))
      both++;
    if(both<2) { /* reverse combo */
      both=0;
      s=I->AtomInfo[a1].selEntry;
      if(SelectorIsMember(s,sele0))
        both++;
      s=I->AtomInfo[a0].selEntry;
      if(SelectorIsMember(s,sele1))
        both++;
    }

    if(both==2) {
      switch(mode) {
      case 0: /* cycle */
        b0->order++;
        if(b0->order>3)
          b0->order=1;
        I->AtomInfo[a0].chemFlag=false;
        I->AtomInfo[a1].chemFlag=false;
        break;
      case 1: /* set */
        b0->order=order;
        I->AtomInfo[a0].chemFlag=false;
        I->AtomInfo[a1].chemFlag=false;
        break;
      }
      ObjectMoleculeInvalidate(I,cRepLine,cRepInvBonds);
      ObjectMoleculeInvalidate(I,cRepCyl,cRepInvBonds);
      ObjectMoleculeInvalidate(I,cRepNonbonded,cRepInvBonds);
      ObjectMoleculeInvalidate(I,cRepNonbondedSphere,cRepInvBonds);
      ObjectMoleculeInvalidate(I,cRepRibbon,cRepInvBonds);
      ObjectMoleculeInvalidate(I,cRepCartoon,cRepInvBonds);
    }
    b0++;
  }
  return(-offset);
}
/*========================================================================*/
int ObjectMoleculeRemoveBonds(ObjectMolecule *I,int sele0,int sele1)
{
  int a0,a1;
  int offset=0;
  BondType *b0,*b1;
  int both;
  int s;
  int a;

  offset=0;
  b0=I->Bond;
  b1=I->Bond;
  for(a=0;a<I->NBond;a++) {
    a0=b0->index[0];
    a1=b0->index[1];
    
    both=0;
    s=I->AtomInfo[a0].selEntry;
    if(SelectorIsMember(s,sele0))
      both++;
    s=I->AtomInfo[a1].selEntry;
    if(SelectorIsMember(s,sele1))
      both++;
    if(both<2) { /* reverse combo */
      both=0;
      s=I->AtomInfo[a1].selEntry;
      if(SelectorIsMember(s,sele0))
        both++;
      s=I->AtomInfo[a0].selEntry;
      if(SelectorIsMember(s,sele1))
        both++;
    }
    
    if(both==2) {
      offset--;
      b0++;
      I->AtomInfo[a0].chemFlag=false;
      I->AtomInfo[a1].chemFlag=false;
    } else if(offset) {
      *(b1++)=*(b0++); /* copy bond info */
    } else {
      *(b1++)=*(b0++); /* copy bond info */
    }
  }
  if(offset) {
    I->NBond += offset;
    VLASize(I->Bond,BondType,I->NBond);
    ObjectMoleculeInvalidate(I,cRepLine,cRepInvBonds);
    ObjectMoleculeInvalidate(I,cRepCyl,cRepInvBonds);
    ObjectMoleculeInvalidate(I,cRepNonbonded,cRepInvBonds);
    ObjectMoleculeInvalidate(I,cRepNonbondedSphere,cRepInvBonds);
    ObjectMoleculeInvalidate(I,cRepRibbon,cRepInvBonds);
    ObjectMoleculeInvalidate(I,cRepCartoon,cRepInvBonds);
  }

  return(-offset);
}
/*========================================================================*/
void ObjectMoleculePurge(ObjectMolecule *I)
{
  int a,a0,a1;
  int *oldToNew = NULL;
  int offset=0;
  BondType *b0,*b1;
  AtomInfoType *ai0,*ai1;
  
  PRINTFD(FB_ObjectMolecule)
    " ObjMolPurge-Debug: step 1, delete object selection\n"
    ENDFD;

  SelectorDelete(I->Obj.Name); /* remove the object selection and free up any selection entries*/

  PRINTFD(FB_ObjectMolecule)
    " ObjMolPurge-Debug: step 2, purge coordinate sets\n"
    ENDFD;

  for(a=0;a<I->NCSet;a++)
	 if(I->CSet[a]) 
      CoordSetPurge(I->CSet[a]);
  if(I->CSTmpl) {
    CoordSetPurge(I->CSTmpl);
  }
  PRINTFD(FB_ObjectMolecule)
    " ObjMolPurge-Debug: step 3, old-to-new mapping\n"
    ENDFD;

  oldToNew = Alloc(int,I->NAtom);
  ai0=I->AtomInfo;
  ai1=I->AtomInfo;
  for(a=0;a<I->NAtom;a++) {
    if(ai0->deleteFlag) {
      offset--;
      ai0++;
      oldToNew[a]=-1;
    } else if(offset) {
      *(ai1++)=*(ai0++);
      oldToNew[a]=a+offset;
    } else {
      oldToNew[a]=a;
      ai0++;
      ai1++;
    }
  }

  if(offset) {
    I->NAtom += offset;
    VLASize(I->AtomInfo,AtomInfoType,I->NAtom);
    for(a=0;a<I->NCSet;a++)
      if(I->CSet[a])
        CoordSetAdjustAtmIdx(I->CSet[a],oldToNew,I->NAtom);
  }

  PRINTFD(FB_ObjectMolecule)
    " ObjMolPurge-Debug: step 4, bonds\n"
    ENDFD;
  
  offset=0;
  b0=I->Bond;
  b1=I->Bond;
  for(a=0;a<I->NBond;a++) {
    a0=b0->index[0];
    a1=b0->index[1];
    if((oldToNew[a0]<0)||(oldToNew[a1]<0)) {
      offset--;
      b0++;
    } else if(offset) {
      *b1=*b0;
      b1->index[0]=oldToNew[a0]; /* copy bond info */
      b1->index[1]=oldToNew[a1];
      b0++;
      b1++;
    } else {
      *b1=*b0;
      b1->index[0]=oldToNew[a0]; /* copy bond info */
      b1->index[1]=oldToNew[a1];
      b0++;
      b1++; /* TODO check reasoning agaist above */
    }
  }
  if(offset) {
    I->NBond += offset;
    VLASize(I->Bond,BondType,I->NBond);
  }
  FreeP(oldToNew);

  PRINTFD(FB_ObjectMolecule)
    " ObjMolPurge-Debug: step 5, invalidate...\n"
    ENDFD;

  ObjectMoleculeInvalidate(I,cRepAll,cRepInvAtoms);

  PRINTFD(FB_ObjectMolecule)
    " ObjMolPurge-Debug: leaving...\n"
    ENDFD;

}
/*========================================================================*/
int ObjectMoleculeGetAtomGeometry(ObjectMolecule *I,int state,int at)
{
  /* this determines hybridization from coordinates in those few cases
   * where it is unambiguous */

  int result = -1;
  int n,nn;
  float v0[3],v1[3],v2[3],v3[3];
  float d1[3],d2[3],d3[3];
  float cp1[3],cp2[3],cp3[3];
  float avg;
  float dp;
  n  = I->Neighbor[at];
  nn = I->Neighbor[n++]; /* get count */
  if(nn==4) 
    result = cAtomInfoTetrahedral; 
  else if(nn==3) {
    /* check cross products */
    ObjectMoleculeGetAtomVertex(I,state,at,v0);    
    ObjectMoleculeGetAtomVertex(I,state,I->Neighbor[n],v1);
    ObjectMoleculeGetAtomVertex(I,state,I->Neighbor[n+2],v2);
    ObjectMoleculeGetAtomVertex(I,state,I->Neighbor[n+4],v3);
    subtract3f(v1,v0,d1);
    subtract3f(v2,v0,d2);
    subtract3f(v3,v0,d3);
    cross_product3f(d1,d2,cp1);
    cross_product3f(d2,d3,cp2);
    cross_product3f(d3,d1,cp3);
    normalize3f(cp1);
    normalize3f(cp2);
    normalize3f(cp3);
    avg=(dot_product3f(cp1,cp2)+
         dot_product3f(cp2,cp3)+
         dot_product3f(cp3,cp1))/3.0;
    if(avg>0.75)
      result=cAtomInfoPlaner;
    else
      result=cAtomInfoTetrahedral;
  } else if(nn==2) {
    ObjectMoleculeGetAtomVertex(I,state,at,v0);    
    ObjectMoleculeGetAtomVertex(I,state,I->Neighbor[n],v1);
    ObjectMoleculeGetAtomVertex(I,state,I->Neighbor[n+2],v2);
    subtract3f(v1,v0,d1);
    subtract3f(v2,v0,d2);
    normalize3f(d1);
    normalize3f(d2);
    dp = dot_product3f(d1,d2);
    if(dp<-0.75)
      result=cAtomInfoLinear;
  }
  return(result);
}
/*========================================================================*/
void ObjectMoleculeInferChemForProtein(ObjectMolecule *I,int state)
{
  /* Infers chemical relations for a molecules under protein assumptions.
   * 
   * NOTE: this routine needs an all-atom model (with hydrogens!)
   * and it will make mistakes on non-protein atoms (if they haven't
   * already been assigned)
  */

  int a,n,a0,a1,nn;
  int changedFlag = true;
  
  AtomInfoType *ai,*ai0,*ai1=NULL;
  
  ObjectMoleculeUpdateNeighbors(I);

  /* first, try to find all amids and acids */
  while(changedFlag) {
    changedFlag=false;
    for(a=0;a<I->NAtom;a++) {
      ai=I->AtomInfo+a;
      if(ai->chemFlag) {
        if(ai->geom==cAtomInfoPlaner)
          if(ai->protons == cAN_C) {
            n = I->Neighbor[a];
            nn = I->Neighbor[n++];
            if(nn>1) {
              a1 = -1;
              while(1) {
                a0 = I->Neighbor[n];
                n+=2;
                if(a0<0) break;
                ai0 = I->AtomInfo+a0;
                if((ai0->protons==cAN_O)&&(!ai0->chemFlag)) {
                  a1=a0;
                  ai1=ai0; /* found candidate carbonyl */
                  break;
                }
              }
              if(a1>0) {
                n = I->Neighbor[a]+1;
                while(1) {
                  a0 = I->Neighbor[n];
                  if(a0<0) break;
                  n+=2;
                  if(a0!=a1) {
                    ai0 = I->AtomInfo+a0;
                    if(ai0->protons==cAN_O) {
                      if(!ai0->chemFlag) {
                        ai0->chemFlag=true; /* acid */
                        ai0->geom=cAtomInfoPlaner;
                        ai0->valence=1;
                        ai1->chemFlag=true;
                        ai1->geom=cAtomInfoPlaner;
                        ai1->valence=1;
                        changedFlag=true;
                        break;
                      }
                    } else if(ai0->protons==cAN_N) {
                      if(!ai0->chemFlag) { 
                        ai0->chemFlag=true; /* amide N */ 
                        ai0->geom=cAtomInfoPlaner;                            
                        ai0->valence=3;
                        ai1->chemFlag=true; /* amide =O */ 
                        ai1->geom=cAtomInfoPlaner;
                        ai1->valence=1;
                        changedFlag=true;
                        break;
                      } else if(ai0->geom==cAtomInfoPlaner) {
                        ai1->chemFlag=true; /* amide =O */
                        ai1->geom=cAtomInfoPlaner;
                        ai1->valence=1;
                        changedFlag=true;
                        break;
                      }
                    }
                  }
                }
              }
            }
          }
      }
    }
  }
  /* then handle aldehydes and amines (partial amides - both missing a valence) */
  
  changedFlag=true;
  while(changedFlag) {
    changedFlag=false;
    for(a=0;a<I->NAtom;a++) {
      ai=I->AtomInfo+a;
      if(!ai->chemFlag) {
        if(ai->protons==cAN_C) {
          n = I->Neighbor[a];
          nn = I->Neighbor[n++];
          if(nn>1) {
            a1 = -1;
            while(1) {
              a0 = I->Neighbor[n];
              n+=2;
              if(a0<0) break;
              ai0 = I->AtomInfo+a0;
              if((ai0->protons==cAN_O)&&(!ai0->chemFlag)) { /* =O */
                ai->chemFlag=true; 
                ai->geom=cAtomInfoPlaner;
                ai->valence=1;
                ai0->chemFlag=true;
                ai0->geom=cAtomInfoPlaner;
                ai0->valence=3;
                changedFlag=true;
                break;
              }
            }
          }
        }
        else if(ai->protons==cAN_N)
          {
            if((!ai->chemFlag)||ai->geom!=cAtomInfoLinear) {
              if(ai->formalCharge==0.0) {
                ai->chemFlag=true; 
                ai->geom=cAtomInfoPlaner;
                ai->valence=3;
              }
            }
          }
      }
    }
  }

}
/*========================================================================*/
void ObjectMoleculeInferChemFromNeighGeom(ObjectMolecule *I,int state)
{
  /* infers chemical relations from neighbors and geometry 
  * NOTE: very limited in scope */

  int a,n,a0,nn;
  int changedFlag=true;
  int geom;
  int carbonVal[10];
  
  AtomInfoType *ai,*ai2;

  carbonVal[cAtomInfoTetrahedral] = 4;
  carbonVal[cAtomInfoPlaner] = 3;
  carbonVal[cAtomInfoLinear] = 2;
  
  ObjectMoleculeUpdateNeighbors(I);
  while(changedFlag) {
    changedFlag=false;
    for(a=0;a<I->NAtom;a++) {
      ai=I->AtomInfo+a;
      if(!ai->chemFlag) {
        geom=ObjectMoleculeGetAtomGeometry(I,state,a);
        switch(ai->protons) {
        case cAN_K:
          ai->chemFlag=1;
          ai->geom=cAtomInfoNone;
          ai->valence=0;
          break;
        case cAN_H:
        case cAN_F:
        case cAN_I:
        case cAN_Br:
          ai->chemFlag=1;
          ai->geom=cAtomInfoSingle;
          ai->valence=1;
          break;
        case cAN_O:
          n = I->Neighbor[a];
          nn = I->Neighbor[n++];
          if(nn!=1) { /* water, hydroxy, ether */
            ai->chemFlag=1;
            ai->geom=cAtomInfoTetrahedral;
            ai->valence=2;
          } else { /* hydroxy or carbonyl? check carbon geometry */
            a0 = I->Neighbor[n+2];
            ai2=I->AtomInfo+a0;
            if(ai2->chemFlag) {
              if((ai2->geom==cAtomInfoTetrahedral)||
                 (ai2->geom==cAtomInfoLinear)) {
                ai->chemFlag=1; /* hydroxy */
                ai->geom=cAtomInfoTetrahedral;
                ai->valence=2;
              }
            }
          }
          break;
        case cAN_C:
          if(geom>=0) {
            ai->geom = geom;
            ai->valence = carbonVal[geom];
            ai->chemFlag=true;
          } else {
            n = I->Neighbor[a];
            nn = I->Neighbor[n++];
            if(nn==1) { /* only one neighbor */
              ai2=I->AtomInfo+I->Neighbor[n];
              if(ai2->chemFlag&&(ai2->geom==cAtomInfoTetrahedral)) {
                ai->chemFlag=true; /* singleton carbon bonded to tetC must be tetC */
                ai->geom=cAtomInfoTetrahedral;
                ai->valence=4;
              }
            }
          }
          break;
        case cAN_N:
          if(geom==cAtomInfoPlaner) {
            ai->chemFlag=true;
            ai->geom=cAtomInfoPlaner;
            ai->valence=3;
          } else if(geom==cAtomInfoTetrahedral) {
            ai->chemFlag=true;
            ai->geom=cAtomInfoTetrahedral;
            ai->valence=4;
          }
          break;
        case cAN_S:
          n = I->Neighbor[a];
          nn = I->Neighbor[n++];
          if(nn==4) { /* sulfone */
            ai->chemFlag=true;
            ai->geom=cAtomInfoTetrahedral;
            ai->valence=4;
          } else if(nn==3) { /* suloxide */
            ai->chemFlag=true;
            ai->geom=cAtomInfoTetrahedral;
            ai->valence=3;
          } else if(nn==2) { /* thioether */
            ai->chemFlag=true;
            ai->geom=cAtomInfoTetrahedral;
            ai->valence=2;
          }
          break;
        case cAN_Cl:
          ai->chemFlag=1;
          if(ai->formalCharge==0.0) {
            ai->geom=cAtomInfoSingle;
            ai->valence=1;
          } else {
            ai->geom=cAtomInfoNone;
            ai->valence=0;
          }
          break;
        }
        if(ai->chemFlag)
          changedFlag=true;
      }
    }
  }
}

/*========================================================================*/
void ObjectMoleculeInferChemFromBonds(ObjectMolecule *I,int state)
{

  int a,b;
  BondType *b0;
  AtomInfoType *ai,*ai0,*ai1=NULL;
  int a0,a1;
  int expect,order;
  int n,nn;
  int changedFlag;
  /* initialize accumulators on uncategorized atoms */

  ObjectMoleculeUpdateNeighbors(I);
  ai=I->AtomInfo;
  for(a=0;a<I->NAtom;a++) {
    if(!ai->chemFlag) {
      ai->geom=0;
      ai->valence=0;
    }
    ai++;
  }
  
  /* find maximum bond order for each atom */

  b0=I->Bond;
  for(b=0;b<I->NBond;b++) {
    a0 = b0->index[0];
    a1 = b0->index[1];
    ai0=I->AtomInfo + a0;
    ai1=I->AtomInfo + a1;
    order = b0->order;
    b0++;
    if(!ai0->chemFlag) {
      if(order>ai0->geom)
        ai0->geom=order;
      ai0->valence+=order;
    }
    if(!ai1->chemFlag) {
      if(order>ai1->geom)
        ai1->geom=order;
      ai1->valence+=order;
    }
    if(order==3) { 
      /* override existing chemistry * this is a temp fix to a pressing problem...
         we need to rethink the chemisty assignment ordering (should bond
         information come first? */
      ai0->geom = cAtomInfoLinear;
      ai1->geom = cAtomInfoLinear;
      switch(ai0->protons) {
      case cAN_C:
        ai0->valence=2;
        break;
      default:
        ai0->valence=1;
      }
      switch(ai1->protons) {
      case cAN_C:
        ai1->valence=2;
        break;
      default:
        ai1->valence=1;
      }
      ai0->chemFlag=true;
      ai1->chemFlag=true;
    }
  }

  /* now set up valences and geometries */

  ai=I->AtomInfo;
  for(a=0;a<I->NAtom;a++) {
    if(!ai->chemFlag) {
      expect = AtomInfoGetExpectedValence(ai);
      n = I->Neighbor[a];
      nn = I->Neighbor[n++];
      if(ai->geom==3) {
        ai->geom = cAtomInfoLinear;
        switch(ai->protons) {
        case cAN_C:
          ai->valence=2;
          break;
        default:
          ai->valence=1;
        }
        ai->chemFlag=true;
      } else {
      if(expect<0) 
        expect = -expect; /* for now, just ignore this issue */
      /*      printf("%d %d %d %d\n",ai->geom,ai->valence,nn,expect);*/
      if(ai->valence==expect) { /* sum of bond orders equals valence */
        ai->chemFlag=true;
        ai->valence=nn;
        switch(ai->geom) { /* max bond order observed */
        case 0: ai->geom = cAtomInfoNone; break;
        case 2: ai->geom = cAtomInfoPlaner; break;
        case 3: ai->geom = cAtomInfoLinear; break;
        default: 
          if(expect==1) 
            ai->geom = cAtomInfoSingle;
          else
            ai->geom = cAtomInfoTetrahedral; 
          break;            
        }
      } else if(ai->valence<expect) { /* missing a bond */
        ai->chemFlag=true;
        ai->valence=nn+(expect-ai->valence); 
        switch(ai->geom) { 
        case 2: ai->geom = cAtomInfoPlaner; break;
        case 3: ai->geom = cAtomInfoLinear; break;
        default: 
          if(expect==1) 
            ai->geom = cAtomInfoSingle;
          else
            ai->geom = cAtomInfoTetrahedral; 
          break;
        }
      } else if(ai->valence>expect) {
        ai->chemFlag=true;
        ai->valence=nn;
        switch(ai->geom) { 
        case 2: ai->geom = cAtomInfoPlaner; break;
        case 3: ai->geom = cAtomInfoLinear; break;
        default: 
          if(expect==1) 
            ai->geom = cAtomInfoSingle;
          else
            ai->geom = cAtomInfoTetrahedral; 
          break;
        }
        if(nn>3)
          ai->geom = cAtomInfoTetrahedral;
      }
      }
    }
    ai++;
  }

  /* now go through and make sure conjugated amines are planer */
  changedFlag=true;
  while(changedFlag) {
    changedFlag=false;
    ai=I->AtomInfo;
    for(a=0;a<I->NAtom;a++) {
      if(ai->chemFlag) {
        if(ai->protons==cAN_N) 
          if(ai->formalCharge==0) 
            if(ai->geom==cAtomInfoTetrahedral) { 
              /* search for uncharged tetrahedral nitrogen */
              n = I->Neighbor[a]+1;
              while(1) {
                a0 = I->Neighbor[n];
                n+=2;
                if(a0<0) break;
                ai0 = I->AtomInfo+a0;
                if((ai0->chemFlag)&&(ai0->geom==cAtomInfoPlaner)&&
                   ((ai0->protons==cAN_C)||(ai0->protons==cAN_N))) {
                  ai->geom=cAtomInfoPlaner; /* found probable delocalization */
                  ai->valence=3; /* just in case...*/
                  changedFlag=true;
                  break;
                }
              }
            }
      }
      ai++; 
    }
  }

  /* now go through and make sure conjugated anions are planer */
  changedFlag=true;
  while(changedFlag) {
    changedFlag=false;
    ai=I->AtomInfo;
    for(a=0;a<I->NAtom;a++) {
      if(ai->chemFlag) {
        if(ai->protons==cAN_O) 
          if(ai->formalCharge==-1) 
            if((ai->geom==cAtomInfoTetrahedral)||
               (ai->geom==cAtomInfoSingle)) { /* search for anionic tetrahedral oxygen */
              n = I->Neighbor[a]+1;
              while(1) {
                a0 = I->Neighbor[n];
                n+=2;
                if(a0<0) break;
                ai0 = I->AtomInfo+a0;
                if((ai0->chemFlag)&&(ai0->geom==cAtomInfoPlaner)&&
                   ((ai0->protons==cAN_C)||(ai0->protons==cAN_N))) {
                  ai->geom=cAtomInfoPlaner; /* found probable delocalization */
                  changedFlag=true;
                  break;
                }
              }
            }
      }
      ai++;
    }
  }


}
/*========================================================================*/
int ObjectMoleculeTransformSelection(ObjectMolecule *I,int state,
                                      int sele,float *TTT,int log,char *sname) 
{
  /* if sele == -1, then the whole object state is transformed */

  int a,s;
  int flag=false;
  CoordSet *cs;
  AtomInfoType *ai;
  int logging;
  int ok=true;
  if(state<0) state=0;
  if(I->NCSet==1) state=0;
  state = state % I->NCSet;
  cs = I->CSet[state];
  if(cs) {
    if(sele>=0) {
      ai=I->AtomInfo;
      for(a=0;a<I->NAtom;a++) {
        s=ai->selEntry;
        if(!(ai->protected==1))
          if(SelectorIsMember(s,sele))
            {
              CoordSetTransformAtom(cs,a,TTT);
              flag=true;
            }
        ai++;
      }
    } else {
      ai=I->AtomInfo;
      for(a=0;a<I->NAtom;a++) {
        if(!(ai->protected==1))
          CoordSetTransformAtom(cs,a,TTT);
        ai++;
      }
      flag=true;
    }
    if(flag) 
      cs->fInvalidateRep(cs,cRepAll,cRepInvCoord);
  }
  if(log) {
    OrthoLineType line;
    WordType sele_str = ",'";
    logging = SettingGet(cSetting_logging);
    if(sele>=0) {
      strcat(sele_str,sname);
      strcat(sele_str,"'");
    }
    else
      sele_str[0]=0;
    switch(logging) {
    case cPLog_pml:
      sprintf(line,
              "_ cmd.transform_object('%s',[\\\n_ %15.9f,%15.9f,%15.9f,%15.9f,\\\n_ %15.9f,%15.9f,%15.9f,%15.9f,\\\n_ %15.9f,%15.9f,%15.9f,%15.9f,\\\n_ %15.9f,%15.9f,%15.9f,%15.9f\\\n_     ],%d,%d%s)\n",
              I->Obj.Name,
              TTT[ 0],TTT[ 1],TTT[ 2],TTT[ 3],
              TTT[ 4],TTT[ 5],TTT[ 6],TTT[ 7],
              TTT[ 8],TTT[ 9],TTT[10],TTT[11],
              TTT[12],TTT[13],TTT[14],TTT[15],state+1,log,sele_str);
      PLog(line,cPLog_no_flush);
      break;
    case cPLog_pym:
      
      sprintf(line,
              "cmd.transform_object('%s',[\n%15.9f,%15.9f,%15.9f,%15.9f,\n%15.9f,%15.9f,%15.9f,%15.9f,\n%15.9f,%15.9f,%15.9f,%15.9f,\n%15.9f,%15.9f,%15.9f,%15.9f\n],%d,%d%s)\n",
              I->Obj.Name,
              TTT[ 0],TTT[ 1],TTT[ 2],TTT[ 3],
              TTT[ 4],TTT[ 5],TTT[ 6],TTT[ 7],
              TTT[ 8],TTT[ 9],TTT[10],TTT[11],
              TTT[12],TTT[13],TTT[14],TTT[15],state+1,log,sele_str);
      PLog(line,cPLog_no_flush);
      break;
    default:
      break;
    }
  }
  return(ok);
}
/*========================================================================*/
int ObjectMoleculeGetAtomIndex(ObjectMolecule *I,int sele)
{
  int a,s;
  for(a=0;a<I->NAtom;a++) {
    s=I->AtomInfo[a].selEntry;
    if(SelectorIsMember(s,sele))
      return(a);
  }
  return(-1);
}
/*========================================================================*/
void ObjectMoleculeUpdateNonbonded(ObjectMolecule *I)
{
  int a;
  BondType *b;
  AtomInfoType *ai;

  if(!I->DiscreteFlag) {
    ai=I->AtomInfo;
    
    for(a=0;a<I->NAtom;a++)
      (ai++)->bonded = false;
    
    b=I->Bond;
    ai=I->AtomInfo;
    for(a=0;a<I->NBond;a++)
      {
        ai[b->index[0]].bonded=true;
        ai[b->index[1]].bonded=true;
        b++;
      }

  }
}
/*========================================================================*/
void ObjectMoleculeUpdateNeighbors(ObjectMolecule *I)
{
  /* neighbor storage structure: VERY COMPLICATED...
     
     0       list offset for atom 0 = n
     1       list offset for atom 1 = n + m + 1
     ...
     n-1     list offset for atom n-1

     n       count for atom 0 
     n+1     neighbor of atom 0
     n+2     bond index
     n+3     neighbor of atom 0
     n+4     bond index
     ...
     n+m     -1 terminator for atom 0

     n+m+1   count for atom 1
     n+m+2   neighbor of atom 1
     n+m+3   bond index
     n+m+4   neighbor of atom 1
     n+m+5   bond index
     etc.

     NOTE: all atoms have an offset and a terminator whether or not they have any bonds 
 */

  int size;
  int a,b,c,d,l0,l1,*l;
  BondType *bnd;
  if(!I->Neighbor) {
    size = (I->NAtom*3)+(I->NBond*4); 
    if(I->Neighbor) {
      VLACheck(I->Neighbor,int,size);
    } else {
      I->Neighbor=VLAlloc(int,size);
    }
    
    /* initialize */
    l = I->Neighbor;
    for(a=0;a<I->NAtom;a++)
      (*l++)=0;
    
    /* count neighbors for each atom */
    bnd = I->Bond;
    for(b=0;b<I->NBond;b++) {
      I->Neighbor[bnd->index[0]]++;
      I->Neighbor[bnd->index[1]]++;
      bnd++;
    }
    
    /* set up offsets and list terminators */
    c = I-> NAtom;
    for(a=0;a<I->NAtom;a++) {
      d = I->Neighbor[a];
      I->Neighbor[c]=d; /* store neighbor count */
      I->Neighbor[a]=c+d+d+1; /* set initial position to end of list, we'll fill backwards */
      I->Neighbor[I->Neighbor[a]]=-1; /* store terminator */
      c += d + d + 2;
    }
    
    /* now load neighbors in a sequential list for each atom (reverse order) */
    bnd = I->Bond;
    for(b=0;b<I->NBond;b++) {
      l0 = bnd->index[0];
      l1 = bnd->index[1];
      bnd++;

      I->Neighbor[l0]--; 
      I->Neighbor[I->Neighbor[l0]]=b; /* store bond indices (for I->Bond) */
      I->Neighbor[l1]--;
      I->Neighbor[I->Neighbor[l1]]=b;

      I->Neighbor[l0]--;
      I->Neighbor[I->Neighbor[l0]]=l1; /* store neighbor references (I->AtomInfo, etc.)*/
      I->Neighbor[l1]--;
      I->Neighbor[I->Neighbor[l1]]=l0;
    }
    for(a=0;a<I->NAtom;a++) { /* adjust down to point to the count, not the first entry */
      if(I->Neighbor[a]>=0)
        I->Neighbor[a]--;
    }
    l=I->Neighbor;
  }
}
/*========================================================================*/
CoordSet *ObjectMoleculeChemPyModel2CoordSet(PyObject *model,AtomInfoType **atInfoPtr)
{
  int nAtom,nBond;
  int a,c;
  float *coord = NULL;
  CoordSet *cset = NULL;
  AtomInfoType *atInfo = NULL,*ai;
  float *f;
  BondType *ii,*bond=NULL;
  int ok=true;
  int auto_show_lines;
  int auto_show_nonbonded;
  int hetatm;
  int ignore_ids;

  PyObject *atomList = NULL;
  PyObject *bondList = NULL;
  PyObject *atom = NULL;
  PyObject *bnd = NULL;
  PyObject *index = NULL;
  PyObject *crd = NULL;
  PyObject *tmp = NULL;
  auto_show_lines = SettingGet(cSetting_auto_show_lines);
  auto_show_nonbonded = SettingGet(cSetting_auto_show_nonbonded);
  AtomInfoPrimeColors();

  ignore_ids=!(int)SettingGet(cSetting_preserve_chempy_ids);

  nAtom=0;
  nBond=0;
  if(atInfoPtr)
	 atInfo = *atInfoPtr;

  atomList = PyObject_GetAttrString(model,"atom");
  if(atomList) 
    nAtom = PyList_Size(atomList);
  else 
    ok=ErrMessage("ObjectMoleculeChemPyModel2CoordSet","can't get atom list");


  if(ok) {
	 coord=VLAlloc(float,3*nAtom);
	 if(atInfo)
		VLACheck(atInfo,AtomInfoType,nAtom);	 
  }

  if(ok) { 
    
	 f=coord;
	 for(a=0;a<nAtom;a++)
		{
        atom = PyList_GetItem(atomList,a);
        if(!atom) 
          ok=ErrMessage("ObjectMoleculeChemPyModel2CoordSet","can't get atom");
        crd = PyObject_GetAttrString(atom,"coord");
        if(!crd) 
          ok=ErrMessage("ObjectMoleculeChemPyModel2CoordSet","can't get coordinates");
        else {
          for(c=0;c<3;c++) {
            tmp = PyList_GetItem(crd,c);
            if (tmp) 
              ok = PConvPyObjectToFloat(tmp,f++);
            if(!ok) {
              ErrMessage("ObjectMoleculeChemPyModel2CoordSet","can't read coordinates");
              break;
            }
          }
        }
        Py_XDECREF(crd);
        
        ai = atInfo+a;
        ai->id = a; /* chempy models are zero-based */
        if(!ignore_ids) { 
          if(ok) { /* get chempy atom id if extant */
            if(PTruthCallStr(atom,"has","id")) { 
              tmp = PyObject_GetAttrString(atom,"id");
              if (tmp)
                ok = PConvPyObjectToInt(tmp,&ai->id);
              if(!ok) 
                ErrMessage("ObjectMoleculeChemPyModel2CoordSet","can't read atom identifier");
              Py_XDECREF(tmp);
            } else {
              ai->id=-1;
            }
          }
        }

        if(ok) {
          tmp = PyObject_GetAttrString(atom,"name");
          if (tmp)
            ok = PConvPyObjectToStrMaxClean(tmp,ai->name,sizeof(AtomName)-1);
          if(!ok) 
            ErrMessage("ObjectMoleculeChemPyModel2CoordSet","can't read name");
          Py_XDECREF(tmp);
        }

        if(ok) {
          if(PTruthCallStr(atom,"has","text_type")) { 
            tmp = PyObject_GetAttrString(atom,"text_type");
            if (tmp)
              ok = PConvPyObjectToStrMaxClean(tmp,ai->textType,sizeof(TextType)-1);
            if(!ok) 
              ErrMessage("ObjectMoleculeChemPyModel2CoordSet","can't read text_type");
            Py_XDECREF(tmp);
          } else {
            ai->textType[0]=0;
          }
        }

        if(ok) {
          if(PTruthCallStr(atom,"has","vdw")) { 
            tmp = PyObject_GetAttrString(atom,"vdw");
            if (tmp)
              ok = PConvPyObjectToFloat(tmp,&ai->vdw);
            if(!ok) 
              ErrMessage("ObjectMoleculeChemPyModel2CoordSet","can't read vdw radius");
            Py_XDECREF(tmp);
          } else {
            ai->vdw=0.0;
          }
        }

        if(ok) {
          if(PTruthCallStr(atom,"has","stereo")) { 
            tmp = PyObject_GetAttrString(atom,"stereo");
            if (tmp)
              ok = PConvPyObjectToInt(tmp,&ai->stereo);
            if(!ok) 
              ErrMessage("ObjectMoleculeChemPyModel2CoordSet","can't read stereo");
            Py_XDECREF(tmp);
          } else {
            ai->stereo = 0;
          }
        }

        if(ok) {
          if(PTruthCallStr(atom,"has","numeric_type")) { 
            tmp = PyObject_GetAttrString(atom,"numeric_type");
            if (tmp)
              ok = PConvPyObjectToInt(tmp,&ai->customType);
            if(!ok) 
              ErrMessage("ObjectMoleculeChemPyModel2CoordSet","can't read numeric_type");
            Py_XDECREF(tmp);
          } else {
            ai->customType = cAtomInfoNoType;
          }
        }

        if(ok) {
          if(PTruthCallStr(atom,"has","formal_charge")) { 
            tmp = PyObject_GetAttrString(atom,"formal_charge");
            if (tmp)
              ok = PConvPyObjectToInt(tmp,&ai->formalCharge);
            if(!ok) 
              ErrMessage("ObjectMoleculeChemPyModel2CoordSet","can't read formal_charge");
            Py_XDECREF(tmp);
          } else {
            ai->formalCharge = 0;
          }
        }

        if(ok) {
          if(PTruthCallStr(atom,"has","partial_charge")) { 
            tmp = PyObject_GetAttrString(atom,"partial_charge");
            if (tmp)
              ok = PConvPyObjectToFloat(tmp,&ai->partialCharge);
            if(!ok) 
              ErrMessage("ObjectMoleculeChemPyModel2CoordSet","can't read partial_charge");
            Py_XDECREF(tmp);
          } else {
            ai->partialCharge = 0.0;
          }
        }

        if(ok) {
          if(PTruthCallStr(atom,"has","flags")) {         
            tmp = PyObject_GetAttrString(atom,"flags");
            if (tmp)
              ok = PConvPyObjectToInt(tmp,(int*)&ai->flags);
            if(!ok) 
              ErrMessage("ObjectMoleculeChemPyModel2CoordSet","can't read flags");
            Py_XDECREF(tmp);
          } else {
            ai->flags = 0;
          }
        }

        if(ok) {
          tmp = PyObject_GetAttrString(atom,"resn");
          if (tmp)
            ok = PConvPyObjectToStrMaxClean(tmp,ai->resn,sizeof(ResName)-1);
          if(!ok) 
            ErrMessage("ObjectMoleculeChemPyModel2CoordSet","can't read resn");
          Py_XDECREF(tmp);
        }
        
		  if(ok) {
          tmp = PyObject_GetAttrString(atom,"resi");
          if (tmp)
            ok = PConvPyObjectToStrMaxClean(tmp,ai->resi,sizeof(ResIdent)-1);
          if(!ok) 
            ErrMessage("ObjectMoleculeChemPyModel2CoordSet","can't read resi");
          else
            sscanf(ai->resi,"%d",&ai->resv);
          Py_XDECREF(tmp);
        }

        if(ok) {
          if(PTruthCallStr(atom,"has","resi_number")) {         
            tmp = PyObject_GetAttrString(atom,"resi_number");
            if (tmp)
              ok = PConvPyObjectToInt(tmp,&ai->resv);
            if(!ok) 
              ErrMessage("ObjectMoleculeChemPyModel2CoordSet","can't read resi_number");
            Py_XDECREF(tmp);
          }
        }
        
		  if(ok) {
          tmp = PyObject_GetAttrString(atom,"segi");
          if (tmp)
            ok = PConvPyObjectToStrMaxClean(tmp,ai->segi,sizeof(SegIdent)-1);
          if(!ok) 
            ErrMessage("ObjectMoleculeChemPyModel2CoordSet","can't read segi");
          Py_XDECREF(tmp);
        }

		  if(ok) {
          tmp = PyObject_GetAttrString(atom,"b");
          if (tmp)
            ok = PConvPyObjectToFloat(tmp,&ai->b);
          if(!ok) 
            ErrMessage("ObjectMoleculeChemPyModel2CoordSet","can't read b value");
          Py_XDECREF(tmp);
        }

		  if(ok) {
          tmp = PyObject_GetAttrString(atom,"q");
          if (tmp)
            ok = PConvPyObjectToFloat(tmp,&ai->q);
          if(!ok) 
            ErrMessage("ObjectMoleculeChemPyModel2CoordSet","can't read occupancy");
          Py_XDECREF(tmp);
        }

        
		  if(ok) {
          tmp = PyObject_GetAttrString(atom,"chain");
          if (tmp)
            ok = PConvPyObjectToStrMaxClean(tmp,ai->chain,sizeof(Chain)-1);
          if(!ok) 
            ErrMessage("ObjectMoleculeChemPyModel2CoordSet","can't read chain");
          Py_XDECREF(tmp);
        }
        
		  if(ok) {
          tmp = PyObject_GetAttrString(atom,"hetatm");
          if (tmp)
            ok = PConvPyObjectToInt(tmp,&hetatm);
          if(!ok) 
            ErrMessage("ObjectMoleculeChemPyModel2CoordSet","can't read hetatm");
          else
            ai->hetatm = hetatm;
          Py_XDECREF(tmp);
        }
        
		  if(ok) {
          tmp = PyObject_GetAttrString(atom,"alt");
          if (tmp)
            ok = PConvPyObjectToStrMaxClean(tmp,ai->alt,sizeof(Chain)-1);
          if(!ok) 
            ErrMessage("ObjectMoleculeChemPyModel2CoordSet","can't read alternate conformation");
          Py_XDECREF(tmp);
        }

		  if(ok) {
          tmp = PyObject_GetAttrString(atom,"symbol");
          if (tmp)
            ok = PConvPyObjectToStrMaxClean(tmp,ai->elem,sizeof(AtomName)-1);
          if(!ok) 
            ErrMessage("ObjectMoleculeChemPyModel2CoordSet","can't read symbol");
          Py_XDECREF(tmp);
        }

        
        for(c=0;c<cRepCnt;c++) {
          atInfo[a].visRep[c] = false;
		  }
        atInfo[a].visRep[cRepLine] = auto_show_lines; /* show lines by default */
        atInfo[a].visRep[cRepNonbonded] = auto_show_nonbonded; /* show lines by default */

		  if(ok&&atInfo) {
			 AtomInfoAssignParameters(ai);
			 atInfo[a].color=AtomInfoGetColor(ai);
		  }


		  if(!ok)
			 break;
		}
  }

  bondList = PyObject_GetAttrString(model,"bond");
  if(bondList) 
    nBond = PyList_Size(bondList);
  else
    ok=ErrMessage("ObjectMoleculeChemPyModel2CoordSet","can't get bond list");

  if(ok) {
	 bond=VLAlloc(BondType,nBond);
    ii=bond;
	 for(a=0;a<nBond;a++)
		{
        bnd = PyList_GetItem(bondList,a);
        if(!bnd) 
          ok=ErrMessage("ObjectMoleculeChemPyModel2CoordSet","can't get bond");
        index = PyObject_GetAttrString(bnd,"index");
        if(!index) 
          ok=ErrMessage("ObjectMoleculeChemPyModel2CoordSet","can't get bond indices");
        else {
          for(c=0;c<2;c++) {
            tmp = PyList_GetItem(index,c);
            if (tmp) 
              ok = PConvPyObjectToInt(tmp,&ii->index[c]);
            if(!ok) {
              ErrMessage("ObjectMoleculeChemPyModel2CoordSet","can't read coordinates");
              break;
            }
          }
        }
        if(ok) {
          tmp = PyObject_GetAttrString(bnd,"order");
          if (tmp)
            ok = PConvPyObjectToInt(tmp,&ii->order);
          if(!ok) 
            ErrMessage("ObjectMoleculeChemPyModel2CoordSet","can't read bond order");
          Py_XDECREF(tmp);
        }

        if(ok) {
          tmp = PyObject_GetAttrString(bnd,"stereo");
          if (tmp)
            ok = PConvPyObjectToInt(tmp,&ii->stereo);
          else 
            ii->stereo = 0;
          if(!ok) 
            ii->stereo = 0;
          Py_XDECREF(tmp);
        }

        ii->id=a;
        if(!ignore_ids) { 
          if(ok) { /* get unique chempy bond id if present */
            if(PTruthCallStr(bnd,"has","id")) { 
              tmp = PyObject_GetAttrString(bnd,"id");
              if (tmp)
                ok = PConvPyObjectToInt(tmp,&ii->id);
              if(!ok) 
                ErrMessage("ObjectMoleculeChemPyModel2CoordSet","can't read bond identifier");
              Py_XDECREF(tmp);
            } else {
              ii->id=-1;
            }
          }
        }
        Py_XDECREF(index);
        ii++;
      }
  }

  Py_XDECREF(atomList);
  Py_XDECREF(bondList);

  if(ok) {
	 cset = CoordSetNew();
	 cset->NIndex=nAtom;
	 cset->Coord=coord;
	 cset->NTmpBond=nBond;
	 cset->TmpBond=bond;
  } else {
	 VLAFreeP(bond);
	 VLAFreeP(coord);
  }
  if(atInfoPtr)
	 *atInfoPtr = atInfo;

  if(PyErr_Occurred())
    PyErr_Print();
  return(cset);
}


/*========================================================================*/
ObjectMolecule *ObjectMoleculeLoadChemPyModel(ObjectMolecule *I,PyObject *model,int frame,int discrete)
{
  CoordSet *cset = NULL;
  AtomInfoType *atInfo;
  int ok=true;
  int isNew = true;
  unsigned int nAtom = 0;
  PyObject *tmp,*mol;

  if(!I) 
	 isNew=true;
  else 
	 isNew=false;

  if(ok) {

	 if(isNew) {
		I=(ObjectMolecule*)ObjectMoleculeNew(discrete);
		atInfo = I->AtomInfo;
		isNew = true;
	 } else {
		atInfo=VLAMalloc(10,sizeof(AtomInfoType),2,true); /* autozero here is important */
		isNew = false;
	 }
	 cset=ObjectMoleculeChemPyModel2CoordSet(model,&atInfo);	 

    mol = PyObject_GetAttrString(model,"molecule");
    if(mol) {
      if(PyObject_HasAttrString(mol,"title")) {
        tmp = PyObject_GetAttrString(mol,"title");
        if(tmp) {
          UtilNCopy(cset->Name,PyString_AsString(tmp),sizeof(WordType));
          Py_DECREF(tmp);
          if(!strcmp(cset->Name,"untitled")) /* ignore untitled */
            cset->Name[0]=0;
        }
      }
      Py_DECREF(mol);
    }
    if(PyObject_HasAttrString(model,"spheroid")&&
       PyObject_HasAttrString(model,"spheroid_normals"))
      {
        tmp = PyObject_GetAttrString(model,"spheroid");
        if(tmp) {
          cset->NSpheroid = PConvPyListToFloatArray(tmp,&cset->Spheroid);
          Py_DECREF(tmp);
        }
        tmp = PyObject_GetAttrString(model,"spheroid_normals");
        if(tmp) {
          PConvPyListToFloatArray(tmp,&cset->SpheroidNormal);
          Py_DECREF(tmp);
        }
      }
    mol = PyObject_GetAttrString(model,"molecule");
    
	 nAtom=cset->NIndex;
  }
  /* include coordinate set */
  if(ok) {
    cset->Obj = I;
    cset->fEnumIndices(cset);
    if(cset->fInvalidateRep)
      cset->fInvalidateRep(cset,cRepAll,cRepInvRep);
    if(isNew) {	
      I->AtomInfo=atInfo; /* IMPORTANT to reassign: this VLA may have moved! */
    } else {
      ObjectMoleculeMerge(I,atInfo,cset,false,cAIC_AllMask); /* NOTE: will release atInfo */
    }
    if(isNew) I->NAtom=nAtom;
    if(frame<0) frame=I->NCSet;
    VLACheck(I->CSet,CoordSet*,frame);
    if(I->NCSet<=frame) I->NCSet=frame+1;
    if(I->CSet[frame]) I->CSet[frame]->fFree(I->CSet[frame]);
    I->CSet[frame] = cset;
    if(isNew) I->NBond = ObjectMoleculeConnect(I,&I->Bond,I->AtomInfo,cset,false);
    if(cset->Symmetry&&(!I->Symmetry)) {
      I->Symmetry=SymmetryCopy(cset->Symmetry);
      SymmetryAttemptGeneration(I->Symmetry);
    }
    SceneCountFrames();
    ObjectMoleculeExtendIndices(I);
    ObjectMoleculeSort(I);
    ObjectMoleculeUpdateIDNumbers(I);
    ObjectMoleculeUpdateNonbonded(I);
  }
  return(I);
}


/*========================================================================*/
ObjectMolecule *ObjectMoleculeLoadCoords(ObjectMolecule *I,PyObject *coords,int frame)
{
  CoordSet *cset = NULL;
  int ok=true;
  int a,l;
  PyObject *v;
  float *f;
  a=0;
  while(a<I->NCSet) {
    if(I->CSet[a]) {
      cset=I->CSet[a];
      break;
    }
    a++;
  }
  
  if(!PyList_Check(coords)) 
    ErrMessage("LoadsCoords","passed argument is not a list");
  else {
    l = PyList_Size(coords);
    if (l==cset->NIndex) {
      cset=CoordSetCopy(cset);
      f=cset->Coord;
      for(a=0;a<l;a++) {
        v=PyList_GetItem(coords,a);
/* no error checking */
        *(f++)=PyFloat_AsDouble(PyList_GetItem(v,0)); 
        *(f++)=PyFloat_AsDouble(PyList_GetItem(v,1));
        *(f++)=PyFloat_AsDouble(PyList_GetItem(v,2));
      }
    }
  }
  /* include coordinate set */
  if(ok) {
    if(cset->fInvalidateRep)
      cset->fInvalidateRep(cset,cRepAll,cRepInvRep);

    if(frame<0) frame=I->NCSet;
    VLACheck(I->CSet,CoordSet*,frame);
    if(I->NCSet<=frame) I->NCSet=frame+1;
    if(I->CSet[frame]) I->CSet[frame]->fFree(I->CSet[frame]);
    I->CSet[frame] = cset;
    SceneCountFrames();
  }
  return(I);
}

/*========================================================================*/
static int BondInOrder(BondType *a,int b1,int b2)
{
  return(BondCompare(a+b1,a+b2)<=0);
}
/*========================================================================*/
static int BondCompare(BondType *a,BondType *b)
{
  int result;
  if(a->index[0]==b->index[0]) {
	if(a->index[1]==b->index[1]) {
	  result=0;
	} else if(a->index[1]>b->index[1]) {
	  result=1;
	} else {
	  result=-1;
	}
  } else if(a->index[0]>b->index[0]) {
	result=1;
  } else {
	result=-1;
  }
  return(result);
}
/*========================================================================*/
void ObjectMoleculeBlindSymMovie(ObjectMolecule *I)
{
  CoordSet *frac;
  int a,c;
  int x,y,z;
  float m[16];

  if(I->NCSet!=1) {
    ErrMessage("ObjectMolecule:","SymMovie only works on objects with a single state.");
  } else if(!I->Symmetry) {
    ErrMessage("ObjectMolecule:","No symmetry loaded!");
  } else if(!I->Symmetry->NSymMat) {
    ErrMessage("ObjectMolecule:","No symmetry matrices!");    
  } else if(I->CSet[0]) {
    frac = CoordSetCopy(I->CSet[0]);
    CoordSetRealToFrac(frac,I->Symmetry->Crystal);
    for(x=-1;x<2;x++)
      for(y=-1;y<2;y++)
        for(z=-1;z<2;z++)
          for(a=0;a<I->Symmetry->NSymMat;a++) {
            if(!((!a)&&(!x)&&(!y)&&(!z))) {
              c = I->NCSet;
              VLACheck(I->CSet,CoordSet*,c);
              I->CSet[c] = CoordSetCopy(frac);
              CoordSetTransform44f(I->CSet[c],I->Symmetry->SymMatVLA+(a*16));
              identity44f(m);
              m[3] = x;
              m[7] = y;
              m[11] = z;
              CoordSetTransform44f(I->CSet[c],m);
              CoordSetFracToReal(I->CSet[c],I->Symmetry->Crystal);
              I->NCSet++;
            }
          }
    frac->fFree(frac);
  }
  SceneChanged();
}

/*========================================================================*/
void ObjectMoleculeExtendIndices(ObjectMolecule *I)
{
  int a;
  CoordSet *cs;

  for(a=-1;a<I->NCSet;a++) {
    if(a<0) 
      cs=I->CSTmpl;
    else
      cs=I->CSet[a];
	 if(cs)
      if(cs->fExtendIndices)
        cs->fExtendIndices(cs,I->NAtom);
  }
}
/*========================================================================*/
void ObjectMoleculeSort(ObjectMolecule *I) /* sorts atoms and bonds */
{
  int *index,*outdex;
  int a,b;
  CoordSet *cs,**dcs;
  AtomInfoType *atInfo;
  int *dAtmToIdx;

  if(!I->DiscreteFlag) {

    index=AtomInfoGetSortedIndex(I->AtomInfo,I->NAtom,&outdex);
    for(a=0;a<I->NBond;a++) { /* bonds */
      I->Bond[a].index[0]=outdex[I->Bond[a].index[0]];
      I->Bond[a].index[1]=outdex[I->Bond[a].index[1]];
    }
    
    for(a=-1;a<I->NCSet;a++) { /* coordinate set mapping */
      if(a<0) {
        cs=I->CSTmpl;
      } else {
        cs=I->CSet[a];
      }
      
      if(cs) {
        for(b=0;b<cs->NIndex;b++)
          cs->IdxToAtm[b]=outdex[cs->IdxToAtm[b]];
        if(cs->AtmToIdx) {
          for(b=0;b<I->NAtom;b++)
            cs->AtmToIdx[b]=-1;
          for(b=0;b<cs->NIndex;b++) {
            cs->AtmToIdx[cs->IdxToAtm[b]]=b;
          }
        }
      }
    }
    
    atInfo=(AtomInfoType*)VLAMalloc(I->NAtom,sizeof(AtomInfoType),5,true);
    /* autozero here is important */
    for(a=0;a<I->NAtom;a++)
      atInfo[a]=I->AtomInfo[index[a]];
    VLAFreeP(I->AtomInfo);
    I->AtomInfo=atInfo;
    
    if(I->DiscreteFlag) {
      dcs = VLAlloc(CoordSet*,I->NAtom);
      dAtmToIdx = VLAlloc(int,I->NAtom);
      for(a=0;a<I->NAtom;a++) {
        b=index[a];
        dcs[a] = I->DiscreteCSet[b];
        dAtmToIdx[a] = I->DiscreteAtmToIdx[b];
      }
      VLAFreeP(I->DiscreteCSet);
      VLAFreeP(I->DiscreteAtmToIdx);
      I->DiscreteCSet = dcs;
      I->DiscreteAtmToIdx = dAtmToIdx;
    }
    AtomInfoFreeSortedIndexes(index,outdex);

    UtilSortInPlace(I->Bond,I->NBond,sizeof(BondType),(UtilOrderFn*)BondInOrder);
    /* sort...important! */
    ObjectMoleculeInvalidate(I,cRepAll,cRepInvAtoms); /* important */

  }
}
/*========================================================================*/

CoordSet *ObjectMoleculeMOLStr2CoordSet(char *buffer,AtomInfoType **atInfoPtr)
{
  char *p;
  int nAtom,nBond;
  int a,c,cnt,atm,chg;
  float *coord = NULL;
  CoordSet *cset = NULL;
  AtomInfoType *atInfo = NULL;
  char cc[MAXLINELEN],cc1[MAXLINELEN],resn[MAXLINELEN] = "UNK";
  float *f;
  BondType *ii;
  BondType *bond=NULL;
  int ok=true;
  int auto_show_lines;
  int auto_show_nonbonded;
  WordType nameTmp;

  auto_show_lines = SettingGet(cSetting_auto_show_lines);
  auto_show_nonbonded = SettingGet(cSetting_auto_show_nonbonded);
  AtomInfoPrimeColors();

  p=buffer;
  nAtom=0;
  if(atInfoPtr)
	 atInfo = *atInfoPtr;

  p=ParseWordCopy(nameTmp,p,sizeof(WordType)-1);
  p=nextline(p); 
  p=nextline(p);
  p=nextline(p);

  if(ok) {
	 p=ncopy(cc,p,3);
	 if(sscanf(cc,"%d",&nAtom)!=1)
		ok=ErrMessage("ReadMOLFile","bad atom count");
  }

  if(ok) {  
	 p=ncopy(cc,p,3);
	 if(sscanf(cc,"%d",&nBond)!=1)
		ok=ErrMessage("ReadMOLFile","bad bond count");
  }

  if(ok) {
	 coord=VLAlloc(float,3*nAtom);
	 if(atInfo)
		VLACheck(atInfo,AtomInfoType,nAtom);	 
  }
  
  p=nextline(p);

  /* read coordinates and atom names */

  if(ok) { 
	 f=coord;
	 for(a=0;a<nAtom;a++)
		{
		  if(ok) {
			 p=ncopy(cc,p,10);
			 if(sscanf(cc,"%f",f++)!=1)
				ok=ErrMessage("ReadMOLFile","bad coordinate");
		  }
		  if(ok) {
			 p=ncopy(cc,p,10);
			 if(sscanf(cc,"%f",f++)!=1)
				ok=ErrMessage("ReadMOLFile","bad coordinate");
		  }
		  if(ok) {
			 p=ncopy(cc,p,10);
			 if(sscanf(cc,"%f",f++)!=1)
				ok=ErrMessage("ReadMOLFile","bad coordinate");
		  }
		  if(ok) {
          p=nskip(p,1);
			 p=ncopy(atInfo[a].name,p,3);
			 UtilCleanStr(atInfo[a].name);
			 
          for(c=0;c<cRepCnt;c++) {
            atInfo[a].visRep[c] = false;
          }
          atInfo[a].visRep[cRepLine] = auto_show_lines; /* show lines by default */
          atInfo[a].visRep[cRepNonbonded] = auto_show_nonbonded; /* show lines by default */

		  }
        if(ok) {
          p=nskip(p,2);
          p=ncopy(cc,p,3);
          if(sscanf(cc,"%d",&atInfo[a].formalCharge)==1) {
            if(atInfo[a].formalCharge) {
              atInfo[a].formalCharge = 4-atInfo[a].formalCharge;
            }
          }
          p=ncopy(cc,p,3);
          if(sscanf(cc,"%d",&atInfo[a].stereo)!=1) 
            atInfo[a].stereo=0;
        }
		  if(ok&&atInfo) {
          atInfo[a].id = a+1;
			 strcpy(atInfo[a].resn,resn);
			 atInfo[a].hetatm=true;
			 AtomInfoAssignParameters(atInfo+a);
			 atInfo[a].color=AtomInfoGetColor(atInfo+a);
          atInfo[a].alt[0]=0;
          atInfo[a].segi[0]=0;
          atInfo[a].resi[0]=0;
		  }
		  p=nextline(p);
		  if(!ok)
			 break;
		}
  }
  if(ok) {
	 bond=VLAlloc(BondType,nBond);
	 ii=bond;
	 for(a=0;a<nBond;a++)
		{
		  if(ok) {
			 p=ncopy(cc,p,3);
			 if(sscanf(cc,"%d",&ii->index[0])!=1)
				ok=ErrMessage("ReadMOLFile","bad bond atom");
		  }
		  
		  if(ok) {  
			 p=ncopy(cc,p,3);
			 if(sscanf(cc,"%d",&ii->index[1])!=1)
				ok=ErrMessage("ReadMOLFile","bad bond atom");
		  }

		  if(ok) {  
			 p=ncopy(cc,p,3);
			 if(sscanf(cc,"%d",&ii->order)!=1)
				ok=ErrMessage("ReadMOLFile","bad bond order");
		  }
        if(ok) {
			 p=ncopy(cc,p,3);
			 if(sscanf(cc,"%d",&ii->stereo)!=1)
            ii->stereo=0;

        }
        ii++;
		  if(!ok)
			 break;
		  p=nextline(p);
		}
	 ii=bond;
	 for(a=0;a<nBond;a++) {
      ii->index[0]--;/* adjust bond indexs down one */
      ii->index[1]--;
      ii++;
	 }
  }
  while(*p) { /* read M  CHG records */
    p=ncopy(cc,p,6);
    if(!strcmp(cc,"M  CHG")) {
      p=ncopy(cc,p,3);
      if(sscanf(cc,"%d",&cnt)==1) {
        while(cnt--) {
          p=ncopy(cc,p,4);
          p=ncopy(cc1,p,4);
          if(!((*cc)||(*cc1))) break;
          if((sscanf(cc,"%d",&atm)==1)&&
             (sscanf(cc1,"%d",&chg)==1)) {
            atm--;
            if((atm>=0)&&(atm<nAtom))
              atInfo[atm].formalCharge = chg;
          }
        }
      }
    }
    p=nextline(p);
  }
  if(ok) {
	 cset = CoordSetNew();
	 cset->NIndex=nAtom;
	 cset->Coord=coord;
	 cset->NTmpBond=nBond;
	 cset->TmpBond=bond;
    strcpy(cset->Name,nameTmp);
  } else {
	 VLAFreeP(bond);
	 VLAFreeP(coord);
  }
  if(atInfoPtr)
	 *atInfoPtr = atInfo;
  return(cset);
}

/*========================================================================*/
ObjectMolecule *ObjectMoleculeReadMOLStr(ObjectMolecule *I,char *MOLStr,int frame,int discrete)
{
  int ok = true;
  CoordSet *cset=NULL;
  AtomInfoType *atInfo;
  int isNew;
  int nAtom;

  if(!I) 
	 isNew=true;
  else 
	 isNew=false;

  if(isNew) {
    I=(ObjectMolecule*)ObjectMoleculeNew(discrete);
    atInfo = I->AtomInfo;
    isNew = true;
  } else {
    atInfo=VLAMalloc(10,sizeof(AtomInfoType),2,true); /* autozero here is important */
    isNew = false;
  }

  cset=ObjectMoleculeMOLStr2CoordSet(MOLStr,&atInfo);
  
  if(!cset) 
	 {
      ObjectMoleculeFree(I);
      I=NULL;
		ok=false;
	 }
  
  if(ok)
	 {
		if(frame<0)
		  frame=I->NCSet;
		if(I->NCSet<=frame)
		  I->NCSet=frame+1;
		VLACheck(I->CSet,CoordSet*,frame);
      
      nAtom=cset->NIndex;
      
      cset->Obj = I;
      cset->fEnumIndices(cset);
      if(cset->fInvalidateRep)
        cset->fInvalidateRep(cset,cRepAll,cRepInvRep);
      if(isNew) {		
        I->AtomInfo=atInfo; /* IMPORTANT to reassign: this VLA may have moved! */
      } else {
        ObjectMoleculeMerge(I,atInfo,cset,false,cAIC_MOLMask); /* NOTE: will release atInfo */
      }

      if(isNew) I->NAtom=nAtom;
      if(frame<0) frame=I->NCSet;
      VLACheck(I->CSet,CoordSet*,frame);
      if(I->NCSet<=frame) I->NCSet=frame+1;
      if(I->CSet[frame]) I->CSet[frame]->fFree(I->CSet[frame]);
      I->CSet[frame] = cset;
      
      if(isNew) I->NBond = ObjectMoleculeConnect(I,&I->Bond,I->AtomInfo,cset,false);
      
      SceneCountFrames();
      ObjectMoleculeExtendIndices(I);
      ObjectMoleculeSort(I);
      ObjectMoleculeUpdateIDNumbers(I);
      ObjectMoleculeUpdateNonbonded(I);
	 }
  return(I);
}
/*========================================================================*/
ObjectMolecule *ObjectMoleculeLoadMOLFile(ObjectMolecule *obj,char *fname,int frame,int discrete)
{
  ObjectMolecule* I=NULL;
  int ok=true;
  FILE *f;
  long size;
  char *buffer,*p;

  f=fopen(fname,"rb");
  if(!f)
	 ok=ErrMessage("ObjectMoleculeLoadMOLFile","Unable to open file!");
  else
	 {
      PRINTFB(FB_ObjectMolecule,FB_Blather)
        " ObjectMoleculeLoadMOLFile: Loading from %s.\n",fname
        ENDFB;
		
		fseek(f,0,SEEK_END);
      size=ftell(f);
		fseek(f,0,SEEK_SET);

		buffer=(char*)mmalloc(size+255);
		ErrChkPtr(buffer);
		p=buffer;
		fseek(f,0,SEEK_SET);
		fread(p,size,1,f);
		p[size]=0;
		fclose(f);
		I=ObjectMoleculeReadMOLStr(obj,buffer,frame,discrete);
		mfree(buffer);
	 }

  return(I);
}

/*========================================================================*/
void ObjectMoleculeMerge(ObjectMolecule *I,AtomInfoType *ai,
                         CoordSet *cs,int bondSearchFlag,int aic_mask)
{
  int *index,*outdex,*a2i,*i2a;
  BondType *bond=NULL;
  int a,b,c,lb=0,nb,ac,a1,a2;
  int found;
  int nAt,nBd,nBond;
  int expansionFlag = false;
  AtomInfoType *ai2;
  int oldNAtom,oldNBond;

  oldNAtom = I->NAtom;
  oldNBond = I->NBond;


  /* first, sort the coodinate set */
  
  index=AtomInfoGetSortedIndex(ai,cs->NIndex,&outdex);
  for(b=0;b<cs->NIndex;b++)
	 cs->IdxToAtm[b]=outdex[cs->IdxToAtm[b]];
  for(b=0;b<cs->NIndex;b++)
	 cs->AtmToIdx[b]=-1;
  for(b=0;b<cs->NIndex;b++)
	 cs->AtmToIdx[cs->IdxToAtm[b]]=b;
  ai2=(AtomInfoType*)VLAMalloc(cs->NIndex,sizeof(AtomInfoType),5,true); /* autozero here is important */
  for(a=0;a<cs->NIndex;a++) 
	 ai2[a]=ai[index[a]]; /* creates a sorted list of atom info records */
  VLAFreeP(ai);
  ai=ai2;

  /* now, match it up with the current object's atomic information */
	 
  for(a=0;a<cs->NIndex;a++) {
	 index[a]=-1;
	 outdex[a]=-1;
  }

  c=0;
  b=0;  
  for(a=0;a<cs->NIndex;a++) {
	 found=false;
    if(!I->DiscreteFlag) { /* don't even try matching for discrete objects */
      lb=b;
      while(b<I->NAtom) {
        ac=(AtomInfoCompare(ai+a,I->AtomInfo+b));
        if(!ac) {
          found=true;
          break;
        }
        else if(ac<0) {
          break;
        }
        b++;
      }
    }
	 if(found) {
		index[a]=b; /* store real atom index b for a in index[a] */
		b++;
	 } else {
	   index[a]=I->NAtom+c; /* otherwise, this is a new atom */
	   c++;
	   b=lb;
	 }
  }

  /* first, reassign atom info for matched atoms */

  /* allocate additional space */
  if(c)
	{
	  expansionFlag=true;
	  nAt=I->NAtom+c;
	} else {
     nAt=I->NAtom;
   }
  
  if(expansionFlag) {
	VLACheck(I->AtomInfo,AtomInfoType,nAt);
  }

  /* allocate our new x-ref tables */
  if(nAt<I->NAtom) nAt=I->NAtom;
  a2i = Alloc(int,nAt);
  i2a = Alloc(int,cs->NIndex);
  ErrChkPtr(a2i);
  ErrChkPtr(i2a);
  
  for(a=0;a<cs->NIndex;a++) /* a is in original file space */
    {
		a1=cs->IdxToAtm[a]; /* a1 is in sorted atom info space */
		a2=index[a1];
		i2a[a]=a2; /* a2 is in object space */
      if(a2<oldNAtom)
        AtomInfoCombine(I->AtomInfo+a2,ai+a1,aic_mask);
      else
        *(I->AtomInfo+a2)=*(ai+a1);
    }
  
  if(I->DiscreteFlag) {
    if(I->NDiscrete<nAt) {
      VLACheck(I->DiscreteAtmToIdx,int,nAt);
      VLACheck(I->DiscreteCSet,CoordSet*,nAt);    
      for(a=I->NDiscrete;a<nAt;a++) {
        I->DiscreteAtmToIdx[a]=-1;
        I->DiscreteCSet[a]=NULL;
      }
    }
    I->NDiscrete = nAt;
  }
  
  cs->NAtIndex = nAt;
  I->NAtom = nAt;
  
  FreeP(cs->AtmToIdx);
  FreeP(cs->IdxToAtm);
  cs->AtmToIdx = a2i;
  cs->IdxToAtm = i2a;

  if(I->DiscreteFlag) {
    FreeP(cs->AtmToIdx);
    for(a=0;a<cs->NIndex;a++) {
      I->DiscreteAtmToIdx[cs->IdxToAtm[a]]=a;
      I->DiscreteCSet[cs->IdxToAtm[a]] = cs;
    }
  } else {
    for(a=0;a<cs->NAtIndex;a++)
      cs->AtmToIdx[a]=-1;
    for(a=0;a<cs->NIndex;a++)
      cs->AtmToIdx[cs->IdxToAtm[a]]=a;
  }
  
  VLAFreeP(ai);
  AtomInfoFreeSortedIndexes(index,outdex);
  
  /* now find and integrate and any new bonds */
  if(expansionFlag) { /* expansion flag means we have introduced at least 1 new atom */
    nBond = ObjectMoleculeConnect(I,&bond,I->AtomInfo,cs,bondSearchFlag);
    if(nBond) {
      index=Alloc(int,nBond);
      
      c=0;
      b=0;  
      nb=0;
      for(a=0;a<nBond;a++) { /* iterate over new bonds */
        found=false;
        b=nb; /* pick up where we left off */
        while(b<I->NBond) { 
          ac=BondCompare(bond+a,I->Bond+b);
          if(!ac) { /* zero is a match */
            found=true;
            break;
          } else if(ac<0) { /* gone past position of this bond */
            break;
          }
          b++; /* no match yet, keep looking */
        }
        if(found) {
          index[a]=b; /* existing bond...*/
          nb=b+1;
        } else { /* this is a new bond, save index and increment */
          index[a]=I->NBond+c;
          c++; 
        }
      }
      /* first, reassign atom info for matched atoms */
      if(c) {
        /* allocate additional space */
        nBd=I->NBond+c;
        
        VLACheck(I->Bond,BondType,nBd);
        
        for(a=0;a<nBond;a++) /* copy the new bonds */
          {
            a2=index[a];
            if(a2 >= I->NBond) { 
              I->Bond[a2] = bond[a];
            }
          }
        I->NBond=nBd;
      }
      FreeP(index);
    }
    VLAFreeP(bond);
  }

  if(oldNAtom) {
    if(oldNAtom==I->NAtom) {
      if(oldNBond!=I->NBond) {
        ObjectMoleculeInvalidate(I,cRepAll,cRepInvBonds);
      }
    } else {
      ObjectMoleculeInvalidate(I,cRepAll,cRepInvAtoms);
    }
  }

}
/*========================================================================*/
ObjectMolecule *ObjectMoleculeReadPDBStr(ObjectMolecule *I,char *PDBStr,int frame,int discrete)
{
  CoordSet *cset = NULL;
  AtomInfoType *atInfo;
  int ok=true;
  int isNew = true;
  unsigned int nAtom = 0;
  char *start,*restart=NULL;
  int repeatFlag = true;
  int successCnt = 0;
  SegIdent segi_override=""; /* saved segi for corrupted NMR pdb files */

  start=PDBStr;
  while(repeatFlag) {
    repeatFlag = false;
  
    if(!I) 
      isNew=true;
    else 
      isNew=false;
    
    if(ok) {
      
      if(isNew) {
        I=(ObjectMolecule*)ObjectMoleculeNew(discrete);
        atInfo = I->AtomInfo;
        isNew = true;
      } else {
        atInfo=VLAMalloc(10,sizeof(AtomInfoType),2,true); /* autozero here is important */
        isNew = false;
      }
      cset=ObjectMoleculePDBStr2CoordSet(start,&atInfo,&restart,segi_override);	 
      nAtom=cset->NIndex;
    }
    
    /* include coordinate set */
    if(ok) {

      cset->Obj = I;
      cset->fEnumIndices(cset);
      if(cset->fInvalidateRep)
        cset->fInvalidateRep(cset,cRepAll,cRepInvRep);
      if(isNew) {		
        I->AtomInfo=atInfo; /* IMPORTANT to reassign: this VLA may have moved! */
      } else {
        ObjectMoleculeMerge(I,atInfo,cset,true,cAIC_PDBMask); /* NOTE: will release atInfo */
      }
      if(isNew) I->NAtom=nAtom;
      if(frame<0) frame=I->NCSet;
      VLACheck(I->CSet,CoordSet*,frame);
      if(I->NCSet<=frame) I->NCSet=frame+1;
      if(I->CSet[frame]) I->CSet[frame]->fFree(I->CSet[frame]);
      I->CSet[frame] = cset;
      if(isNew) I->NBond = ObjectMoleculeConnect(I,&I->Bond,I->AtomInfo,cset,true);
      if(cset->Symmetry&&(!I->Symmetry)) {
        I->Symmetry=SymmetryCopy(cset->Symmetry);
        SymmetryAttemptGeneration(I->Symmetry);
      }
      SceneCountFrames();
      ObjectMoleculeExtendIndices(I);
      ObjectMoleculeSort(I);
      ObjectMoleculeUpdateIDNumbers(I);
      ObjectMoleculeUpdateNonbonded(I);
      successCnt++;
      if(successCnt>1) {
        if(successCnt==2){
          PRINTFB(FB_ObjectMolecule,FB_Actions)
            " ObjectMolReadPDBStr: read MODEL %d\n",1
            ENDFB;
            }
        PRINTFB(FB_ObjectMolecule,FB_Actions)
          " ObjectMolReadPDBStr: read MODEL %d\n",successCnt
          ENDFB;
      }
    }
    if(restart) {
      repeatFlag=true;
      start=restart;
      frame=frame+1;
      restart=NULL;
    }
  }
  return(I);
}
/*========================================================================*/
ObjectMolecule *ObjectMoleculeLoadPDBFile(ObjectMolecule *obj,char *fname,int frame,int discrete)
{
  ObjectMolecule *I=NULL;
  int ok=true;
  FILE *f;
  long size;
  char *buffer,*p;

  f=fopen(fname,"rb");
  if(!f)
	 ok=ErrMessage("ObjectMoleculeLoadPDBFile","Unable to open file!");
  else
	 {
      PRINTFB(FB_ObjectMolecule,FB_Blather)
        " ObjectMoleculeLoadPDBFile: Loading from %s.\n",fname
        ENDFB;
		
		fseek(f,0,SEEK_END);
      size=ftell(f);
		fseek(f,0,SEEK_SET);

		buffer=(char*)mmalloc(size+255);
		ErrChkPtr(buffer);
		p=buffer;
		fseek(f,0,SEEK_SET);
		fread(p,size,1,f);
		p[size]=0;
		fclose(f);

		I=ObjectMoleculeReadPDBStr(obj,buffer,frame,discrete);

		mfree(buffer);
	 }

  return(I);
}

/*========================================================================*/
void ObjectMoleculeAppendAtoms(ObjectMolecule *I,AtomInfoType *atInfo,CoordSet *cs)
{
  int a;
  BondType *ii;
  BondType *si;
  AtomInfoType *src,*dest;
  int nAtom,nBond;

  if(I->NAtom) {
	 nAtom = I->NAtom+cs->NIndex;
	 VLACheck(I->AtomInfo,AtomInfoType,nAtom);	 
	 dest = I->AtomInfo+I->NAtom;
	 src = atInfo;
	 for(a=0;a<cs->NIndex;a++)
		*(dest++)=*(src++);
	 I->NAtom=nAtom;
	 VLAFreeP(atInfo);
  } else {
	 if(I->AtomInfo)
		VLAFreeP(I->AtomInfo);
	 I->AtomInfo = atInfo;
	 I->NAtom=cs->NIndex;
  }
  nBond=I->NBond+cs->NTmpBond;
  if(!I->Bond)
	 I->Bond=VLAlloc(BondType,nBond);
  VLACheck(I->Bond,BondType,nBond);
  ii=I->Bond+I->NBond;
  si=cs->TmpBond;
  for(a=0;a<cs->NTmpBond;a++)
	 {
		ii->index[0]=cs->IdxToAtm[si->index[0]];
		ii->index[1]=cs->IdxToAtm[si->index[1]];
      ii->order=si->order;
      ii->stereo=si->stereo;
      ii->id=-1;
      ii++;
      si++;
	 }
  I->NBond=nBond;
}
/*========================================================================*/
CoordSet *ObjectMoleculeGetCoordSet(ObjectMolecule *I,int setIndex)
{
  if((setIndex>=0)&&(setIndex<I->NCSet))
	 return(I->CSet[setIndex]);
  else
	 return(NULL);
}
/*========================================================================*/
void ObjectMoleculeTransformTTTf(ObjectMolecule *I,float *ttt,int frame) 
{
  int b;
  CoordSet *cs;
  for(b=0;b<I->NCSet;b++)
	{
     if((frame<0)||(frame==b)) {
       cs=I->CSet[b];
       if(cs) {
         if(cs->fInvalidateRep)
           cs->fInvalidateRep(I->CSet[b],cRepAll,cRepInvCoord);
         MatrixApplyTTTfn3f(cs->NIndex,cs->Coord,ttt,cs->Coord);
       }
     }
	}
}
/*========================================================================*/
void ObjectMoleculeSeleOp(ObjectMolecule *I,int sele,ObjectMoleculeOpRec *op)
{
  int a,b,c,s,d,t_i;
  int a1,ind;
  float r,rms;
  float v1[3],v2,*vv1,*vv2,*coord,*vt,*vt1,*vt2;
  int inv_flag;
  int hit_flag = false;
  int ok = true;
  int cnt;
  int match_flag=false;
  int offset;
  CoordSet *cs;
  AtomInfoType *ai,*ai0,*ai_option;
  
  PRINTFD(FB_ObjectMolecule)
    " ObjectMoleculeSeleOp-DEBUG: sele %d op->code %d\n",sele,op->code
    ENDFD;

  if(sele>=0) {
	SelectorUpdateTable();
   /* always run on entry */
	switch(op->code) {
	case OMOP_ALTR: 
   case OMOP_AlterState:
     PBlock();
     /* PBlockAndUnlockAPI() is not safe.
      * what if "v" is invalidated by another thread? */
     break;
   }
   /* */
	switch(op->code) {
	case OMOP_AddHydrogens:
     ObjectMoleculeAddSeleHydrogens(I,sele);
     break;
#ifdef _OLD_CODE
     if(!ObjectMoleculeVerifyChemistry(I)) {
       ErrMessage(" AddHydrogens","missing chemical geometry information.");
     } else {
       doneFlag=false;
       while(!doneFlag) {
         doneFlag=true;
         a=0;
         while(a<I->NAtom) {
           ai=I->AtomInfo + a;
           s=I->AtomInfo[a].selEntry;
           if(SelectorIsMember(s,sele))
             if(ObjectMoleculeFillOpenValences(I,a)) {
               hit_flag=true;
               doneFlag=false;
             }
           a++; /* realize that the atom list may have been resorted */
         }
       }
       if(hit_flag) {
         ObjectMoleculeSort(I);
         ObjectMoleculeUpdateIDNumbers(I);
       } 
     }
     break;
#endif

	case OMOP_PrepareFromTemplate:
     ai0=op->ai; /* template atom */
     for(a=0;a<I->NAtom;a++)
       {
         s=I->AtomInfo[a].selEntry;
         if(SelectorIsMember(s,sele))
           {
             ai = I->AtomInfo + a;
             ai->hetatm=ai0->hetatm;
             ai->flags=ai0->flags;
             strcpy(ai->chain,ai0->chain);
             strcpy(ai->alt,ai0->alt);
             strcpy(ai->segi,ai0->segi);
             if(op->i1==1) { /* mode 1, merge residue information */
               strcpy(ai->resi,ai0->resi);
               ai->resv=ai0->resv;
               strcpy(ai->resn,ai0->resn);    
             }
             if((ai->elem[0]==ai0->elem[0])&&(ai->elem[1]==ai0->elem[1]))
               ai->color=ai0->color;
             else
               ai->color=AtomInfoGetColor(ai);
             for(b=0;b<cRepCnt;b++)
               ai->visRep[b]=ai0->visRep[b];
             ai->id=-1;
             op->i2++;
           }
       }
     break;
     
	case OMOP_PDB1:
	  for(b=0;b<I->NCSet;b++)
       if(I->CSet[b])
		  {
			if((b==op->i1)||(op->i1<0))
			  for(a=0;a<I->NAtom;a++)
				{
				  s=I->AtomInfo[a].selEntry;
              if(SelectorIsMember(s,sele))
                {
                  if(I->DiscreteFlag) {
                    if(I->CSet[b]==I->DiscreteCSet[a])
                      ind=I->DiscreteAtmToIdx[a];
                    else
                      ind=-1;
                  } else 
                    ind=I->CSet[b]->AtmToIdx[a];
                  if(ind>=0) 
                    CoordSetAtomToPDBStrVLA(&op->charVLA,&op->i2,I->AtomInfo+a,
                                            I->CSet[b]->Coord+(3*ind),op->i3);
                  op->i3++;
                }
				}
		  }
	  break;
	case OMOP_AVRT: /* average vertex coordinate */
     for(a=0;a<I->NAtom;a++)
       {
         s=I->AtomInfo[a].selEntry;
         if(SelectorIsMember(s,sele))
           {
             cnt=0;
             for(b=0;b<I->NCSet;b++) {
               if(I->CSet[b])
                 {
                   if(I->DiscreteFlag) {
                     if(I->CSet[b]==I->DiscreteCSet[a])
                       a1=I->DiscreteAtmToIdx[a];
                     else
                       a1=-1;
                   } else 
                     a1=I->CSet[b]->AtmToIdx[a];
                   if(a1>=0) {
                     if(!cnt) {
                       VLACheck(op->vv1,float,(op->nvv1*3)+2);
                       VLACheck(op->vc1,int,op->nvv1);
                     }
                     cnt++;
                     vv2=I->CSet[b]->Coord+(3*a1);
                     vv1=op->vv1+(op->nvv1*3);
                     *(vv1++)+=*(vv2++);
                     *(vv1++)+=*(vv2++);
                     *(vv1++)+=*(vv2++);
                   }
                 }
             }
             op->vc1[op->nvv1]=cnt;
             if(cnt)
               op->nvv1++;

           }
       }
     break;
	case OMOP_SFIT: /* state fitting within a single object */
     vt = Alloc(float,3*op->nvv2);
     cnt = 0;
     for(a=0;a<I->NAtom;a++)
       {
         s=I->AtomInfo[a].selEntry;
         if(SelectorIsMember(s,sele))
           {
             cnt++;
             break;
           }
       }
     if(cnt) { /* only perform action for selected object */
       
       for(b=0;b<I->NCSet;b++) {
         rms = -1.0;
         vt1 = vt; /* reset target vertex pointers */
         vt2 = op->vv2;
         t_i = 0; /* original target vertex index */
         if(I->CSet[b]&&(b!=op->i2))
           {
             op->nvv1=0;
             for(a=0;a<I->NAtom;a++)
               {
                 s=I->AtomInfo[a].selEntry;
                 if(SelectorIsMember(s,sele))
                   {
                     if(I->DiscreteFlag) {
                       if(I->CSet[b]==I->DiscreteCSet[a])
                         a1=I->DiscreteAtmToIdx[a];
                       else
                         a1=-1;
                     } else 
                       a1=I->CSet[b]->AtmToIdx[a];
                     if(a1>=0) {

                       match_flag=false;
                       while(t_i<op->nvv2) {
                         if(op->i1VLA[t_i]==a) {/* same atom? */
                           match_flag=true;
                           break;
                         }
                         if(op->i1VLA[t_i]<a) { /* catch up? */
                           t_i++;
                           vt2+=3;
                         } else 
                           break;
                       }
                       if(match_flag) {
                         VLACheck(op->vv1,float,(op->nvv1*3)+2);
                         vv2=I->CSet[b]->Coord+(3*a1);
                         vv1=op->vv1+(op->nvv1*3);
                         *(vv1++)=*(vv2++);
                         *(vv1++)=*(vv2++);
                         *(vv1++)=*(vv2++);
                         *(vt1++)=*(vt2);
                         *(vt1++)=*(vt2+1);
                         *(vt1++)=*(vt2+2);
                         op->nvv1++;
                       }
                     }
                   }
               }
             if(op->nvv1!=op->nvv2) {
               PRINTFB(FB_Executive,FB_Warnings)
                 "Executive-Warning: Missing atoms in state %d (%d instead of %d).\n",
                 b+1,op->nvv1,op->nvv2
                 ENDFB;
             }
             if(op->nvv1) {
               if(op->i1!=0) /* fitting flag */
                 rms = MatrixFitRMS(op->nvv1,op->vv1,vt,NULL,op->ttt);
               else 
                 rms = MatrixGetRMS(op->nvv1,op->vv1,vt,NULL);
               if(op->i1==2) 
                 ObjectMoleculeTransformTTTf(I,op->ttt,b);
             } else {
               PRINTFB(FB_Executive,FB_Warnings)
                 "Executive-Warning: No matches found for state %d.\n",b+1
                 ENDFB;
             }
           }
         VLACheck(op->f1VLA,float,b);
         op->f1VLA[b]=rms;
       }
       VLASize(op->f1VLA,float,I->NCSet);  /* NOTE this action is object-specific! */
     }
     FreeP(vt);
     break;
	case OMOP_SetGeometry: /* save undo */
     for(a=0;a<I->NAtom;a++)
       {
         s=I->AtomInfo[a].selEntry;
         if(SelectorIsMember(s,sele))
           {
             ai = I->AtomInfo + a;
             ai->geom=op->i1;
             ai->valence=op->i2;
             op->i3++;
             hit_flag=true;
             break;
           }
       }
     break;
	case OMOP_SaveUndo: /* save undo */
     for(a=0;a<I->NAtom;a++)
       {
         s=I->AtomInfo[a].selEntry;
         if(SelectorIsMember(s,sele))
           {
             hit_flag=true;
             break;
           }
       }
     break;
	case OMOP_Identify: /* identify atoms */
     for(a=0;a<I->NAtom;a++)
       {
         s=I->AtomInfo[a].selEntry;
         if(SelectorIsMember(s,sele))
           {
             VLACheck(op->i1VLA,int,op->i1);
             op->i1VLA[op->i1++]=I->AtomInfo[a].id;
           }
       }
     break;
	case OMOP_IdentifyObjects: /* identify atoms */
     for(a=0;a<I->NAtom;a++)
       {
         s=I->AtomInfo[a].selEntry;
         if(SelectorIsMember(s,sele))
           {
             VLACheck(op->i1VLA,int,op->i1);
             op->i1VLA[op->i1]=I->AtomInfo[a].id; 
             VLACheck(op->obj1VLA,ObjectMolecule*,op->i1);
             op->obj1VLA[op->i1]=I;
             op->i1++;
           }
       }
     break;
	case OMOP_Index: /* identify atoms */
     for(a=0;a<I->NAtom;a++)
       {
         s=I->AtomInfo[a].selEntry;
         if(SelectorIsMember(s,sele))
           {
             VLACheck(op->i1VLA,int,op->i1);
             op->i1VLA[op->i1]=a; /* NOTE: need to incr by 1 before python */
             VLACheck(op->obj1VLA,ObjectMolecule*,op->i1);
             op->obj1VLA[op->i1]=I;
             op->i1++;
           }
       }
     break;
	case OMOP_GetObjects: /* identify atoms */
     for(a=0;a<I->NAtom;a++)
       {
         s=I->AtomInfo[a].selEntry;
         if(SelectorIsMember(s,sele))
           {
             VLACheck(op->obj1VLA,ObjectMolecule*,op->i1);
             op->obj1VLA[op->i1]=I;
             op->i1++;
             break;
           }
       }
     break;
	case OMOP_CountAtoms: /* count atoms in object, in selection */
     ai=I->AtomInfo;
     for(a=0;a<I->NAtom;a++)
       {
         s=ai->selEntry;
         if(SelectorIsMember(s,sele))
           op->i1++;
         ai++;
       }
     break;
   case OMOP_PhiPsi:
     ai=I->AtomInfo;
     for(a=0;a<I->NAtom;a++)
       {
         s=ai->selEntry;
         if(SelectorIsMember(s,sele)) {
           VLACheck(op->i1VLA,int,op->i1);
           op->i1VLA[op->i1]=a;
           VLACheck(op->obj1VLA,ObjectMolecule*,op->i1);
           op->obj1VLA[op->i1]=I;
           VLACheck(op->f1VLA,float,op->i1);
           VLACheck(op->f2VLA,float,op->i1);
           if(ObjectMoleculeGetPhiPsi(I,a,op->f1VLA+op->i1,op->f2VLA+op->i1,op->i2))
             op->i1++;
         }
         ai++; 
       }
     break;
	case OMOP_Cartoon: /* adjust cartoon type */
     ai=I->AtomInfo;
     for(a=0;a<I->NAtom;a++)
       {
         s=ai->selEntry;
         if(SelectorIsMember(s,sele)) {
           ai->cartoon = op->i1;
           op->i2++;
         }
         ai++; 
       }
     break;
	case OMOP_Protect: /* protect atoms from movement */
     ai=I->AtomInfo;
     for(a=0;a<I->NAtom;a++)
       {
         s=ai->selEntry;
         if(SelectorIsMember(s,sele))
           {
             ai->protected = op->i1;
             op->i2++;
           }
         ai++;
       }
     break;
	case OMOP_Mask: /* protect atoms from selection */
     ai=I->AtomInfo;
     for(a=0;a<I->NAtom;a++)
       {
         s=ai->selEntry;
         if(SelectorIsMember(s,sele))
           {
             ai->masked = op->i1;
             op->i2++;
           }
         ai++;
       }
     break;
	case OMOP_SetB: /* set B-value */
     ai=I->AtomInfo;
     for(a=0;a<I->NAtom;a++)
       {
         s=ai->selEntry;
         if(SelectorIsMember(s,sele))
           {
             ai->b = op->f1;
             op->i2++;
           }
         ai++;
       }
     break;
	case OMOP_Remove: /* flag atoms for deletion */
     ai=I->AtomInfo;
     if(I->DiscreteFlag) /* for now, can't remove atoms from discrete objects */
       ErrMessage("Remove","Can't remove atoms from discrete objects.");
     else
       for(a=0;a<I->NAtom;a++)
         {         
           ai->deleteFlag=false;
           s=ai->selEntry;
           if(SelectorIsMember(s,sele))
             {
               ai->deleteFlag=true;
               op->i1++;
             }
           ai++;
         }
     break;
   case OMOP_SingleStateVertices: /* same as OMOP_VERT for a single state */
     ai=I->AtomInfo;
     if(op->cs1<I->NCSet) {
       if(I->CSet[op->cs1]) {
         b = op->cs1;
         for(a=0;a<I->NAtom;a++)
           {         
             s=ai->selEntry;
             if(SelectorIsMember(s,sele))
               {
                 ai->deleteFlag=true; /* ?????? */
                 op->i1++;

                 if(I->DiscreteFlag) {
                   if(I->CSet[b]==I->DiscreteCSet[a])
                     a1=I->DiscreteAtmToIdx[a];
                   else
                     a1=-1;
                 } else 
                   a1=I->CSet[b]->AtmToIdx[a];
                 if(a1>=0) {
                   VLACheck(op->vv1,float,(op->nvv1*3)+2);
                   vv2=I->CSet[b]->Coord+(3*a1);
                   vv1=op->vv1+(op->nvv1*3);
                   *(vv1++)=*(vv2++);
                   *(vv1++)=*(vv2++);
                   *(vv1++)=*(vv2++);
                   op->nvv1++;
                 }
               }
             ai++;
           }
       }
     }
     break;
   case OMOP_CSetIdxGetAndFlag:
     ai=I->AtomInfo;
     for(a=0;a<I->NAtom;a++)
       {         
         s=ai->selEntry;
         if(SelectorIsMember(s,sele))
           {
             for(b=op->cs1;b<=op->cs2;b++) {
               offset = b-op->cs1;
               if(b<I->NCSet) {
                 if(I->CSet[b]) {
                   if(I->DiscreteFlag) {
                     if(I->CSet[b]==I->DiscreteCSet[a])
                       a1=I->DiscreteAtmToIdx[a];
                     else
                       a1=-1;
                   } else 
                     a1=I->CSet[b]->AtmToIdx[a];
                   if(a1>=0) {
                     op->ii1[op->i1*offset+op->i2] = 1; /* presence flag */
                     vv1=op->vv1+3*(op->i1*offset+op->i2); /* atom-based offset */
                     vv2=I->CSet[b]->Coord+(3*a1);
                     *(vv1++)=*(vv2++);
                     *(vv1++)=*(vv2++);
                     *(vv1++)=*(vv2++);
                     op->nvv1++;
                   }
                 }
               }
             }
             op->i2++; /* atom index field for atoms within selection...*/
           }
         ai++;
       }
     break;
   case OMOP_CSetIdxSetFlagged:
     ai=I->AtomInfo;
     for(a=0;a<I->NAtom;a++)
       {         
         s=ai->selEntry;
         if(SelectorIsMember(s,sele))
           {
             for(b=op->cs1;b<=op->cs2;b++) {
               offset = b-op->cs1;
               if(b<I->NCSet) {
                 if(I->CSet[b]) {
                   if(I->DiscreteFlag) {
                     if(I->CSet[b]==I->DiscreteCSet[a])
                       a1=I->DiscreteAtmToIdx[a];
                     else
                       a1=-1;
                   } else 
                     a1=I->CSet[b]->AtmToIdx[a];
                   if(a1>=0) {
                     if(op->ii1[op->i1*offset+op->i2]) { /* copy flag */
		       vv1=op->vv1+3*(op->i1*offset+op->i2); /* atom-based offset */
		       vv2=I->CSet[b]->Coord+(3*a1);
		       *(vv2++)=*(vv1++);
		       *(vv2++)=*(vv1++);
		       *(vv2++)=*(vv1++);
		       op->nvv1++;
		     }
                   }
                 }
               }
             }
             op->i2++; /* atom index field for atoms within selection...*/
           }
         ai++;
       }
     break;
   default:
     for(a=0;a<I->NAtom;a++)
		 {
		   switch(op->code) { 
         case OMOP_Flag: 
           I->AtomInfo[a].flags &= op->i2; /* clear flag using mask */
           op->i4++;
           /* no break here is intentional!  */
         case OMOP_FlagSet:
         case OMOP_FlagClear:
		   case OMOP_COLR: /* normal atom based loops */
		   case OMOP_VISI:
		   case OMOP_TTTF:
         case OMOP_ALTR:
         case OMOP_LABL:
         case OMOP_AlterState:
			 s=I->AtomInfo[a].selEntry;
          if(SelectorIsMember(s,sele))
            {
              switch(op->code) {
              case OMOP_Flag:
                I->AtomInfo[a].flags |= op->i1; /* set flag */
                op->i3++;
                break;
              case OMOP_FlagSet:
                I->AtomInfo[a].flags |= op->i1; /* set flag */
                op->i3++;
                break;
              case OMOP_FlagClear:
                I->AtomInfo[a].flags &= op->i2; /* clear flag */
                op->i3++;
                break;
              case OMOP_VISI:
                if(op->i1<0)
                  for(d=0;d<cRepCnt;d++) 
                    I->AtomInfo[a].visRep[d]=op->i2;                      
                else {
                  I->AtomInfo[a].visRep[op->i1]=op->i2;
                  if(op->i1==cRepCell) I->Obj.RepVis[cRepCell]=op->i2;
                }
                break;
                break;
              case OMOP_COLR:
                I->AtomInfo[a].color=op->i1;
                op->i2++;
                break;
              case OMOP_TTTF:
                hit_flag=true;
                break;
              case OMOP_LABL:
                if (ok) {
                  if(!op->s1[0]) {
                    I->AtomInfo[a].label[0]=0;
                    op->i1++;
                    I->AtomInfo[a].visRep[cRepLabel]=false;
                    hit_flag=true;
                  }  else {
                    if(PLabelAtom(&I->AtomInfo[a],op->s1,a)) {
                      op->i1++;
                      I->AtomInfo[a].visRep[cRepLabel]=true;
                      hit_flag=true;
                    } else
                      ok=false;
                  }
                }
                break;
              case OMOP_ALTR:
                if (ok) {
                  if(PAlterAtom(&I->AtomInfo[a],op->s1,op->i2,I->Obj.Name,a))
                    op->i1++;
                  else
                    ok=false;
                }
                break;
              case OMOP_AlterState:
                if (ok) {
                  if(op->i2<I->NCSet) {
                    cs = I->CSet[op->i2];
                    if(cs) {
                      if(I->DiscreteFlag) {
                        if(cs==I->DiscreteCSet[a])
                          a1=I->DiscreteAtmToIdx[a];
                        else
                          a1=-1;
                      } else 
                        a1=cs->AtmToIdx[a];
                      if(a1>=0) {
                        if(op->i4) 
                          ai_option = I->AtomInfo + a;
                        else
                          ai_option = NULL;
                        if(PAlterAtomState(cs->Coord+(a1*3),op->s1,op->i3,ai_option)) {
                          op->i1++;
                          hit_flag=true;
                        } else
                          ok=false;
                      }
                    }
                  }
                }
                break;
              }
              break;
            }
			 break;

          /* coord-set based properties, iterating only a single coordinate set */
         case OMOP_CSetMinMax:          
         case OMOP_CSetSumVertices:
         case OMOP_CSetMoment: 
           if((op->cs1>=0)&&(op->cs1<I->NCSet)) {
             cs=I->CSet[op->cs1];
             if(cs) {
               s=I->AtomInfo[a].selEntry;
               if(SelectorIsMember(s,sele))
                 {
                   switch(op->code) {
                   case OMOP_CSetSumVertices:
                     if(I->DiscreteFlag) {
                       if(cs==I->DiscreteCSet[a])
                         a1=I->DiscreteAtmToIdx[a];
                       else
                         a1=-1;
                     } else 
                       a1=cs->AtmToIdx[a];
                     if(a1>=0)
                       {
                         coord = cs->Coord+3*a1;
                         if(op->i2) /* do we want object-transformed coordinates? */
                           if(I->Obj.TTTFlag) {
                             transformTTT44f3f(I->Obj.TTT,coord,v1);
                             coord=v1;
                           }
                         add3f(op->v1,coord,op->v1);
                         op->i1++;
                       }
                     break;
                   case OMOP_CSetMinMax:
                     if(I->DiscreteFlag) {
                       if(cs==I->DiscreteCSet[a])
                         a1=I->DiscreteAtmToIdx[a];
                       else
                         a1=-1;
                     } else 
                       a1=cs->AtmToIdx[a];
                     if(a1>=0)
                       {
                         coord = cs->Coord+3*a1;
                         if(op->i2) /* do we want object-transformed coordinates? */
                           if(I->Obj.TTTFlag) {
                             transformTTT44f3f(I->Obj.TTT,coord,v1);
                             coord=v1;
                           }
                         if(op->i1) {
                           for(c=0;c<3;c++) {
                             if(*(op->v1+c)>*(coord+c)) *(op->v1+c)=*(coord+c);
                             if(*(op->v2+c)<*(coord+c)) *(op->v2+c)=*(coord+c);
                           }
                         } else {
                           for(c=0;c<3;c++) {
                             *(op->v1+c)=*(coord+c);
                             *(op->v2+c)=*(coord+c);
                           }
                         }
                         op->i1++;
                       }
                     break;
                   case OMOP_CSetMoment: 
                     if(I->DiscreteFlag) {
                       if(cs==I->DiscreteCSet[a])
                         a1=I->DiscreteAtmToIdx[a];
                       else
                         a1=-1;
                     } else 
                       a1=cs->AtmToIdx[a];
                     if(a1>=0) {
                       subtract3f(cs->Coord+(3*a1),op->v1,v1);
                       v2=v1[0]*v1[0]+v1[1]*v1[1]+v1[2]*v1[2]; 
                       op->d[0][0] += v2 - v1[0] * v1[0];
                       op->d[0][1] +=    - v1[0] * v1[1];
                       op->d[0][2] +=    - v1[0] * v1[2];
                       op->d[1][0] +=    - v1[1] * v1[0];
                       op->d[1][1] += v2 - v1[1] * v1[1];
                       op->d[1][2] +=    - v1[1] * v1[2];
                       op->d[2][0] +=    - v1[2] * v1[0];
                       op->d[2][1] +=    - v1[2] * v1[1];
                       op->d[2][2] += v2 - v1[2] * v1[2];
                     }
                     break;
                     
                   }
                 }
             }
           }
           break;
		   default: /* coord-set based properties, iterating as all coordsets within atoms */
			 for(b=0;b<I->NCSet;b++)
			   if(I->CSet[b])
              {
                cs=I->CSet[b];
                inv_flag=false;
                s=I->AtomInfo[a].selEntry;
                if(SelectorIsMember(s,sele))
                  {
                    switch(op->code) {
                    case OMOP_SUMC:
                      if(I->DiscreteFlag) {
                        if(cs==I->DiscreteCSet[a])
                          a1=I->DiscreteAtmToIdx[a];
                        else
                          a1=-1;
                      } else 
                        a1=cs->AtmToIdx[a];
							 if(a1>=0)
							   {
                          coord = cs->Coord+3*a1;
                          if(op->i2) /* do we want object-transformed coordinates? */
                            if(I->Obj.TTTFlag) {
                              transformTTT44f3f(I->Obj.TTT,coord,v1);
                              coord=v1;
                            }
                          add3f(op->v1,coord,op->v1);
                          op->i1++;
							   }
							 break;
                    case OMOP_MNMX:
                      if(I->DiscreteFlag) {
                        if(cs==I->DiscreteCSet[a])
                          a1=I->DiscreteAtmToIdx[a];
                        else
                          a1=-1;
                      } else 
                        a1=cs->AtmToIdx[a];
							 if(a1>=0)
							   {
                          coord = cs->Coord+3*a1;
                          if(op->i2) /* do we want object-transformed coordinates? */
                            if(I->Obj.TTTFlag) {
                              transformTTT44f3f(I->Obj.TTT,coord,v1);
                              coord=v1;
                            }
                          if(op->i1) {
                            for(c=0;c<3;c++) {
                              if(*(op->v1+c)>*(coord+c)) *(op->v1+c)=*(coord+c);
                              if(*(op->v2+c)<*(coord+c)) *(op->v2+c)=*(coord+c);
                            }
                          } else {
                            for(c=0;c<3;c++) {
                              *(op->v1+c)=*(coord+c);
                              *(op->v2+c)=*(coord+c);
                            }
                          }
                          op->i1++;
							   }
							 break;
                    case OMOP_MDST: 
                      if(I->DiscreteFlag) {
                        if(cs==I->DiscreteCSet[a])
                          a1=I->DiscreteAtmToIdx[a];
                        else
                          a1=-1;
                      } else 
                        a1=cs->AtmToIdx[a];
							 if(a1>=0)
							   {
                          r=diff3f(op->v1,cs->Coord+(3*a1));
                          if(r>op->f1)
                            op->f1=r;
							   }
							 break;
                    case OMOP_INVA:
                      if(I->DiscreteFlag) {
                        if(cs==I->DiscreteCSet[a])
                          a1=I->DiscreteAtmToIdx[a];
                        else
                          a1=-1;
                      } else 
                        a1=cs->AtmToIdx[a]; 
                      if(a1>=0)                     /* selection touches this coordinate set */ 
                        inv_flag=true;              /* so set the invalidation flag */
                      break;
                    case OMOP_VERT: 
                      if(I->DiscreteFlag) {
                        if(cs==I->DiscreteCSet[a])
                          a1=I->DiscreteAtmToIdx[a];
                        else
                          a1=-1;
                      } else 
                        a1=cs->AtmToIdx[a];
                      if(a1>=0) {
                        VLACheck(op->vv1,float,(op->nvv1*3)+2);
                        vv2=cs->Coord+(3*a1);
                        vv1=op->vv1+(op->nvv1*3);
                        *(vv1++)=*(vv2++);
                        *(vv1++)=*(vv2++);
                        *(vv1++)=*(vv2++);
                        op->nvv1++;
                      }
							 break;	
                    case OMOP_SVRT:  /* gives us only vertices for a specific coordinate set */
                      if(b==op->i1) {
                        if(I->DiscreteFlag) {
                          if(cs==I->DiscreteCSet[a])
                            a1=I->DiscreteAtmToIdx[a];
                          else
                            a1=-1;
                        } else 
                          a1=cs->AtmToIdx[a];
                        if(a1>=0) {
                          VLACheck(op->vv1,float,(op->nvv1*3)+2);
                          VLACheck(op->i1VLA,int,op->nvv1);
                          op->i1VLA[op->nvv1]=a; /* save atom index for later comparisons */
                          vv2=cs->Coord+(3*a1);
                          vv1=op->vv1+(op->nvv1*3);
                          *(vv1++)=*(vv2++);
                          *(vv1++)=*(vv2++);
                          *(vv1++)=*(vv2++);
                          op->nvv1++;
                        }
                      }
							 break;	
                      /* Moment of inertia tensor - unweighted - assumes v1 is center of molecule */
                    case OMOP_MOME: 
                      if(I->DiscreteFlag) {
                        if(cs==I->DiscreteCSet[a])
                          a1=I->DiscreteAtmToIdx[a];
                        else
                          a1=-1;
                      } else 
                        a1=cs->AtmToIdx[a];
							 if(a1>=0) {
							   subtract3f(cs->Coord+(3*a1),op->v1,v1);
							   v2=v1[0]*v1[0]+v1[1]*v1[1]+v1[2]*v1[2]; 
							   op->d[0][0] += v2 - v1[0] * v1[0];
							   op->d[0][1] +=    - v1[0] * v1[1];
							   op->d[0][2] +=    - v1[0] * v1[2];
							   op->d[1][0] +=    - v1[1] * v1[0];
							   op->d[1][1] += v2 - v1[1] * v1[1];
							   op->d[1][2] +=    - v1[1] * v1[2];
							   op->d[2][0] +=    - v1[2] * v1[0];
							   op->d[2][1] +=    - v1[2] * v1[1];
							   op->d[2][2] += v2 - v1[2] * v1[2];
							 }
							 break;
                    }
                  }
                switch(op->code) { /* full coord-set based */
                case OMOP_INVA:
                  if(inv_flag) {
                    if(op->i1<0) {
                      /* invalidate all representations */
                      for(d=0;d<cRepCnt;d++) {
                        if(cs->fInvalidateRep)
                          cs->fInvalidateRep(cs,d,op->i2);
                      }
                    } else if(cs->fInvalidateRep) 
                      /* invalidate only that particular representation */
                      cs->fInvalidateRep(cs,op->i1,op->i2);
                  }
                  break;
                }
              } /* end coordset section */
          break;
         }
		 }
     break;
	}
	if(hit_flag) {
	  switch(op->code) {
	  case OMOP_TTTF:
       ObjectMoleculeTransformTTTf(I,op->ttt,-1);
       break;
	  case OMOP_LABL:
       ObjectMoleculeInvalidate(I,cRepLabel,cRepInvText);
       break;
     case OMOP_AlterState: /* overly coarse - doing all states, could do just 1 */
       ObjectMoleculeInvalidate(I,-1,cRepInvRep);
       SceneChanged();
       break;
     case OMOP_SaveUndo:
       op->i2=true;
       ObjectMoleculeSaveUndo(I,op->i1,false);
       break;
	  }
	}

   /* always run on exit...*/
	switch(op->code) {
	case OMOP_ALTR:
   case OMOP_AlterState:
     PUnblock();
     break;
   }
   /* */
  }
}
/*========================================================================*/
void ObjectMoleculeDescribeElement(ObjectMolecule *I,int index, char *buffer) 
{
  AtomInfoType *ai;
  ai=I->AtomInfo+index;
  if(ai->alt[0])
    sprintf(buffer,"%s: /%s/%s/%s/%s/%s`%s",ai->resn,I->Obj.Name,ai->segi,ai->chain,ai->resi,ai->name,ai->alt);
    else
  sprintf(buffer,"%s: /%s/%s/%s/%s/%s",ai->resn,I->Obj.Name,ai->segi,ai->chain,ai->resi,ai->name);
}
/*========================================================================*/
void ObjectMoleculeGetAtomSele(ObjectMolecule *I,int index, char *buffer) 
{
  AtomInfoType *ai;
  ai=I->AtomInfo+index;
  if(ai->alt[0]) 
    sprintf(buffer,"/%s/%s/%s/%s/%s`%s",I->Obj.Name,ai->segi,ai->chain,ai->resi,
            ai->name,ai->alt);
  else
    sprintf(buffer,"/%s/%s/%s/%s/%s`",I->Obj.Name,ai->segi,ai->chain,ai->resi,
            ai->name);   
}
/*========================================================================*/
void ObjectMoleculeGetAtomSeleLog(ObjectMolecule *I,int index, char *buffer) 
{
  AtomInfoType *ai;
  if(SettingGet(cSetting_robust_logs)) {
    ai=I->AtomInfo+index;
    
    if(ai->alt[0]) 
      sprintf(buffer,"/%s/%s/%s/%s/%s`%s",I->Obj.Name,ai->segi,ai->chain,ai->resi,
              ai->name,ai->alt);
    else
      sprintf(buffer,"/%s/%s/%s/%s/%s`",I->Obj.Name,ai->segi,ai->chain,ai->resi,
              ai->name);   
  } else {
    sprintf(buffer,"(%s`%d)",I->Obj.Name,index+1);
  }
}

void ObjectMoleculeGetAtomSeleFast(ObjectMolecule *I,int index, char *buffer) 
{
  AtomInfoType *ai;
  WordType segi,chain,resi,name,alt;
  ai=I->AtomInfo+index;
  
  if(ai->segi[0]) {
    strcpy(segi,"s;");
    strcat(segi,ai->segi);
  } else {
    strcpy(segi,"s;''");
  }
  if(ai->chain[0]) {
    strcpy(chain,"c;");
    strcat(chain,ai->chain);
  } else {
    strcpy(chain,"c;''");
  }
  if(ai->resi[0]) {
    strcpy(resi,"i;");
    strcat(resi,ai->resi);
  } else {
    strcpy(resi,"i;''");
  }
  if(ai->name[0]) {
    strcpy(name,"n;");
    strcat(name,ai->name);
  } else {
    strcpy(name,"n;''");
  }
  if(ai->alt[0]) {
    strcpy(alt,"alt ");
    strcat(alt,ai->alt);
  } else {
    strcpy(alt,"alt ''");
  }
  sprintf(buffer,"(%s&%s&%s&%s&%s&%s)",I->Obj.Name,segi,chain,resi,name,alt);
}

/*========================================================================*/
int ObjectMoleculeGetNFrames(ObjectMolecule *I)
{
  return I->NCSet;
}
/*========================================================================*/
void ObjectMoleculeUpdate(ObjectMolecule *I)
{
  int a;
  OrthoBusyPrime();
  for(a=0;a<I->NCSet;a++)
	 if(I->CSet[a]) {	
	   OrthoBusySlow(a,I->NCSet);
		PRINTFD(FB_ObjectMolecule)
		  " ObjectMolecule-DEBUG: updating state %d of \"%s\".\n" 
         , a+1, I->Obj.Name
        ENDFD;

      if(I->CSet[a]->fUpdate)
        I->CSet[a]->fUpdate(I->CSet[a]);
	 }
  if(I->Obj.RepVis[cRepCell]) {
    if(I->Symmetry) {
      if(I->Symmetry->Crystal) {
        if(I->UnitCellCGO)
          CGOFree(I->UnitCellCGO);
        I->UnitCellCGO = CrystalGetUnitCellCGO(I->Symmetry->Crystal);
      }
    }
  } 
  PRINTFD(FB_ObjectMolecule)
    " ObjectMolecule: updates complete for object %s.\n",I->Obj.Name
    ENDFD;
}
/*========================================================================*/
void ObjectMoleculeInvalidate(ObjectMolecule *I,int rep,int level)
{
  int a;
  PRINTFD(FB_ObjectMolecule)
    " ObjectMoleculeInvalidate: entered. rep: %d level: %d\n",rep,level
    ENDFD;

  if(level>=cRepInvBonds) {
    VLAFreeP(I->Neighbor); /* set I->Neighbor to NULL */
    if(I->Sculpt) {
      SculptFree(I->Sculpt);
      I->Sculpt = NULL;
    }
    ObjectMoleculeUpdateNonbonded(I);
    if(level>=cRepInvAtoms) {
      SelectorUpdateObjectSele(I);
    }
  }
  PRINTFD(FB_ObjectMolecule)
    " ObjectMoleculeInvalidate: invalidating representations...\n"
    ENDFD;

  for(a=0;a<I->NCSet;a++) 
	 if(I->CSet[a]) {	 
      if(I->CSet[a]->fInvalidateRep)
        I->CSet[a]->fInvalidateRep(I->CSet[a],rep,level);
	 }

  PRINTFD(FB_ObjectMolecule)
    " ObjectMoleculeInvalidate: leaving...\n"
    ENDFD;

}
/*========================================================================*/
int ObjectMoleculeMoveAtom(ObjectMolecule *I,int state,int index,float *v,int mode,int log)
{
  int result = 0;
  CoordSet *cs;
  if(!(I->AtomInfo[index].protected==1)) {
    if(state<0) state=0;
    if(I->NCSet==1) state=0;
    state = state % I->NCSet;
    cs = I->CSet[state];
    if(cs) {
      result = CoordSetMoveAtom(I->CSet[state],index,v,mode);
      cs->fInvalidateRep(cs,cRepAll,cRepInvCoord);
    }
  }
  if(log) {
    OrthoLineType line,buffer;
    if(SettingGet(cSetting_logging)) {
      ObjectMoleculeGetAtomSele(I,index,buffer);
      sprintf(line,"cmd.translate_atom(\"%s\",%15.9f,%15.9f,%15.9f,%d,%d,%d)\n",
              buffer,v[0],v[1],v[2],state+1,mode,0);
      PLog(line,cPLog_no_flush);
    }
  }
  /*  if(I->Sculpt) {
      SculptIterateObject(I->Sculpt,I,state,1);
      }*/
  return(result);
}
/*========================================================================*/
int ObjectMoleculeInitBondPath(ObjectMolecule *I,ObjectMoleculeBPRec *bp )
{
  int a;
  bp->dist = Alloc(int,I->NAtom);
  bp->list = Alloc(int,I->NAtom);
  for(a=0;a<I->NAtom;a++)
    bp->dist[a]=-1;
  bp->n_atom = 0;
  return 1;
}
/*========================================================================*/
int ObjectMoleculePurgeBondPath(ObjectMolecule *I,ObjectMoleculeBPRec *bp )
{
  FreeP(bp->dist);
  FreeP(bp->list);
  return 1;
}
/*========================================================================*/
int ObjectMoleculeGetBondPaths(ObjectMolecule *I,int atom,
                               int max,ObjectMoleculeBPRec *bp)
{
  /* returns list of bond counts from atom to all others 
     dist and list must be vla array pointers or NULL */

  int a,a1,a2,n;
  int cur;
  int n_cur;
  int b_cnt = 0;

  ObjectMoleculeUpdateNeighbors(I);
  
  /* reinitialize dist array (if we've done at least one pass) */

  for(a=0;a<bp->n_atom;a++)
    bp->dist[bp->list[a]]=-1;

  bp->n_atom = 0;
  bp->dist[atom] = 0;
  bp->list[bp->n_atom] = atom;
  bp->n_atom++;
  
  cur = 0;
  while(1) {
    b_cnt++;
    if(b_cnt>max) break;

    n_cur = bp->n_atom-cur;

    /* iterate through all current atoms */

    if(!n_cur) break;
    while(n_cur--) {
      a1 = bp->list[cur++];
      n=I->Neighbor[a1]; 
      n++; /* skip cnt */
      while(1) {
        a2=I->Neighbor[n];
        n+=2;
        if(a2<0) break;
        if(bp->dist[a2]<0) { /* for each atom not yet sampled... */
          bp->dist[a2]=b_cnt;
          bp->list[bp->n_atom]=a2;
          bp->n_atom++;
        }
      }
    }
  }
  return(bp->n_atom);
}
/*========================================================================*/
int ***ObjectMoleculeGetBondPrint(ObjectMolecule *I,int max_bond,int max_type,int *dim)
{
  int a,b,i,c;
  int at1,at2;
  int ***result=NULL;
  ObjectMoleculeBPRec bp;

  dim[0]=max_type+1;
  dim[1]=max_type+1;
  dim[2]=max_bond+1;
  
  result=(int***)UtilArrayMalloc((unsigned int*)dim,3,sizeof(int));
  UtilZeroMem(**result,dim[0]*dim[1]*dim[2]*sizeof(int));
  
  ObjectMoleculeInitBondPath(I,&bp);
  for(a=0;a<I->NAtom;a++) {
    at1 = I->AtomInfo[a].customType;
    if((at1>=0)&&(at1<=max_type)) {
      ObjectMoleculeGetBondPaths(I,a,max_bond,&bp);    
      for(b=0;b<bp.n_atom;b++)
        {
          i = bp.list[b];
          at2 = I->AtomInfo[i].customType;
          if((at2>=0)&&(at2<=max_type)) {
            c=bp.dist[i];
            result[at1][at2][c]++;
          }
        }
    }
  }
  ObjectMoleculePurgeBondPath(I,&bp);
  return(result);
}
/*========================================================================*/
float ObjectMoleculeGetAvgHBondVector(ObjectMolecule *I,int atom,int state,float *v)
     /* computes average hydrogen bonding vector for an atom */
{
  float result = 0.0;
  int a1,a2,n;
  int vec_cnt = 0;
  float v_atom[3],v_neigh[3],v_diff[3],v_acc[3] = {0.0,0.0,0.0};
  CoordSet *cs;

  
  ObjectMoleculeUpdateNeighbors(I);

  a1 = atom;
  if(state<0) state=0;
  if(I->NCSet==1) state=0;
  state = state % I->NCSet;
  cs = I->CSet[state];
  if(cs) {
    if(CoordSetGetAtomVertex(cs,a1,v_atom)) { /* atom exists in this C-set */
      n=I->Neighbor[atom];
      n++;
      while(1) {
        a2=I->Neighbor[n];
        if(a2<0) break;
        n+=2;
        
        if(I->AtomInfo[a2].elem[0]!='H') { /* ignore hydrogens */
          if(CoordSetGetAtomVertex(cs,a2,v_neigh)) { 
            subtract3f(v_atom,v_neigh,v_diff);
            normalize3f(v_diff);
            add3f(v_diff,v_acc,v_acc);
            vec_cnt++;
          }
        }
      }
      if(vec_cnt) {
        result = length3f(v_acc);
        result = result/vec_cnt;
        normalize23f(v_acc,v);
      }
      copy3f(v_acc,v);
    }
  }
  return(result);
}
/*========================================================================*/
int ObjectMoleculeGetAtomVertex(ObjectMolecule *I,int state,int index,float *v)
{
  int result = 0;
  if(state<0) state=0;
  if(I->NCSet==1) state=0;
  state = state % I->NCSet;
  if(I->CSet[state]) 
    result = CoordSetGetAtomVertex(I->CSet[state],index,v);
  return(result);
}
/*========================================================================*/
int ObjectMoleculeSetAtomVertex(ObjectMolecule *I,int state,int index,float *v)
{
  int result = 0;
  if(state<0) state=0;
  if(I->NCSet==1) state=0;
  state = state % I->NCSet;
  if(I->CSet[state]) 
    result = CoordSetSetAtomVertex(I->CSet[state],index,v);
  return(result);
}
/*========================================================================*/
void ObjectMoleculeRender(ObjectMolecule *I,int state,CRay *ray,Pickable **pick,int pass)
{
  int a;

  PRINTFD(FB_ObjectMolecule)
    " ObjectMolecule: rendering %s...\n",I->Obj.Name
    ENDFD;

  ObjectPrepareContext(&I->Obj,ray);

  if(I->UnitCellCGO&&(I->Obj.RepVis[cRepCell])) {
    if(ray) {
      
      CGORenderRay(I->UnitCellCGO,ray,ColorGet(I->Obj.Color),
                         I->Obj.Setting,NULL);
    } else if(pick&&PMGUI) {
    } else if(PMGUI) {
      ObjectUseColor(&I->Obj);
      CGORenderGL(I->UnitCellCGO,ColorGet(I->Obj.Color),
                         I->Obj.Setting,NULL);
    }
  }

  PRINTFD(FB_ObjectMolecule)
    " ObjectMolecule: CGO's complete...\n"
    ENDFD;

  if(state<0) {
    for(a=0;a<I->NCSet;a++)
      if(I->CSet[a])
        if(I->CSet[a]->fRender)
          I->CSet[a]->fRender(I->CSet[a],ray,pick,pass);        
  } else if(state<I->NCSet) {
	 I->CurCSet=state % I->NCSet;
	 if(I->CSet[I->CurCSet]) {
      if(I->CSet[I->CurCSet]->fRender)
        I->CSet[I->CurCSet]->fRender(I->CSet[I->CurCSet],ray,pick,pass);
	 }
  } else if(I->NCSet==1) { /* if only one coordinate set, assume static */
    if(SettingGet(cSetting_static_singletons))
      if(I->CSet[0]->fRender)
        I->CSet[0]->fRender(I->CSet[0],ray,pick,pass);    
  }
  PRINTFD(FB_ObjectMolecule)
    " ObjectMolecule: rendering complete for object %s.\n",I->Obj.Name
    ENDFD;
}
/*========================================================================*/
ObjectMolecule *ObjectMoleculeNew(int discreteFlag)
{
  int a;
  OOAlloc(ObjectMolecule);
  ObjectInit((CObject*)I);
  I->Obj.type=cObjectMolecule;
  I->NAtom=0;
  I->NBond=0;
  I->CSet=VLAMalloc(10,sizeof(CoordSet*),5,true); /* auto-zero */
  I->NCSet=0;
  I->Bond=NULL;
  I->AtomCounter=-1;
  I->BondCounter=-1;
  I->DiscreteFlag=discreteFlag;
  I->UnitCellCGO=NULL;
  I->Sculpt=NULL;
  I->CSTmpl=NULL;
  if(I->DiscreteFlag) { /* discrete objects don't share atoms between states */
    I->DiscreteAtmToIdx = VLAMalloc(10,sizeof(int),6,false);
    I->DiscreteCSet = VLAMalloc(10,sizeof(CoordSet*),5,false);
    I->NDiscrete=0;
  } else {
    I->DiscreteAtmToIdx = NULL;
    I->DiscreteCSet = NULL;
  }    
  I->Obj.fRender=(void (*)(struct CObject *, int, CRay *, Pickable **,int))ObjectMoleculeRender;
  I->Obj.fFree= (void (*)(struct CObject *))ObjectMoleculeFree;
  I->Obj.fUpdate=  (void (*)(struct CObject *)) ObjectMoleculeUpdate;
  I->Obj.fGetNFrame = (int (*)(struct CObject *)) ObjectMoleculeGetNFrames;
  I->Obj.fDescribeElement = (void (*)(struct CObject *,int index,char *buffer)) ObjectMoleculeDescribeElement;
  I->Obj.fGetSettingHandle = (CSetting **(*)(struct CObject *,int state))
    ObjectMoleculeGetSettingHandle;
  I->AtomInfo=VLAMalloc(10,sizeof(AtomInfoType),2,true); /* autozero here is important */
  I->CurCSet=0;
  I->Symmetry=NULL;
  I->Neighbor=NULL;
  for(a=0;a<=cUndoMask;a++) {
    I->UndoCoord[a]=NULL;
    I->UndoState[a]=-1;
  }
  I->UndoIter=0;
  return(I);
}
/*========================================================================*/
ObjectMolecule *ObjectMoleculeCopy(ObjectMolecule *obj)
{
  int a;
  BondType *i0,*i1;
  AtomInfoType *a0,*a1;
  OOAlloc(ObjectMolecule);
  (*I)=(*obj);
  I->Symmetry=SymmetryCopy(I->Symmetry); /* null-safe */
  I->UnitCellCGO=NULL;
  I->Neighbor=NULL;
  I->Sculpt=NULL;
  for(a=0;a<=cUndoMask;a++)
    I->UndoCoord[a]=NULL;
  I->CSet=VLAMalloc(I->NCSet,sizeof(CoordSet*),5,true); /* auto-zero */
  for(a=0;a<I->NCSet;a++) {
    I->CSet[a]=CoordSetCopy(obj->CSet[a]);
    I->CSet[a]->Obj=I;
  }
  if(obj->CSTmpl)
    I->CSTmpl = CoordSetCopy(obj->CSTmpl);
  else
    I->CSTmpl=NULL;
  I->Bond=VLAlloc(BondType,I->NBond);
  i0=I->Bond;
  i1=obj->Bond;
  for(a=0;a<I->NBond;a++) {
    *(i0++)=*(i1++); /* copy structure */
  }
  
  I->AtomInfo=VLAlloc(AtomInfoType,I->NAtom);
  a0=I->AtomInfo;
  a1=obj->AtomInfo;
  for(a=0;a<I->NAtom;a++)
    *(a0++)=*(a1++);

  for(a=0;a<I->NAtom;a++) {
    I->AtomInfo[a].selEntry=0;
  }
  
  return(I);

}

/*========================================================================*/
void ObjectMoleculeFree(ObjectMolecule *I)
{
  int a;
  SceneObjectDel((CObject*)I);

  for(a=0;a<I->NCSet;a++)
	 if(I->CSet[a]) {
      if(I->CSet[a]->fFree)
        I->CSet[a]->fFree(I->CSet[a]);
		I->CSet[a]=NULL;
	 }
  if(I->Symmetry) SymmetryFree(I->Symmetry);
  VLAFreeP(I->Neighbor);
  VLAFreeP(I->DiscreteAtmToIdx);
  VLAFreeP(I->DiscreteCSet);
  VLAFreeP(I->CSet);
  VLAFreeP(I->AtomInfo);
  VLAFreeP(I->Bond);
  if(I->UnitCellCGO) 
    CGOFree(I->UnitCellCGO);
  for(a=0;a<=cUndoMask;a++)
    FreeP(I->UndoCoord[a]);
  if(I->Sculpt)
    SculptFree(I->Sculpt);
  if(I->CSTmpl)
    if(I->CSTmpl->fFree)
      I->CSTmpl->fFree(I->CSTmpl);
  ObjectPurge(&I->Obj);
  OOFreeP(I);
}

/*========================================================================*/
int ObjectMoleculeConnect(ObjectMolecule *I,BondType **bond,AtomInfoType *ai,
                          CoordSet *cs,int bondSearchFlag)
{
  #define cMULT 1

  int a,b,c,d,e,f,i,j;
  int a1,a2;
  float *v1,*v2,dst;
  int maxBond;
  MapType *map;
  int nBond;
  BondType *ii1,*ii2;
  int flag;
  int order;
  AtomInfoType *ai1,*ai2;
  float cutoff_s;
  float cutoff_h;
  float cutoff_v;
  float cutoff;
  float max_cutoff;
  int water_flag;

  cutoff_v=SettingGet(cSetting_connect_cutoff);
  cutoff_s=cutoff_v + 0.2;
  cutoff_h=cutoff_v - 0.2;
  max_cutoff = cutoff_s;

  /*  FeedbackMask[FB_ObjectMolecule]=0xFF;*/
  nBond = 0;
  maxBond = cs->NIndex * 8;
  (*bond) = VLAlloc(BondType,maxBond);
  if(cs->NIndex&&bondSearchFlag) /* &&(!I->DiscreteFlag) WLD 010527 */
	 {
      switch((int)SettingGet(cSetting_connect_mode)) {
      case 0:
        /* distance-based bond location  */

      map=MapNew(max_cutoff+MAX_VDW,cs->Coord,cs->NIndex,NULL);
      if(map)
        {
          for(i=0;i<cs->NIndex;i++)
            {
              v1=cs->Coord+(3*i);
              MapLocus(map,v1,&a,&b,&c);
              for(d=a-1;d<=a+1;d++)
                for(e=b-1;e<=b+1;e++)
                  for(f=c-1;f<=c+1;f++)
                    {
                      j = *(MapFirst(map,d,e,f));
                      while(j>=0)
                        {
                          if(i<j)
                            {
                              v2 = cs->Coord + (3*j);
                              dst = diff3f(v1,v2);										
                              
                              a1=cs->IdxToAtm[i];
                              a2=cs->IdxToAtm[j];

                              ai1=ai+a1;
                              ai2=ai+a2;
                                    
                              dst -= ((ai1->vdw+ai2->vdw)/2);

                              /* quick hack for water detection.  
                                 they don't usually don't have CONECT records 
                                 and may not be HETATMs though they are supposed to be... */
                              
                              water_flag=false;
                              if((ai1->resn[0]=='W')&&
                                 (ai1->resn[1]=='A')&&
                                 (ai1->resn[2]=='T')&&
                                 (!ai1->resn[3]))
                                water_flag=true;
                              else if((ai1->resn[0]=='H')&&
                                      (ai1->resn[1]=='O')&&
                                      (ai1->resn[2]=='H')&&
                                      (!ai1->resn[3]))
                                water_flag=true;
                              if((ai2->resn[0]=='W')&&
                                 (ai2->resn[1]=='A')&&
                                 (ai2->resn[2]=='T')&&
                                 (!ai2->resn[3]))
                                water_flag=true;
                              else if((ai2->resn[0]=='H')&&
                                      (ai2->resn[1]=='O')&&
                                      (ai2->resn[2]=='H')&&
                                      (!ai2->resn[3]))
                                water_flag=true;
                              
                              cutoff = cutoff_h;
                              
                              /* workaround for hydrogens and sulfurs... */
                              
                              if(ai1->hydrogen||ai2->hydrogen)
                                cutoff = cutoff_h;
                              else if(((ai1->elem[0]=='S')&&(!ai1->elem[1]))||
                                   ((ai2->elem[0]=='S')&&(!ai2->elem[1])))
                                cutoff = cutoff_s;
                              else
                                cutoff = cutoff_v;
                              if( (dst <= cutoff)&&
                                  (!(ai1->hydrogen&&ai2->hydrogen))&&
                                  (water_flag||(!cs->TmpBond)||(!(ai1->hetatm&&ai2->hetatm))))
                                {
                                  flag=true;
                                  if(ai1->alt[0]!=ai2->alt[0]) { /* handle alternate conformers */
                                    if(ai1->alt[0]&&ai2->alt[0])
                                        flag=false; /* don't connect atoms with different, non-NULL
                                                       alternate conformations */
                                  } else if(ai1->alt[0]&&ai2->alt[0])
                                    if(!AtomInfoSameResidue(ai1,ai2))
                                      flag=false; /* don't connect non-NULL, alt conformations in 
                                                     different residues */
                                  if(ai1->alt[0]||ai2->alt[0]) 
                                  if(water_flag) /* hack to clean up water bonds */
                                    if(!AtomInfoSameResidue(ai1,ai2))
                                      flag=false;
                                      
                                  if(flag) {
                                    ai1->bonded=true;
                                    ai2->bonded=true;
                                    VLACheck((*bond),BondType,nBond);
                                    (*bond)[nBond].index[0] = a1;
                                    (*bond)[nBond].index[1] = a2;
                                    (*bond)[nBond].stereo = 0;
                                    order = 1;
                                    if((!ai1->hetatm)&&(!ai1->resn[3])) { /* Standard PDB residue */
                                      if(AtomInfoSameResidue(ai1,ai2)) {
                                        /* nasty high-speed hack to get bond valences and formal charges 
                                           for standard residues */
                                        if(((!ai1->name[1])&&(!ai2->name[1]))&&
                                           (((ai1->name[0]=='C')&&(ai2->name[0]=='O'))||
                                            ((ai1->name[0]=='O')&&(ai2->name[0]=='C')))) {
                                          order=2;
                                        } else {
                                          switch(ai1->resn[0]) {
                                          case 'A':
                                            switch(ai1->resn[1]) {
                                            case 'R': /* ARG */
                                              if(!strcmp(ai1->name,"NH1")) 
                                                ai1->formalCharge=1;
                                              else if(!strcmp(ai2->name,"NH1")) 
                                                ai2->formalCharge=1;
                                              if(((!strcmp(ai1->name,"CZ"))&&(!strcmp(ai2->name,"NH1")))||
                                                 ((!strcmp(ai2->name,"CZ"))&&(!strcmp(ai1->name,"NH1")))) 
                                                order=2;
                                              break;
                                            case 'S': 
                                              switch(ai1->resn[2]) {
                                              case 'P': /* ASP */
                                                if(!strcmp(ai1->name,"OD2")) 
                                                  ai1->formalCharge=-1;
                                                else if(!strcmp(ai2->name,"OD2")) 
                                                  ai2->formalCharge=-1;
                                              case 'N': /* ASN or ASP */
                                                if(((!strcmp(ai1->name,"CG"))&&(!strcmp(ai2->name,"OD1")))||
                                                   ((!strcmp(ai2->name,"CG"))&&(!strcmp(ai1->name,"OD1")))) 
                                                  order=2;
                                                break;
                                              }
                                            }
                                          case 'G':
                                            switch(ai1->resn[1]) {
                                            case 'L': 
                                              switch(ai1->resn[2]) {
                                              case 'U': /* GLU */
                                                if(!strcmp(ai1->name,"OE2")) 
                                                  ai1->formalCharge=-1;
                                                else if(!strcmp(ai2->name,"OE2")) 
                                                  ai2->formalCharge=-1;
                                              case 'N': /* GLN or GLU */
                                                if(((!strcmp(ai1->name,"CD"))&&(!strcmp(ai2->name,"OE1")))||
                                                   ((!strcmp(ai2->name,"CD"))&&(!strcmp(ai1->name,"OE1")))) 
                                                  order=2;
                                                break;
                                              }
                                            }
                                            break;
                                          case 'H':
                                            switch(ai1->resn[1]) {
                                            case 'I':
                                              switch(ai1->resn[2]) {
                                              case 'P':
                                                if(!strcmp(ai1->name,"ND1")) 
                                                  ai1->formalCharge=1;
                                                else if(!strcmp(ai2->name,"ND1")) 
                                                  ai2->formalCharge=1;
                                              case 'S':
                                              case 'E':
                                                if(((!strcmp(ai1->name,"CG"))&&(!strcmp(ai2->name,"CD2")))||
                                                   ((!strcmp(ai2->name,"CG"))&&(!strcmp(ai1->name,"CD2")))) 
                                                  order=2;
                                                else if(((!strcmp(ai1->name,"CE1"))&&(!strcmp(ai2->name,"ND1")))||
                                                        ((!strcmp(ai2->name,"CE1"))&&(!strcmp(ai1->name,"ND1")))) 
                                                  order=2;
                                                break;
                                                break;
                                              case 'D':
                                                if(((!strcmp(ai1->name,"CG"))&&(!strcmp(ai2->name,"CD2")))||
                                                   ((!strcmp(ai2->name,"CG"))&&(!strcmp(ai1->name,"CD2")))) 
                                                  order=2;
                                                else if(((!strcmp(ai1->name,"CE1"))&&(!strcmp(ai2->name,"NE2")))||
                                                        ((!strcmp(ai2->name,"CE1"))&&(!strcmp(ai1->name,"NE2")))) 
                                                  order=2;
                                                break;
                                              }
                                              break;
                                            }
                                            break;
                                          case 'P':
                                            switch(ai1->resn[1]) {
                                            case 'H': /* PHE */
                                              if(ai1->resn[2]=='E') {
                                                if(((!strcmp(ai1->name,"CG"))&&(!strcmp(ai2->name,"CD1")))||
                                                   ((!strcmp(ai2->name,"CG"))&&(!strcmp(ai1->name,"CD1")))) 
                                                  order=2;
                                                else if(((!strcmp(ai1->name,"CZ"))&&(!strcmp(ai2->name,"CE1")))||
                                                        ((!strcmp(ai2->name,"CZ"))&&(!strcmp(ai1->name,"CE1")))) 
                                                  order=2;
                                                
                                                else if(((!strcmp(ai1->name,"CE2"))&&(!strcmp(ai2->name,"CD2")))||
                                                        ((!strcmp(ai2->name,"CE2"))&&(!strcmp(ai1->name,"CD2")))) 
                                                  order=2;
                                                break; 
                                              }
                                            }
                                            break;
                                          case 'L':
                                            if(!strcmp(ai1->name,"NZ")) 
                                              ai1->formalCharge=1;
                                            else if(!strcmp(ai2->name,"NZ")) 
                                              ai2->formalCharge=1;
                                            break;
                                          case 'T':
                                            switch(ai1->resn[1]) {
                                            case 'Y': /* TYR */
                                              if(ai1->resn[2]=='R') {
                                                if(((!strcmp(ai1->name,"CG"))&&(!strcmp(ai2->name,"CD1")))||
                                                   ((!strcmp(ai2->name,"CG"))&&(!strcmp(ai1->name,"CD1")))) 
                                                  order=2;
                                                else if(((!strcmp(ai1->name,"CZ"))&&(!strcmp(ai2->name,"CE1")))||
                                                        ((!strcmp(ai2->name,"CZ"))&&(!strcmp(ai1->name,"CE1")))) 
                                                  order=2;
                                                
                                                else if(((!strcmp(ai1->name,"CE2"))&&(!strcmp(ai2->name,"CD2")))||
                                                        ((!strcmp(ai2->name,"CE2"))&&(!strcmp(ai1->name,"CD2")))) 
                                                  order=2;
                                                break; 
                                              }
                                              break;
                                            case 'R':
                                              if(ai1->resn[2]=='P') {
                                                if(((!strcmp(ai1->name,"CG"))&&(!strcmp(ai2->name,"CD1")))||
                                                   ((!strcmp(ai2->name,"CG"))&&(!strcmp(ai1->name,"CD1")))) 
                                                  order=2;
                                                else if(((!strcmp(ai1->name,"CZ3"))&&(!strcmp(ai2->name,"CE3")))||
                                                        ((!strcmp(ai2->name,"CZ3"))&&(!strcmp(ai1->name,"CE3")))) 
                                                  order=2;
                                                else if(((!strcmp(ai1->name,"CZ2"))&&(!strcmp(ai2->name,"CH2")))||
                                                        ((!strcmp(ai2->name,"CZ2"))&&(!strcmp(ai1->name,"CH2")))) 
                                                  order=2;
                                                else if(((!strcmp(ai1->name,"CE2"))&&(!strcmp(ai2->name,"CD2")))||
                                                        ((!strcmp(ai2->name,"CE2"))&&(!strcmp(ai1->name,"CD2")))) 
                                                  order=2;
                                                break; 
                                              }

                                              break;
                                            }
                                            
                                          }
                                        }
                                      }
                                    }
                                    (*bond)[nBond].order = order;
                                    nBond++;
                                  }
                                }
                            }
                          j=MapNext(map,j);
                        }
                    }
            }
          MapFree(map);
        case 1: /* dictionary-based connectivity */
          /* TODO */
          
          break;
        }
      }
      PRINTFB(FB_ObjectMolecule,FB_Blather)
        " ObjectMoleculeConnect: Found %d bonds.\n",nBond
        ENDFB;
      if(Feedback(FB_ObjectMolecule,FB_Debugging)) {
        for(a=0;a<nBond;a++)
          printf(" ObjectMoleculeConnect: bond %d ind0 %d ind1 %d\n",
                 a,(*bond)[a].index[0],(*bond)[a].index[1]);
      }
    }

  if(cs->NTmpBond&&cs->TmpBond) {
      PRINTFB(FB_ObjectMolecule,FB_Blather) 
      " ObjectMoleculeConnect: incorporating explicit bonds. %d %d\n",
             nBond,cs->NTmpBond
        ENDFB;
    VLACheck((*bond),BondType,(nBond+cs->NTmpBond));
    ii1=(*bond)+nBond;
    ii2=cs->TmpBond;
    for(a=0;a<cs->NTmpBond;a++)
      {
        a1 = cs->IdxToAtm[ii2->index[0]]; /* convert bonds from index space */
        a2 = cs->IdxToAtm[ii2->index[1]]; /* to atom space */
        ai[a1].bonded=true;
        ai[a2].bonded=true;
        ii1->index[0]=a1;
        ii1->index[1]=a2;
        ii1->order = ii2->order;
        ii1->stereo = ii2->stereo;
        ii1++;
        ii2++;

      }
    nBond=nBond+cs->NTmpBond;
    VLAFreeP(cs->TmpBond);
    cs->NTmpBond=0;
  }

  if(cs->NTmpLinkBond&&cs->TmpLinkBond) {
    PRINTFB(FB_ObjectMolecule,FB_Blather) 
      "ObjectMoleculeConnect: incorporating linkage bonds. %d %d\n",
      nBond,cs->NTmpLinkBond
      ENDFB;
    VLACheck((*bond),BondType,(nBond+cs->NTmpLinkBond));
    ii1=(*bond)+nBond;
    ii2=cs->TmpLinkBond;
    for(a=0;a<cs->NTmpLinkBond;a++)
      {
        a1 = ii2->index[0]; /* first atom is in object */
        a2 = cs->IdxToAtm[ii2->index[1]]; /* second is in the cset */
        ai[a1].bonded=true;
        ai[a2].bonded=true;
        ii1->index[0]=a1;
        ii1->index[1]=a2;
        ii1->order = ii2->order;
        ii1->stereo = ii2->stereo;
        ii1++;
        ii2++;
      }
    nBond=nBond+cs->NTmpLinkBond;
    VLAFreeP(cs->TmpLinkBond);
    cs->NTmpLinkBond=0;
  }

  PRINTFD(FB_ObjectMolecule)
    " ObjectMoleculeConnect: elminating duplicates with %d bonds...\n",nBond
    ENDFD;

  if(!I->DiscreteFlag) {
    UtilSortInPlace((*bond),nBond,sizeof(BondType),(UtilOrderFn*)BondInOrder);
    if(nBond) { /* eliminate duplicates */
      ii1=(*bond)+1;
      ii2=(*bond)+1;
      a=nBond-1;
      nBond=1;
      if(a>0) 
        while(a--) { 
          if((ii2->index[0]!=(ii1-1)->index[0])||
             (ii2->index[1]!=(ii1-1)->index[1])) {
            *(ii1++)=*(ii2++); /* copy bond */
            nBond++;
          } else {
            ii2++; /* skip bond */
          }
        }
      VLASize((*bond),BondType,nBond);
    }
  }
  PRINTFD(FB_ObjectMolecule)
    " ObjectMoleculeConnect: leaving with %d bonds...\n",nBond
    ENDFD;
  return(nBond);
}

/*========================================================================*/
ObjectMolecule *ObjectMoleculeReadMMDStr(ObjectMolecule *I,char *MMDStr,int frame,int discrete)
{
  int ok = true;
  CoordSet *cset=NULL;
  AtomInfoType *atInfo;
  int isNew;
  int nAtom;

  if(!I) 
	 isNew=true;
  else 
	 isNew=false;

  if(isNew) {
    I=(ObjectMolecule*)ObjectMoleculeNew(discrete);
    atInfo = I->AtomInfo;
  } else {
    atInfo=VLAMalloc(10,sizeof(AtomInfoType),2,true); /* autozero here is important */
  }
  
  cset=ObjectMoleculeMMDStr2CoordSet(MMDStr,&atInfo);  

  if(!cset) 
	 {
		VLAFreeP(atInfo);
		ok=false;
	 }
  
  if(ok)
	 {
		if(!I) 
		  I=(ObjectMolecule*)ObjectMoleculeNew(discrete);
		if(frame<0)
		  frame=I->NCSet;
		if(I->NCSet<=frame)
		  I->NCSet=frame+1;
		VLACheck(I->CSet,CoordSet*,frame);
      nAtom=cset->NIndex;
      cset->Obj = I;
      if(cset->fEnumIndices)
        cset->fEnumIndices(cset);
      if(cset->fInvalidateRep)
        cset->fInvalidateRep(cset,cRepAll,cRepInvRep);
      if(isNew) {		
        I->AtomInfo=atInfo; /* IMPORTANT to reassign: this VLA may have moved! */
        I->NAtom=nAtom;
      } else {
        ObjectMoleculeMerge(I,atInfo,cset,false,cAIC_MMDMask); /* NOTE: will release atInfo */
      }
      if(frame<0) frame=I->NCSet;
      VLACheck(I->CSet,CoordSet*,frame);
      if(I->NCSet<=frame) I->NCSet=frame+1;
      I->CSet[frame] = cset;
      if(isNew) I->NBond = ObjectMoleculeConnect(I,&I->Bond,I->AtomInfo,cset,false);
      SceneCountFrames();
      ObjectMoleculeExtendIndices(I);
      ObjectMoleculeSort(I);
      ObjectMoleculeUpdateIDNumbers(I);
      ObjectMoleculeUpdateNonbonded(I);
	 }
  return(I);
}
/*========================================================================*/
ObjectMolecule *ObjectMoleculeLoadMMDFile(ObjectMolecule *obj,char *fname,
                                          int frame,char *sepPrefix,int discrete)
{
  ObjectMolecule* I=NULL;
  int ok=true;
  FILE *f;
  int oCnt=0;
  long size;
  char *buffer,*p;
  char cc[MAXLINELEN],oName[ObjNameMax];
  int nLines;
  f=fopen(fname,"rb");
  if(!f)
	 ok=ErrMessage("ObjectMoleculeLoadMMDFile","Unable to open file!");
  else
	 {
      PRINTFB(FB_ObjectMolecule,FB_Blather)
        " ObjectMoleculeLoadMMDFile: Loading from %s.\n",fname
        ENDFB;
		fseek(f,0,SEEK_END);
      size=ftell(f);
		fseek(f,0,SEEK_SET);
		buffer=(char*)mmalloc(size+255);
		ErrChkPtr(buffer);
		p=buffer;
		fseek(f,0,SEEK_SET);
		fread(p,size,1,f);
		p[size]=0;
		fclose(f);
      p=buffer;
      while(ok) {
        ncopy(cc,p,6);
        if(sscanf(cc,"%d",&nLines)!=1)
          break;
        if(ok) {
          if(sepPrefix) {
            I=ObjectMoleculeReadMMDStr(NULL,p,frame,discrete);
            oCnt++;
            sprintf(oName,"%s-%02d",sepPrefix,oCnt);
            ObjectSetName((CObject*)I,oName);
            ExecutiveManageObject((CObject*)I,true);
          } else {
            I=ObjectMoleculeReadMMDStr(obj,p,frame,discrete);
            obj=I;
          }
          p=nextline(p);
          while(nLines--)
            p=nextline(p);
        }
      }
		mfree(buffer);
	 }

  return(I);
}

/*========================================================================*/
CoordSet *ObjectMoleculePDBStr2CoordSet(char *buffer,
                                        AtomInfoType **atInfoPtr,
                                        char **restart,
                                        char *segi_override)
{

  char *p;
  int nAtom;
  int a,b,c;
  float *coord = NULL;
  CoordSet *cset = NULL;
  AtomInfoType *atInfo = NULL,*ai;
  int AFlag;
  char SSCode;
  int atomCount;
  int conectFlag = false;
  BondType *bond=NULL,*ii1,*ii2;
  int *idx;
  int nBond=0;
  int b1,b2,nReal,maxAt;
  CSymmetry *symmetry = NULL;
  int symFlag;
  int auto_show_lines = SettingGet(cSetting_auto_show_lines);
  int auto_show_nonbonded = SettingGet(cSetting_auto_show_nonbonded);
  int literal_names = SettingGet(cSetting_pdb_literal_names);
  int newModelFlag = false;
  int ssFlag = false;
  int ss_resv1=0,ss_resv2=0;
  ResIdent ss_resi1="",ss_resi2="";
  unsigned char ss_chain1=0,ss_chain2=0;
  SSEntry *ss_list = NULL;
  int n_ss = 1;
  int *(ss[256]); /* one array for each chain identifier */
  
  char cc[MAXLINELEN];
  char cc_saved,ctmp;
  int index;
  int ignore_pdb_segi = 0;
  int ss_valid;
  SSEntry *sst;
  int ssi = 0;

  ignore_pdb_segi = SettingGet(cSetting_ignore_pdb_segi);
  AtomInfoPrimeColors();

  p=buffer;
  nAtom=0;
  if(atInfoPtr)
	 atInfo = *atInfoPtr;

  if(!atInfo)
    ErrFatal("PDBStr2CoordSet","need atom information record!"); /* failsafe for old version..*/

  *restart = NULL;
  while(*p)
	 {
		if((*p == 'A')&&(*(p+1)=='T')&&(*(p+2)=='O')&&(*(p+3)=='M')
         &&(!*restart))
		  nAtom++;
		else if((*p == 'H')&&(*(p+1)=='E')&&(*(p+2)=='L')&&(*(p+3)=='I')&&(*(p+4)=='X'))
        ssFlag=true;
		else if((*p == 'S')&&(*(p+1)=='H')&&(*(p+2)=='E')&&(*(p+3)=='E')&&(*(p+4)=='T'))
        ssFlag=true;
		else if((*p == 'A')&&(*(p+1)=='T')&&(*(p+2)=='O')&&(*(p+3)=='M')
         &&(!*restart))
		  nAtom++;
		else if((*p == 'H')&&(*(p+1)=='E')&&(*(p+2)=='T')&&
         (*(p+3)=='A')&&(*(p+4)=='T')&&(*(p+5)=='M')&&(!*restart))
        nAtom++;
		else if((*p == 'E')&&(*(p+1)=='N')&&(*(p+2)=='D')&&
         (*(p+3)=='M')&&(*(p+4)=='D')&&(*(p+5)=='L')&&(!*restart))
        *restart=nextline(p);
		else if((*p == 'C')&&(*(p+1)=='O')&&(*(p+2)=='N')&&
         (*(p+3)=='E')&&(*(p+4)=='C')&&(*(p+5)=='T'))
        conectFlag=true;
      p=nextline(p);
	 }

  *restart=NULL;
  coord=VLAlloc(float,3*nAtom);

  if(atInfo)
	 VLACheck(atInfo,AtomInfoType,nAtom);

  if(conectFlag) {
    nBond=0;
    bond=VLAlloc(BondType,6*nAtom);  
  }
  p=buffer;
  PRINTFB(FB_ObjectMolecule,FB_Blather)
	 " ObjectMoleculeReadPDB: Found %i atoms...\n",nAtom
    ENDFB;

  if(ssFlag) {
    for(a=0;a<=255;a++) {
      ss[a]=0;
    }
    ss_list=VLAlloc(SSEntry,50);
  }

  a=0; /* WATCHOUT */
  atomCount=0;

  while(*p)
	 {
		AFlag=false;
      SSCode=0;
		if((*p == 'A')&&(*(p+1)=='T')&&(*(p+2)=='O')&&(*(p+3)=='M'))
		  AFlag = 1;
		else if((*p == 'H')&&(*(p+1)=='E')&&(*(p+2)=='T')&&
         (*(p+3)=='A')&&(*(p+4)=='T')&&(*(p+5)=='M'))
        AFlag = 2;
		else if((*p == 'R')&&(*(p+1)=='E')&&(*(p+2)=='M')&&
         (*(p+3)=='A')&&(*(p+4)=='R')&&(*(p+5)=='K')&&
         (*(p+6)==' ')&&(*(p+7)=='2')&&(*(p+8)=='9')&&
         (*(p+9)=='0'))
        {
        }
		else if((*p == 'H')&&(*(p+1)=='E')&&(*(p+2)=='L')&&(*(p+3)=='I')&&(*(p+4)=='X'))
        {
          ss_valid=true;

          p=nskip(p,19);
          ss_chain1 = (*p);
          p=nskip(p,2);
          p=ncopy(cc,p,4);
          if(!sscanf(cc,"%s",ss_resi1)) ss_valid=false;
          if(!sscanf(cc,"%d",&ss_resv1)) ss_valid=false;

          p=nskip(p,6);
          ss_chain2 = (*p);
          p=nskip(p,2);
          p=ncopy(cc,p,4);

          if(!sscanf(cc,"%s",ss_resi2)) ss_valid=false;
          if(!sscanf(cc,"%d",&ss_resv2)) ss_valid=false;
    
          if(ss_valid) {
            PRINTFB(FB_ObjectMolecule,FB_Details)
              " ObjectMolecule: read HELIX %c %s %c %s\n",
              ss_chain1,ss_resi1,ss_chain2,ss_resi2
              ENDFB;
            SSCode='H';
          }

          if(ss_chain1==' ') ss_chain1=0;
          if(ss_chain2==' ') ss_chain2=0;          

        }
		else if((*p == 'S')&&(*(p+1)=='H')&&(*(p+2)=='E')&&(*(p+3)=='E')&&(*(p+4)=='T'))
        {
          ss_valid=true;
          p=nskip(p,21);
          ss_chain1 = (*p);
          p=nskip(p,1);
          p=ncopy(cc,p,4);
          if(!sscanf(cc,"%s",ss_resi1)) ss_valid=false;
          if(!sscanf(cc,"%d",&ss_resv1)) ss_valid=false;
          p=nskip(p,6);
          ss_chain2 = (*p);
          p=nskip(p,1);
          p=ncopy(cc,p,4);
          if(!sscanf(cc,"%s",ss_resi2)) ss_valid=false;
          if(!sscanf(cc,"%d",&ss_resv2)) ss_valid=false;
       
          if(ss_valid) {
            PRINTFB(FB_ObjectMolecule,FB_Details)
              " ObjectMolecule: read SHEET %c %s %c %s\n",
              ss_chain1,ss_resi1,ss_chain2,ss_resi2
              ENDFB;
            SSCode = 'S';
          }

          if(ss_chain1==' ') ss_chain1=0;
          if(ss_chain2==' ') ss_chain2=0;   

        }
		else if((*p == 'E')&&(*(p+1)=='N')&&(*(p+2)=='D')&&
         (*(p+3)=='M')&&(*(p+4)=='D')&&(*(p+5)=='L')&&(!*restart))
        *restart=nextline(p);
		else if((*p == 'C')&&(*(p+1)=='R')&&(*(p+2)=='Y')&&
              (*(p+3)=='S')&&(*(p+4)=='T')&&(*(p+5)=='1')&&(!*restart))
        {
          if(!symmetry) symmetry=SymmetryNew();          
          if(symmetry) {
            PRINTFB(FB_ObjectMolecule,FB_Details)
              " PDBStrToCoordSet: Attempting to read symmetry information\n"
              ENDFB;
            p=nskip(p,6);
            symFlag=true;
            p=ncopy(cc,p,9);
            if(sscanf(cc,"%f",&symmetry->Crystal->Dim[0])!=1) symFlag=false;
            p=ncopy(cc,p,9);
            if(sscanf(cc,"%f",&symmetry->Crystal->Dim[1])!=1) symFlag=false;
            p=ncopy(cc,p,9);
            if(sscanf(cc,"%f",&symmetry->Crystal->Dim[2])!=1) symFlag=false;
            p=ncopy(cc,p,7);
            if(sscanf(cc,"%f",&symmetry->Crystal->Angle[0])!=1) symFlag=false;
            p=ncopy(cc,p,7);
            if(sscanf(cc,"%f",&symmetry->Crystal->Angle[1])!=1) symFlag=false;
            p=ncopy(cc,p,7);
            if(sscanf(cc,"%f",&symmetry->Crystal->Angle[2])!=1) symFlag=false;
            p=nskip(p,1);
            p=ncopy(symmetry->SpaceGroup,p,10);
            UtilCleanStr(symmetry->SpaceGroup);
            p=ncopy(cc,p,4);
            if(sscanf(cc,"%d",&symmetry->PDBZValue)!=1) symmetry->PDBZValue=1;
            if(!symFlag) {
              ErrMessage("PDBStrToCoordSet","Error reading CRYST1 record\n");
              SymmetryFree(symmetry);
              symmetry=NULL;
            }
          }
        }
		else if((*p == 'C')&&(*(p+1)=='O')&&(*(p+2)=='N')&&
         (*(p+3)=='E')&&(*(p+4)=='C')&&(*(p+5)=='T'))
        {
          p=nskip(p,6);
          p=ncopy(cc,p,5);
          if(sscanf(cc,"%d",&b1)==1)
            while (1) {
              p=ncopy(cc,p,5);
              if(sscanf(cc,"%d",&b2)!=1)
                break;
              else {
                if((b1>=0)&&(b2>=0)) { /* IDs must be positive */
                  VLACheck(bond,BondType,nBond);
                  if(b1<=b2) {
                    bond[nBond].index[0]=b1; /* temporarily store the atom indexes */
                    bond[nBond].index[1]=b2;
                    bond[nBond].order=1;
                    bond[nBond].stereo=0;
                    
                  } else {
                    bond[nBond].index[0]=b2;
                    bond[nBond].index[1]=b1;
                    bond[nBond].order=1;
                    bond[nBond].stereo=0;
                  }
                  nBond++;
                }
              }
            }
        }
      
      if(SSCode) {
        
        /* pretty confusing how this works... the following efficient (i.e. array-based)
           secondary structure lookup even when there are multiple insertion codes
           and where there may be multiple SS records for the residue using different 
           insertion codes */

        if(!ss[ss_chain1]) { /* allocate new array for chain if necc. */
          ss[ss_chain1]=Calloc(int,cResvMask+1);
        }

        sst = NULL; 
        for(b=ss_resv1;b<=ss_resv2;b++) { /* iterate over all residues indicated */
          index = b & cResvMask;
          if(ss[ss_chain1][index]) sst = NULL; /* make a unique copy in the event of multiple entries for one resv */
          if(!sst) {
            VLACheck(ss_list,SSEntry,n_ss);
            ssi = n_ss++;
            sst = ss_list + ssi;
            sst->resv1 = ss_resv1;
            sst->resv2 = ss_resv2;
            sst->chain1 = ss_chain1;
            sst->chain2 = ss_chain2;
            sst->type=SSCode;
            strcpy(sst->resi1,ss_resi1);
            strcpy(sst->resi2,ss_resi2);
          }
          sst->next = ss[ss_chain1][index];
          ss[ss_chain1][index]=ssi;
          if(sst->next) sst = NULL; /* force another unique copy */
        }
        
        if(ss_chain2!=ss_chain1) { /* handle case where chains are not the same (undefined in PDB spec?) */
          if(!ss[ss_chain2]) {
            ss[ss_chain2]=Calloc(int,cResvMask+1);
          }
          sst = NULL; 
          for(b=ss_resv1;b<=ss_resv2;b++) { /* iterate over all residues indicated */
            index = b & cResvMask;
            if(ss[ss_chain2][index]) sst = NULL; /* make a unique copy in the event of multiple entries for one resv */
            if(!sst) {
              VLACheck(ss_list,SSEntry,n_ss);
              ssi = n_ss++;
              sst = ss_list + ssi;
              sst->resv1 = ss_resv1;
              sst->resv2 = ss_resv2;
              sst->chain1 = ss_chain1;
              sst->chain2 = ss_chain2;
              sst->type=SSCode;
              strcpy(sst->resi1,ss_resi1);
              strcpy(sst->resi2,ss_resi2);
            }
            sst->next = ss[ss_chain2][index];
            ss[ss_chain2][index]=ssi;
            if(sst->next) sst = NULL; /* force another unique copy */
          }
        }
      }
      if(AFlag&&(!*restart))
        {
          ai=atInfo+atomCount;
          
          p=nskip(p,6);
          p=ncopy(cc,p,5);
          if(!sscanf(cc,"%d",&ai->id)) ai->id=0;

          p=nskip(p,1);/* to 12 */
          p=ncopy(cc,p,4); 
          if(!sscanf(cc,"%s",ai->name)) 
            ai->name[0]=0;
          else {
            if(!literal_names) {
              if(ai->name[3])
                if((ai->name[0]>='0')&&(ai->name[0]<='9')) {
                  ctmp = ai->name[0];
                  ai->name[0]= ai->name[1];
                  ai->name[1]= ai->name[2];
                  ai->name[2]= ai->name[3];
                  ai->name[3]= ctmp;
                }
            }
          }
          
          p=ncopy(cc,p,1);
          if(*cc==32)
            ai->alt[0]=0;
          else {
            ai->alt[0]=*cc;
            ai->alt[1]=0;
          }

          p=ncopy(cc,p,3); 
          if(!sscanf(cc,"%s",ai->resn)) ai->resn[0]=0;

          p=nskip(p,1);
          p=ncopy(cc,p,1);
          if(*cc==' ')
            ai->chain[0]=0;
          else {
            ai->chain[0] = *cc;
            ai->chain[1] = 0;
          }

          p=ncopy(cc,p,5); /* we treat insertion records as part of the residue identifier */
          if(!sscanf(cc,"%s",ai->resi)) ai->resi[0]=0;
          if(!sscanf(cc,"%d",&ai->resv)) ai->resv=1;
          
          if(ssFlag) { /* get secondary structure information (if avail) */

            ss_chain1=ai->chain[0];
            index = ai->resv & cResvMask;
            if(ss[ss_chain1]) {
              ssi = ss[ss_chain1][index];
              while(ssi) {
                sst = ss_list + ssi; /* contains shared entry, or unique linked list for each residue */
                /*                printf("%d<=%d<=%d, %s<=%s<=%s ", 
                                  sst->resv1,ai->resv,sst->resv2,
                                  sst->resi1,ai->resi,sst->resi2);*/
                if(ai->resv>=sst->resv1)
                  if(ai->resv<=sst->resv2)
                    if((ai->resv!=sst->resv1)||(WordCompare(ai->resi,sst->resi1,true)>=0))
                      if((ai->resv!=sst->resv2)||(WordCompare(ai->resi,sst->resi2,true)<=0))
                        {
                          ai->ssType[0]=sst->type;
                          /*                          printf(" Y\n");*/
                          break;
                        }
                /*                printf(" N\n");*/
                ssi = sst->next;
              }
            }
            
          } else {
            ai->cartoon = cCartoon_tube;
          }
          p=nskip(p,3);
          p=ncopy(cc,p,8);
          sscanf(cc,"%f",coord+a);
          p=ncopy(cc,p,8);
          sscanf(cc,"%f",coord+(a+1));
          p=ncopy(cc,p,8);
          sscanf(cc,"%f",coord+(a+2));

          p=ncopy(cc,p,6);
          if(!sscanf(cc,"%f",&ai->q))
            ai->q=1.0;
          
          p=ncopy(cc,p,6);
          if(!sscanf(cc,"%f",&ai->b))
            ai->b=0.0;

          p=nskip(p,6);
          p=ncopy(cc,p,4);
          if(!ignore_pdb_segi) {
            if(!segi_override[0])
              {
                if(!sscanf(cc,"%s",ai->segi)) 
                  ai->segi[0]=0;
                else {
                  cc_saved=cc[3];
                  ncopy(cc,p,4); 
                  if((cc_saved=='1')&& /* atom ID overflow? (nonstandard use...)...*/
                     (cc[0]=='0')&& 
                     (cc[1]=='0')&&
                     (cc[2]=='0')&&
                     (cc[3]=='0')&&
                     atomCount) {
                    strcpy(segi_override,(ai-1)->segi);
                    strcpy(ai->segi,(ai-1)->segi);
                  }
                }
              } else {
                strcpy(ai->segi,segi_override);
              }
          } else {
            ai->segi[0]=0;
          }
          
          p=ncopy(cc,p,2);
          if(!sscanf(cc,"%s",ai->elem)) 
            ai->elem[0]=0;          
          else if(!(((ai->elem[0]>='a')&&(ai->elem[0]<='z'))|| /* don't get confused by PDB misuse */
                    ((ai->elem[0]>='A')&&(ai->elem[0]<='Z'))))
            ai->elem[0]=0;                      

          for(c=0;c<cRepCnt;c++) {
            ai->visRep[c] = false;
          }
          ai->visRep[cRepLine] = auto_show_lines; /* show lines by default */
          ai->visRep[cRepNonbonded] = auto_show_nonbonded; /* show lines by default */

          if(AFlag==1) 
            ai->hetatm=0;
          else {
            ai->hetatm=1;
            ai->flags=cAtomFlag_ignore;
          }
          
          AtomInfoAssignParameters(ai);
          ai->color=AtomInfoGetColor(ai);

          PRINTFD(FB_ObjectMolecule)
            "%s %s %s %s %8.3f %8.3f %8.3f %6.2f %6.2f %s\n",
                    ai->name,ai->resn,ai->resi,ai->chain,
                    *(coord+a),*(coord+a+1),*(coord+a+2),ai->b,ai->q,
                    ai->segi
            ENDFD;

			 a+=3;
			 atomCount++;
		  }
      p=nextline(p);
	 }
  if(conectFlag) {
    UtilSortInPlace(bond,nBond,sizeof(BondType),(UtilOrderFn*)BondInOrder);              
    if(nBond) {
      ii1=bond;
      ii2=bond+1;
      nReal=1;
      ii1->order=1;
      a=nBond-1;
      while(a) {
        if((ii1->index[0]==ii2->index[0])&&(ii1->index[1]==ii2->index[1])) {
          ii1->order++; /* count dup */
        } else {
          ii1++; /* non-dup, make copy */
          ii1->index[0]=ii2->index[0];
          ii1->index[1]=ii2->index[1];
          ii1->order=ii2->order;
          ii1->stereo=ii2->stereo;
          nReal++;
        }
        ii2++;
        a--;
      }

      nBond=nReal;
      /* now, find atoms we're looking for */

      /* determine ranges */
      maxAt=nAtom;
      ii1=bond;
      for(a=0;a<nBond;a++) {
        if(ii1->index[0]>maxAt) maxAt=ii1->index[0];
        if(ii1->index[1]>maxAt) maxAt=ii1->index[1];
        ii1++;
      }
      for(a=0;a<nAtom;a++) 
        if(maxAt<atInfo[a].id) maxAt=atInfo[a].id;
      /* build index */
      maxAt++;
      idx = Alloc(int,maxAt+1);
      for(a=0;a<maxAt;a++) {
        idx[a]=-1;
      }
      for(a=0;a<nAtom;a++)
        idx[atInfo[a].id]=a;
      /* convert indices to bonds */
      ii1=bond;
      ii2=bond;
      nReal=0;
      for(a=0;a<nBond;a++) {
        if((ii1->index[0]>=0)&&((ii1->index[1])>=0)) {
          if((idx[ii1->index[0]]>=0)&&(idx[ii1->index[1]]>=0)) { /* in case PDB file has bad bonds */
            ii2->index[0]=idx[ii1->index[0]];
            ii2->index[1]=idx[ii1->index[1]];
            if((ii2->index[0]>=0)&&(ii2->index[1]>=0)) {
              if(ii1->order<=2) ii2->order=1;
              else if(ii1->order<=4) ii2->order=2;
              else ii2->order=3;
              nReal++;
              ii2++;
            }
          }
        }
        ii1++;
      }
      nBond=nReal;
      FreeP(idx);
    }
  }
  PRINTFB(FB_ObjectMolecule,FB_Blather)
   " PDBStr2CoordSet: Read %d bonds from CONECT records (%p).\n",nBond,bond
    ENDFB;
  cset = CoordSetNew();
  cset->NIndex=nAtom;
  cset->Coord=coord;
  cset->TmpBond=bond;
  cset->NTmpBond=nBond;
  if(symmetry) cset->Symmetry=symmetry;
  if(atInfoPtr)
	 *atInfoPtr = atInfo;

  if(*restart) { /* if plan on need to reading another object, 
                    make sure there is another model to read...*/
    p=*restart;
    newModelFlag=false;
    while(*p) {
        
        if((*p == 'M')&&(*(p+1)=='O')&&(*(p+2)=='D')&&
           (*(p+3)=='E')&&(*(p+4)=='L')&&(*(p+5)==' ')) {
          newModelFlag=true;
          break;
        }
        if((*p == 'E')&&(*(p+1)=='N')&&(*(p+2)=='D')&&
           (*(p+3)=='M')&&(*(p+4)=='D')&&(*(p+5)=='L')) {
          newModelFlag=true;
          break;
        }
        p=nextline(p);
    }
    if(!newModelFlag) {
      *restart=NULL;
    }
  }

  if(ssFlag) {
    for(a=0;a<=255;a++) {
      FreeP(ss[a]);
    }
    VLAFreeP(ss_list);
  }
  return(cset);
}

/*========================================================================*/
CoordSet *ObjectMoleculeMMDStr2CoordSet(char *buffer,AtomInfoType **atInfoPtr)
{
  char *p;
  int nAtom,nBond;
  int a,c,bPart,bOrder;
  float *coord = NULL;
  CoordSet *cset = NULL;
  AtomInfoType *atInfo = NULL,*ai;
  char cc[MAXLINELEN];
  float *f;
  BondType *ii,*bond=NULL;
  int ok=true;
  int auto_show_lines = SettingGet(cSetting_auto_show_lines);
  int auto_show_nonbonded = SettingGet(cSetting_auto_show_nonbonded);
  AtomInfoPrimeColors();

  p=buffer;
  nAtom=0;
  if(atInfoPtr)
	 atInfo = *atInfoPtr;


  if(ok) {
	 p=ncopy(cc,p,6);
	 if(sscanf(cc,"%d",&nAtom)!=1)
		ok=ErrMessage("ReadMMDFile","bad atom count");
  }

  if(ok) {
	 coord=VLAlloc(float,3*nAtom);
	 if(atInfo)
		VLACheck(atInfo,AtomInfoType,nAtom);	 
  }

  if(!atInfo)
    ErrFatal("PDBStr2CoordSet","need atom information record!"); /* failsafe for old version..*/

  nBond=0;
  if(ok) {
	 bond=VLAlloc(BondType,6*nAtom);  
  }
  p=nextline(p);

  /* read coordinates and atom names */

  if(ok) { 
	 f=coord;
	 ii=bond;
	 for(a=0;a<nAtom;a++)
		{
        ai=atInfo+a;

        ai->id=a+1;
        if(ok) {
          p=ncopy(cc,p,4);
          if(sscanf(cc,"%d",&ai->customType)!=1) 
            ok=ErrMessage("ReadMMDFile","bad atom type");
        }
        if(ok) {
          if(ai->customType<=14) strcpy(ai->elem,"C");
          else if(ai->customType<=23) strcpy(ai->elem,"O");
          else if(ai->customType<=40) strcpy(ai->elem,"N");
          else if(ai->customType<=48) strcpy(ai->elem,"H");
          else if(ai->customType<=52) strcpy(ai->elem,"S");
          else if(ai->customType<=53) strcpy(ai->elem,"P");
          else if(ai->customType<=55) strcpy(ai->elem,"B");
          else if(ai->customType<=56) strcpy(ai->elem,"F");
          else if(ai->customType<=57) strcpy(ai->elem,"Cl");           
          else if(ai->customType<=58) strcpy(ai->elem,"Br");           
          else if(ai->customType<=59) strcpy(ai->elem,"I");           
          else if(ai->customType<=60) strcpy(ai->elem,"Si");           
          else if(ai->customType<=61) strcpy(ai->elem,"Du");           
          else if(ai->customType<=62) strcpy(ai->elem,"Z0");
          else if(ai->customType<=63) strcpy(ai->elem,"Lp");
          else strcpy(ai->elem,"?");
        }
        for(c=0;c<6;c++) {
          if(ok) {
            p=ncopy(cc,p,8);
            if(sscanf(cc,"%d%d",&bPart,&bOrder)!=2)
              ok=ErrMessage("ReadMMDFile","bad bond record");
            else {
              if(bPart&&bOrder&&(a<(bPart-1))) {
                nBond++;
                ii->index[0]=a;
                ii->index[1]=bPart-1;
                ii->order=bOrder;
                ii->stereo=0;
                ii->id=-1;
                ii++;
              }
            }
          }
        }
        if(ok) {
          p=ncopy(cc,p,12);
          if(sscanf(cc,"%f",f++)!=1)
            ok=ErrMessage("ReadMMDFile","bad coordinate");
        }
        if(ok) {
          p=ncopy(cc,p,12);
          if(sscanf(cc,"%f",f++)!=1)
            ok=ErrMessage("ReadMMDFile","bad coordinate");
        }
        if(ok) {
          p=ncopy(cc,p,12);
			 if(sscanf(cc,"%f",f++)!=1)
				ok=ErrMessage("ReadMMDFile","bad coordinate");
		  }
        if(ok) {
          p=nskip(p,1);
          p=ncopy(cc,p,5);
          if(sscanf(cc,"%d",&ai->resv)==1) {
            sprintf(ai->resi,"%d",ai->resv); /* range check...*/
          }
        }
        if(ok) {
          p=nskip(p,6);
          p=ncopy(cc,p,9);
			 if(sscanf(cc,"%f",&ai->partialCharge)!=1)
				ok=ErrMessage("ReadMMDFile","bad charge");
        }
        if(ok) {
          p=nskip(p,10);
          p=ncopy(cc,p,3);
          if(sscanf(cc,"%s",ai->resn)!=1)
            ai->resn[0]=0;
          ai->hetatm=true;
        }

        ai->segi[0]=0;
        ai->alt[0]=0;

        if(ok) {
          p=nskip(p,2);
          p=ncopy(ai->name,p,4);
          UtilCleanStr(ai->name);
          if(ai->name[0]==0) {
            strcpy(ai->name,ai->elem);
            sprintf(cc,"%02d",a+1);
            if((strlen(cc)+strlen(ai->name))>4)
              strcpy(ai->name,cc);
            else
              strcat(ai->name,cc);
          }

          for(c=0;c<cRepCnt;c++) {
            ai->visRep[c] = false;
          }
          ai->visRep[cRepLine] = auto_show_lines; /* show lines by default */
          ai->visRep[cRepNonbonded] = auto_show_nonbonded; /* show lines by default */

        }
        if(ok) {
          AtomInfoAssignParameters(ai);
          ai->color = AtomInfoGetColor(ai);
        }
        if(!ok)
          break;
        p=nextline(p);
      }
  }
  if(ok)
    VLASize(bond,BondType,nBond);
  if(ok) {
	 cset = CoordSetNew();
	 cset->NIndex=nAtom;
	 cset->Coord=coord;
	 cset->NTmpBond=nBond;
	 cset->TmpBond=bond;
  } else {
	 VLAFreeP(bond);
	 VLAFreeP(coord);
  }
  if(atInfoPtr)
	 *atInfoPtr = atInfo;
  return(cset);
}


#ifdef _NO_LONGER_USED
/*========================================================================*/
static char *nextline(char *p) {
  while(*p) {
	 if(*p==0xD) { /* Mac or PC */
		if(*(p+1)==0xA) /* PC */
		  p++;
		p++;
		break;
	 }
	 if(*p==0xA) /* Unix */
		{
		  p++;
		  break;
		}
	 p++;
  }
  return p;
}
/*========================================================================*/
static char *wcopy(char *q,char *p,int n) { /* word copy */
  while(*p) {
	 if(*p<=32) 
		p++;
	 else
		break;
  }
  while(*p) {
	 if(*p<=32)
		break;
	 if(!n)
		break;
	 if((*p==0xD)||(*p==0xA)) /* don't copy end of lines */
		break;
	 *(q++)=*(p++);
	 n--;
  }
  *q=0;
  return p;
}
/*========================================================================*/
static char *ncopy(char *q,char *p,int n) {  /* n character copy */
  while(*p) {
	 if(!n)
		break;
	 if((*p==0xD)||(*p==0xA)) /* don't copy end of lines */
		break;
	 *(q++)=*(p++);
	 n--;
  }
  *q=0;
  return p;
}
/*========================================================================*/
static char *nskip(char *p,int n) {  /* n character skip */
  while(*p) {
	 if(!n)
		break;
	 if((*p==0xD)||(*p==0xA)) /* stop at newlines */
		break;
    p++;
	 n--;
  }
  return p;
}

#endif
