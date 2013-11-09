#ifndef vanderwaals_h
#define vanderwaals_h
#include "logger.h"
#include "types.h"

class VanDerWaals : public Logger {
 private:
  real_t cuton;                                                 //!< Cuton distance
  real_t cutoff;                                                //!< Cutoff distance
  real_t cycle;                                                 //!< Periodic cycle
  int numTypes;                                                 //!< Number of atom types
  std::vector<real_t> rscale;                                   //!< Distance scaling parameter for VdW potential
  std::vector<real_t> gscale;                                   //!< Value scaling parameter for VdW potential
  std::vector<real_t> fgscale;                                  //!< Value scaling parameter for VdW force

 private:
//! Van der Waals P2P kernel
  void P2P(C_iter Ci, C_iter Cj, vec3 Xperiodic) const {
    for (B_iter Bi=Ci->BODY; Bi!=Ci->BODY+Ci->NBODY; Bi++) {
      int atypei = int(Bi->SRC);
      for (B_iter Bj=Cj->BODY; Bj!=Cj->BODY+Cj->NBODY; Bj++) {
	vec3 dX = Bi->X - Bj->X - Xperiodic;
	real_t R2 = norm(dX);
	if (R2 != 0) {
	  int atypej = int(Bj->SRC);
	  real_t rs = rscale[atypei*numTypes+atypej];
	  real_t gs = gscale[atypei*numTypes+atypej];
	  real_t fgs = fgscale[atypei*numTypes+atypej];
	  real_t R2s = R2 * rs;
	  real_t invR2 = 1.0 / R2s;
	  real_t invR6 = invR2 * invR2 * invR2;
	  real_t cuton2 = cuton * cuton;
	  real_t cutoff2 = cutoff * cutoff;
          if (R2 < cutoff2) {
	    real_t tmp = 0, dtmp = 0;
            if (cuton2 < R2) {
	      real_t tmp1 = (cutoff2 - R2) / ((cutoff2-cuton2)*(cutoff2-cuton2)*(cutoff2-cuton2));
	      real_t tmp2 = tmp1 * (cutoff2 - R2) * (cutoff2 - 3 * cuton2 + 2 * R2);
	      tmp = invR6 * (invR6 - 1) * tmp2;
	      dtmp = invR6 * (invR6 - 1) * 12 * (cuton2 - R2) * tmp1
	        - 6 * invR6 * (invR6 + (invR6 - 1) * tmp2) * tmp2 / R2;
            } else {
	      tmp = invR6 * (invR6 - 1);
	      dtmp = invR2 * invR6 * (2 * invR6 - 1);
	    }
	    dtmp *= fgs;
	    Bi->TRG[0] += gs * tmp;
	    Bi->TRG[1] += dX[0] * dtmp;
	    Bi->TRG[2] += dX[1] * dtmp;
	    Bi->TRG[3] += dX[2] * dtmp;
          }
	}
      }
    }
  }

  //! Traverse tree to find neighbors
  struct Neighbor {
    VanDerWaals * VdW;                                          // VanDerWaals object
    C_iter Ci;                                                  // Iterator of current target cell
    C_iter Cj;                                                  // Iterator of current source cell
    C_iter C0;                                                  // Iterator of first source cell
    Neighbor(VanDerWaals * _VdW, C_iter _Ci, C_iter _Cj, C_iter _C0) :// Constructor
      VdW(_VdW), Ci(_Ci), Cj(_Cj), C0(_C0) {}                   // Initialize variables
    void operator() () {                                        // Overload operator()
      vec3 dX = Ci->X - Cj->X;                                  //  Distance vector from source to target
      wrap(dX, VdW->cycle);                                     //  Wrap around periodic domain
      vec3 Xperiodic = Ci->X - Cj->X - dX;                      //  Coordinate offset for periodic B.C.
      real_t R = std::sqrt(norm(dX));                           //  Scalar distance
      if (R < 3 * VdW->cutoff) {                                //  If cells are close
        if(Cj->NCHILD == 0) VdW->P2P(Ci,Cj,Xperiodic);          //   Van der Waals kernel
        task_group;                                             //   Intitialize tasks
        for (C_iter CC=C0+Cj->ICHILD; CC!=C0+Cj->ICHILD+Cj->NCHILD; CC++) {// Loop over cell's children
          Neighbor neighbor(VdW, Ci, CC, C0);                   //    Initialize recursive functor
          create_taskc(neighbor);                               //    Create task for recursive call
        }                                                       //   End loop over cell's children
        wait_tasks;                                             //   Synchronize tasks
      }                                                         //  End if for far cells
    }                                                           // End overload operator()
  };

 public:
//! Constructor
  VanDerWaals(double _cuton, double _cutoff, double _cycle, int _numTypes,
	      double * _rscale, double * _gscale, double * _fgscale) :
    cuton(_cuton), cutoff(_cutoff), cycle(_cycle), numTypes(_numTypes) {
    rscale.resize(numTypes*numTypes);
    gscale.resize(numTypes*numTypes);
    fgscale.resize(numTypes*numTypes);
    for (int i=0; i<numTypes*numTypes; i++) {
      rscale[i] = _rscale[i];
      gscale[i] = _gscale[i];
      fgscale[i] = _fgscale[i];
    }
  }

//! Evaluate Van Der Waals potential and force
  void evaluate(Cells &cells, Cells &jcells) {
    startTimer("Van der Waals");                                // Start timer
    C_iter Cj = jcells.begin();                                 // Set begin iterator for source cells
    for (C_iter Ci=cells.begin(); Ci!=cells.end(); Ci++) {      // Loop over target cells
      if (Ci->NCHILD == 0) {                                    //  If target cell is leaf
	Neighbor neighbor(this, Ci, Cj, Cj);                    //   Initialize recursive functor
	neighbor();                                             //   Find neighbors recursively
      }                                                         //  End if for leaf target cell
    }                                                           // End loop over target cells
    stopTimer("Van der Waals");                                 // Stop timer
  }

  void print(int stringLength) {
    if (verbose) {
      std::cout << std::setw(stringLength) << std::fixed << std::left// Set format
                << "cuton" << " : " << cuton << std::endl       // Print cuton
                << std::setw(stringLength)                      // Set format
                << "cutoff" << " : " << cutoff << std::endl     // Print cutoff
                << std::setw(stringLength)                      // Set format
                << "cycle" << " : " << cycle << std::endl;      // Print cycle
    }
  }
};
#endif
