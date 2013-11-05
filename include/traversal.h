#ifndef traversal_h
#define traversal_h
#include "kernel.h"
#include "logger.h"
#include "thread.h"

#if COUNT
#define count(N) N++
#else
#define count(N)
#endif

class Traversal : public Kernel, public Logger {
 private:
  int nspawn;                                                   //!< Threshold of NBODY for spawning new threads
  int images;                                                   //!< Number of periodic image sublevels
  real_t numP2P;                                                //!< Number of P2P kernel calls
  real_t numM2L;                                                //!< Number of M2L kernel calls
  C_iter Ci0;                                                   //!< Begin iterator for target cells
  C_iter Cj0;                                                   //!< Begin iterator for source cells

//! Dual tree traversal for a single pair of cells
  void traverse(C_iter Ci, C_iter Cj, bool mutual) {
    vec3 dX = Ci->X - Cj->X - Xperiodic;                        // Distance vector from source to target
    real_t R2 = norm(dX);                                       // Scalar distance squared
    if (R2 > (Ci->RCRIT+Cj->RCRIT)*(Ci->RCRIT+Cj->RCRIT)) {     // If distance is far enough
      M2L(Ci, Cj, mutual);                                      //  M2L kernel
      count(numM2L);                                            //  Increment M2L counter
    } else if (Ci->NCHILD == 0 && Cj->NCHILD == 0) {            // Else if both cells are bodies
      if (Cj->NBODY == 0) {                                    //  If the bodies weren't sent from remote node
	M2L(Ci, Cj, mutual);                                    //   M2L kernel
        count(numM2L);                                          //   Increment M2L counter
      } else {                                                  //  Else if the bodies were sent
	if (R2 == 0 && Ci == Cj) {                              //   If source and target are same
	  P2P(Ci);                                              //    P2P kernel for single cell
	} else {                                                //   Else if source and target are different
	  P2P(Ci, Cj, mutual);                                  //    P2P kernel for pair of cells
	}                                                       //   End if for same source and target
	count(numP2P);                                          //   Increment P2P counter
      }                                                         //  End if for bodies
    } else {                                                    // Else if cells are close but not bodies
      splitCell(Ci, Cj, mutual);                                //  Split cell and call function recursively for child
    }                                                           // End if for multipole acceptance
  }

#if CXX_LAMBDA == 0
  struct traverseCallable {
    Traversal * traversal;
    C_iter CiBegin; C_iter CiEnd; C_iter CjBegin; C_iter CjEnd; bool mutual;
  traverseCallable(Traversal * traversal_, C_iter CiBegin_, C_iter CiEnd_, C_iter CjBegin_, C_iter CjEnd_, bool mutual_) :
    traversal(traversal_), CiBegin(CiBegin_), CiEnd(CiEnd_), CjBegin(CjBegin_), CjEnd(CjEnd_), mutual(mutual_) {}
    void operator() () { traversal->traverse(CiBegin, CiEnd, CjBegin, CjEnd, mutual); }
  };

  traverseCallable
    traverse_(C_iter CiBegin_, C_iter CiEnd_, C_iter CjBegin_, C_iter CjEnd_, bool mutual_) {
    return traverseCallable(this, CiBegin_, CiEnd_, CjBegin_, CjEnd_, mutual_);
  }

#endif

//! Dual tree traversal for a range of Ci and Cj
  void traverse(C_iter CiBegin, C_iter CiEnd, C_iter CjBegin, C_iter CjEnd, bool mutual) {
    Trace trace;
    startTracer(trace);
    if (CiEnd - CiBegin == 1 || CjEnd - CjBegin == 1) {         // If only one cell in range
      if (CiBegin == CjBegin) {                                 //  If Ci == Cj
        assert(CiEnd == CjEnd);                                 //   Check if mutual & self interaction
        traverse(CiBegin, CjBegin, mutual);                     //   Call traverse for single pair
      } else {                                                  //  If Ci != Cj
        for (C_iter Ci=CiBegin; Ci!=CiEnd; Ci++) {              //   Loop over all Ci cells
          for (C_iter Cj=CjBegin; Cj!=CjEnd; Cj++) {            //    Loop over all Cj cells
            traverse(Ci, Cj, mutual);                           //     Call traverse for single pair
          }                                                     //    End loop over all Cj cells
        }                                                       //   End loop over all Ci cells
      }                                                         //  End if for Ci == Cj
    } else {                                                    // If many cells are in the range
      C_iter CiMid = CiBegin + (CiEnd - CiBegin) / 2;           //  Split range of Ci cells in half
      C_iter CjMid = CjBegin + (CjEnd - CjBegin) / 2;           //  Split range of Cj cells in half
      /* FIXME: 
	 here we need to have two task_group statements
	 so that dag recorder to work */
      {
	task_group;                                               //  Initialize task group
#if CXX_LAMBDA
	create_task0(traverse(CiBegin, CiMid, CjBegin, CjMid, mutual));// Spawn Ci:former Cj:former
#else
	create_taskc(traverse_(CiBegin, CiMid, CjBegin, CjMid, mutual));
#endif
	traverse(CiMid, CiEnd, CjMid, CjEnd, mutual);           //   No spawn Ci:latter Cj:latter
	wait_tasks;                                             //   Synchronize task group
      }
      {
	task_group;                                               //  Initialize task group
#if CXX_LAMBDA
	create_task0(traverse(CiBegin, CiMid, CjMid, CjEnd, mutual));// Spawn Ci:former Cj:latter
#else
	create_taskc(traverse_(CiBegin, CiMid, CjMid, CjEnd, mutual));
#endif
	if (!mutual || CiBegin != CjBegin) {                    //   Exclude mutual & self interaction
	  traverse(CiMid, CiEnd, CjBegin, CjMid, mutual);       //    No spawn Ci:latter Cj:former
	} else {                                                //   If mutual or self interaction
	  assert(CiEnd == CjEnd);                               //    Check if mutual & self interaction
	}                                                       //   End if for mutual & self interaction
	wait_tasks;                                             //   Synchronize task group
      }
    }                                                           // End if for many cells in range
    stopTracer(trace);
  }

//! Tree traversal of periodic cells
  void traversePeriodic(real_t cycle) {
    startTimer("Traverse periodic");                            // Start timer
    Xperiodic = 0;                                              // Periodic coordinate offset
    Cells pcells(27);                                           // Create cells
    C_iter Ci = pcells.end()-1;                                 // Last cell is periodic parent cell
    *Ci = *Cj0;                                                 // Copy values from source root
    Ci->ICHILD = 0;                                             // Child cells for periodic center cell
    Ci->NCHILD = 26;                                            // Number of child cells for periodic center cell
    C_iter C0 = Cj0;                                            // Placeholder for Cj0
    for (int level=0; level<images-1; level++) {                // Loop over sublevels of tree
      for (int ix=-1; ix<=1; ix++) {                            //  Loop over x periodic direction
        for (int iy=-1; iy<=1; iy++) {                          //   Loop over y periodic direction
          for (int iz=-1; iz<=1; iz++) {                        //    Loop over z periodic direction
            if (ix != 0 || iy != 0 || iz != 0) {                //     If periodic cell is not at center
              for (int cx=-1; cx<=1; cx++) {                    //      Loop over x periodic direction (child)
                for (int cy=-1; cy<=1; cy++) {                  //       Loop over y periodic direction (child)
                  for (int cz=-1; cz<=1; cz++) {                //        Loop over z periodic direction (child)
                    Xperiodic[0] = (ix * 3 + cx) * cycle;       //         Coordinate offset for x periodic direction
                    Xperiodic[1] = (iy * 3 + cy) * cycle;       //         Coordinate offset for y periodic direction
                    Xperiodic[2] = (iz * 3 + cz) * cycle;       //         Coordinate offset for z periodic direction
                    M2L(Ci0,Ci,false);                          //         M2L kernel
                  }                                             //        End loop over z periodic direction (child)
                }                                               //       End loop over y periodic direction (child)
              }                                                 //      End loop over x periodic direction (child)
            }                                                   //     Endif for periodic center cell
          }                                                     //    End loop over z periodic direction
        }                                                       //   End loop over y periodic direction
      }                                                         //  End loop over x periodic direction
#if Cartesian
      for (int i=1; i<NTERM; i++) Ci->M[i] *= Ci->M[0];         //  Normalize multipole expansion coefficients
#endif
      Cj0 = pcells.begin();                                     //  Redefine Cj0 for M2M
      C_iter Cj = Cj0;                                          //  Iterator of periodic neighbor cells
      for (int ix=-1; ix<=1; ix++) {                            //  Loop over x periodic direction
        for (int iy=-1; iy<=1; iy++) {                          //   Loop over y periodic direction
          for (int iz=-1; iz<=1; iz++) {                        //    Loop over z periodic direction
            if( ix != 0 || iy != 0 || iz != 0 ) {               //     If periodic cell is not at center
              Cj->X[0] = Ci->X[0] + ix * cycle;                 //      Set new x coordinate for periodic image
              Cj->X[1] = Ci->X[1] + iy * cycle;                 //      Set new y cooridnate for periodic image
              Cj->X[2] = Ci->X[2] + iz * cycle;                 //      Set new z coordinate for periodic image
              Cj->M    = Ci->M;                                 //      Copy multipoles to new periodic image
              Cj++;                                             //      Increment periodic cell iterator
            }                                                   //     Endif for periodic center cell
          }                                                     //    End loop over z periodic direction
        }                                                       //   End loop over y periodic direction
      }                                                         //  End loop over x periodic direction
      Ci->RMAX = 0;                                             //  Initialize Rmax of periodic parent
      Ci->M = 0;                                                //  Reset multipoles of periodic parent
      M2M(Ci,Cj0);                                              //  Evaluate periodic M2M kernels for this sublevel
#if Cartesian
      for (int i=1; i<NTERM; i++) Ci->M[i] /= Ci->M[0];         //  Normalize multipole expansion coefficients
#endif
      cycle *= 3;                                               //  Increase center cell size three times
      Cj0 = C0;                                                 //  Reset Cj0 back
    }                                                           // End loop over sublevels of tree
#if Cartesian
    Ci0->L /= Ci0->M[0];                                        // Normalize local expansion coefficients
#endif
    stopTimer("Traverse periodic");                             // Stop timer
  }

//! Split cell and call traverse() recursively for child
  void splitCell(C_iter Ci, C_iter Cj, bool mutual) {
    if (Cj->NCHILD == 0) {                                      // If Cj is leaf
      assert(Ci->NCHILD > 0);                                   //  Make sure Ci is not leaf
      for (C_iter ci=Ci0+Ci->ICHILD; ci!=Ci0+Ci->ICHILD+Ci->NCHILD; ci++ ) {// Loop over Ci's children
        traverse(ci, Cj, mutual);                               //   Traverse a single pair of cells
      }                                                         //  End loop over Ci's children
    } else if (Ci->NCHILD == 0) {                               // Else if Ci is leaf
      assert(Cj->NCHILD > 0);                                   //  Make sure Cj is not leaf
      for (C_iter cj=Cj0+Cj->ICHILD; cj!=Cj0+Cj->ICHILD+Cj->NCHILD; cj++ ) {// Loop over Cj's children
        traverse(Ci, cj, mutual);                               //   Traverse a single pair of cells
      }                                                         //  End loop over Cj's children
    } else if (Ci->NBODY + Cj->NBODY >= nspawn || (mutual && Ci == Cj)) {// Else if cells are still large
      traverse(Ci0+Ci->ICHILD, Ci0+Ci->ICHILD+Ci->NCHILD,       //  Traverse for range of cell pairs
               Cj0+Cj->ICHILD, Cj0+Cj->ICHILD+Cj->NCHILD, mutual);
    } else if (Ci->RCRIT >= Cj->RCRIT) {                        // Else if Ci is larger than Cj
      for (C_iter ci=Ci0+Ci->ICHILD; ci!=Ci0+Ci->ICHILD+Ci->NCHILD; ci++ ) {// Loop over Ci's children
        traverse(ci, Cj, mutual);                               //   Traverse a single pair of cells
      }                                                         //  End loop over Ci's children
    } else {                                                    // Else if Cj is larger than Ci
      for (C_iter cj=Cj0+Cj->ICHILD; cj!=Cj0+Cj->ICHILD+Cj->NCHILD; cj++ ) {// Loop over Cj's children
        traverse(Ci, cj, mutual);                               //   Traverse a single pair of cells
      }                                                         //  End loop over Cj's children
    }                                                           // End if for leafs and Ci Cj size
  }

 public:
  Traversal(int _nspawn, int _images) : nspawn(_nspawn), images(_images), numP2P(0), numM2L(0) {}

//! Evaluate P2P and M2L using dual tree traversal
  void dualTreeTraversal(Cells &icells, Cells &jcells, real_t cycle, bool mutual=false) {
    if (icells.empty() || jcells.empty()) return;               // Quit if either of the cell vectors are empty
    startTimer("Traverse");                                     // Start timer
    Ci0 = icells.begin();                                       // Set iterator of target root cell
    Cj0 = jcells.begin();                                       // Set iterator of source root cell
    if (images == 0) {                                          // If non-periodic boundary condition
      Xperiodic = 0;                                            //  No periodic shift
      traverse(Ci0,Cj0,mutual);                                 //  Traverse the tree
    } else {                                                    // If periodic boundary condition
      for (int ix=-1; ix<=1; ix++) {                            //  Loop over x periodic direction
	for (int iy=-1; iy<=1; iy++) {                          //   Loop over y periodic direction
	  for (int iz=-1; iz<=1; iz++) {                        //    Loop over z periodic direction
	    Xperiodic[0] = ix * cycle;                          //     Coordinate shift for x periodic direction
	    Xperiodic[1] = iy * cycle;                          //     Coordinate shift for y periodic direction
	    Xperiodic[2] = iz * cycle;                          //     Coordinate shift for z periodic direction
	    traverse(Ci0,Cj0,false);                            //     Traverse the tree for this periodic image
	  }                                                     //    End loop over z periodic direction
	}                                                       //   End loop over y periodic direction
      }                                                         //  End loop over x periodic direction
      traversePeriodic(cycle);                                  //  Traverse tree for periodic images
    }                                                           // End if for periodic boundary condition
    stopTimer("Traverse");                                      // Stop timer
    writeTrace();                                               // Write trace to file
  }

//! Direct summation
  void direct(Bodies &ibodies, Bodies &jbodies, real_t cycle) {
    Cells cells(2);                                             // Define a pair of cells to pass to P2P kernel
    C_iter Ci = cells.begin(), Cj = cells.begin()+1;            // First cell is target, second cell is source
    Ci->BODY = ibodies.begin();                                 // Iterator of first target body
    Ci->NBODY = ibodies.size();                                 // Number of target bodies
    Cj->BODY = jbodies.begin();                                 // Iterator of first source body
    Cj->NBODY = jbodies.size();                                 // Number of source bodies
    int prange = 0;                                             // Range of periodic images
    for (int i=0; i<images; i++) {                              // Loop over periodic image sublevels
      prange += int(std::pow(3.,i));                            //  Accumulate range of periodic images
    }                                                           // End loop over perioidc image sublevels
    for (int ix=-prange; ix<=prange; ix++) {                    // Loop over x periodic direction
      for (int iy=-prange; iy<=prange; iy++) {                  //  Loop over y periodic direction
        for (int iz=-prange; iz<=prange; iz++) {                //   Loop over z periodic direction
          Xperiodic[0] = ix * cycle;                            //    Coordinate shift for x periodic direction
          Xperiodic[1] = iy * cycle;                            //    Coordinate shift for y periodic direction
          Xperiodic[2] = iz * cycle;                            //    Coordinate shift for z periodic direction
          P2P(Ci,Cj,false);                                     //    Evaluate P2P kernel
        }                                                       //   End loop over z periodic direction
      }                                                         //  End loop over y periodic direction
    }                                                           // End loop over x periodic direction
  }

//! Normalize bodies after direct summation
  void normalize(Bodies &bodies) {
    for (B_iter B=bodies.begin(); B!=bodies.end(); B++) {       // Loop over bodies
      B->TRG /= B->SRC;                                         //  Normalize by target charge
    }                                                           // End loop over bodies
  }

//! Print traversal statistics
  void printTraversalData() {
#if COUNT
    if (verbose) {                                              // If verbose flag is true
      std::cout << "--- Traversal stats --------------" << std::endl// Print title
	      << std::setw(stringLength) << std::left           //  Set format
	      << "P2P calls"  << " : "                          //  Print title
	      << std::setprecision(0) << std::fixed             //  Set format
              << numP2P << std::endl                            //  Print number of P2P calls
	      << std::setw(stringLength) << std::left           //  Set format
	      << "M2L calls"  << " : "                          //  Print title
	      << std::setprecision(0) << std::fixed             //  Set format
              << numM2L << std::endl;                           //  Print number of M2L calls
    }                                                           // End if for verbose flag
#endif
  }
};
#endif
