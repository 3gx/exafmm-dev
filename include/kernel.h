#ifndef kernel_h
#define kernel_h
#include <cmath>
#include "types.h"

class Kernel {
public:
  void P2P(C_iter Ci, C_iter Cj, vec3 Xperiodic, bool mutual) const; //!< P2P kernel between cells Ci and Cj
  void P2P(C_iter C) const;                                     //!< P2P kernel for cell C
  void P2M(C_iter C) const;                                     //!< P2M kernel for cell C
  void M2M(C_iter Ci, C_iter C0) const;                         //!< M2M kernel for one parent cell Ci
  void M2L(C_iter Ci, C_iter Cj, vec3 Xperiodic, bool mutual) const; //!< M2L kernel between cells Ci and Cj
  void L2L(C_iter Ci, C_iter C0) const;                         //!< L2L kernel for one child cell Ci
  void L2P(C_iter Ci) const;                                    //!< L2P kernel for cell Ci
};
#endif
