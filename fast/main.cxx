#include "dataset.h"
#include "parallelfmm.h"
#ifdef VTK
#include "vtk.h"
#endif

int main() {
  int numBodies = 1000;
  IMAGES = 0;
  THETA = 0.6;
  Bodies bodies, jbodies;
  Cells cells, jcells;
  Dataset DATA;
  ParallelFMM FMM;
  FMM.printNow = MPIRANK == 0;
#if HYBRID
  FMM.timeKernels();
#endif
#ifdef MANY
  for ( int it=0; it<25; it++ ) {
  numBodies = int(pow(10,(it+24)/8.0));
#else
  {
#if BUILD
  numBodies = 10000000;
#else
  numBodies = 1000000;
#endif
#endif // MANY
  if(FMM.printNow) std::cout << "N                    : " << numBodies << std::endl;
  bodies.resize(numBodies);
  DATA.cube(bodies);
  FMM.startTimer("FMM");

  FMM.octsection(bodies);
#if BOTTOMUP
  FMM.bottomup(bodies,cells);
#else
  FMM.topdown(bodies,cells);
#endif
#if BUILD
#else
  FMM.startPAPI();
#if IneJ

#if 1
  FMM.getLET(cells);
  FMM.commBodies();
  FMM.commCells();
  FMM.evaluate(cells,cells);
  jbodies = bodies;
  for( int irank=1; irank<MPISIZE; irank++ ) {
    FMM.setLET(jcells,(MPIRANK+irank)%MPISIZE);
    FMM.shiftBodies(jbodies);
    Cells icells;
    FMM.topdown(jbodies,icells);


    CellQueue Qi, Qj;
    Qi.push(icells.begin());
    Qj.push(jcells.begin());
    uint ic=0;
    while( !Qi.empty() ) {
      C_iter Ci=Qi.front(); Qi.pop();
      C_iter Cj=Qj.front(); Qj.pop();
      if( Ci->ICELL != Cj->ICELL ) {
        std::cout << MPIRANK << " " << Ci->ICELL << " " << Cj->ICELL << std::endl;
        break;
      }
      if( Ci->NCHILD != Cj->NCHILD ) {
        std::cout << MPIRANK << " " << Ci->NCHILD << " " << Cj->NCHILD << std::endl;
        break;
      }
      if( Ci->NCLEAF != Cj->NCLEAF ) {
        std::cout << MPIRANK << " " << Ci->NCLEAF << " " << Cj->NCLEAF << std::endl;
        break;
      }
      real bi = 0, bj = 0;
      for( int i=0; i<Ci->NCLEAF; i++ ) {
        B_iter Bi=Ci->LEAF+i, Bj=Cj->LEAF+i;
        bi += Bi->X[0];
        bj += Bj->X[0];
      }
//      if( fabs(bi-bj) > 1e-6 ) std::cout << bi << " " << bj << std::endl;
      for( int i=0; i<Ci->NCHILD; i++ )  Qi.push(icells.begin()+Ci->CHILD+i);
      for( int i=0; i<Cj->NCHILD; i++ )  Qj.push(jcells.begin()+Cj->CHILD+i);
      ic++;
    }
    assert( ic == icells.size() );
    assert( icells.size() == jcells.size() );

    FMM.evaluate(cells,jcells);
  }
#else
  jbodies = bodies;
  for( int irank=0; irank!=MPISIZE; irank++ ) {
    FMM.shiftBodies(jbodies);
    jcells.clear();
#if BOTTOMUP
    FMM.bottomup(jbodies,jcells);
#else
    FMM.topdown(jbodies,jcells);
#endif
    FMM.evaluate(cells,jcells);
  }
#endif

#else
  FMM.evaluate(cells);
#endif

  FMM.stopPAPI();
  FMM.stopTimer("FMM",FMM.printNow);
  FMM.eraseTimer("FMM");
  FMM.writeTime();
  FMM.resetTimer();

  jbodies = bodies;
  if (bodies.size() > 100) bodies.resize(100);
  Bodies bodies2 = bodies;
  DATA.initTarget(bodies2);
  FMM.startTimer("Direct sum");
  for( int i=0; i!=MPISIZE; ++i ) {
    FMM.shiftBodies(jbodies);
    FMM.direct(bodies2,jbodies);
    if(FMM.printNow) std::cout << "Direct loop   : " << i+1 << "/" << MPISIZE << std::endl;
  }
  FMM.stopTimer("Direct sum",FMM.printNow);
  FMM.eraseTimer("Direct sum");
  real diff1 = 0, norm1 = 0, diff2 = 0, norm2 = 0, diff3 = 0, norm3 = 0, diff4 = 0, norm4 = 0;
  DATA.evalError(bodies,bodies2,diff1,norm1,diff2,norm2);
  MPI_Datatype MPI_TYPE = FMM.getType(diff1);
  MPI_Reduce(&diff1,&diff3,1,MPI_TYPE,MPI_SUM,0,MPI_COMM_WORLD);
  MPI_Reduce(&norm1,&norm3,1,MPI_TYPE,MPI_SUM,0,MPI_COMM_WORLD);
  MPI_Reduce(&diff2,&diff4,1,MPI_TYPE,MPI_SUM,0,MPI_COMM_WORLD);
  MPI_Reduce(&norm2,&norm4,1,MPI_TYPE,MPI_SUM,0,MPI_COMM_WORLD);
  if(FMM.printNow) DATA.printError(diff3,norm3,diff4,norm4);
#endif // BUILD
  }

#ifdef VTK
  for( B_iter B=jbodies.begin(); B!=jbodies.end(); ++B ) B->ICELL = 0;
  for( C_iter C=jcells.begin(); C!=jcells.end(); ++C ) {
    Body body;
    body.ICELL = 1;
    body.X     = C->X;
    body.SRC   = 0;
    jbodies.push_back(body);
  }
  int Ncell = 0;
  vtkPlot vtk;
  if( MPIRANK == 0 ) {
    vtk.setDomain(M_PI,0);
    vtk.setGroupOfPoints(jbodies,Ncell);
  }
  for( int i=1; i!=MPISIZE; ++i ) {
    FMM.shiftBodies(jbodies);
    if( MPIRANK == 0 ) {
      vtk.setGroupOfPoints(jbodies,Ncell);
    }
  }
  if( MPIRANK == 0 ) {
    vtk.plot(Ncell);
  }
#endif
}
