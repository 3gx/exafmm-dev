/*
Copyright (C) 2011 by Rio Yokota, Simon Layton, Lorena Barba

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/
#include "serialfmm.h"
#ifdef VTK
#include "vtk.h"
#endif

int main() {
  const int numBodies = 10000;                                  // Number of bodies
  const int numTarget = 100;                                    // Number of target points to be used for error eval
  IMAGES = 0;                                                   // Level of periodic image tree (0 for non-periodic)
  THETA = 1 / sqrtf(4);                                         // Multipole acceptance criteria
  Bodies bodies(numBodies);                                     // Define vector of bodies
  Bodies jbodies;                                               // Define vector of source bodies
  Cells cells, jcells;                                          // Define vector of cells
  SerialFMM<Laplace> FMM;                                       // Instantiate SerialFMM class
  FMM.initialize();                                             // Initialize FMM
  FMM.printNow = true;                                          // Print timer

  FMM.startTimer("Set bodies");                                 // Start timer
  FMM.cube(bodies,1,1);                                         // Initialize bodies in a cube
  Bodies bodies2 = bodies;                                      // Define new bodies vector for direct sum
  FMM.stopTimer("Set bodies",FMM.printNow);                     // Stop timer
  FMM.eraseTimer("Set bodies");                                 // Erase entry from timer to avoid timer overlap

  if( IMAGES != 0 ) {                                           // For periodic boundary condition
    FMM.startTimer("Set periodic");                             //  Start timer
    jbodies = FMM.periodicBodies(bodies2);                      //  Copy source bodies for all periodic images
    FMM.stopTimer("Set periodic",FMM.printNow);                 //  Stop timer
    FMM.eraseTimer("Set periodic");                             //  Erase entry from timer to avoid timer overlap
  } else {                                                      // For free field boundary condition
    jbodies = bodies2;                                          //  Copy source bodies
  }                                                             // End if for periodic boundary condition
  FMM.startTimer("Direct sum");                                 // Start timer
  bodies2.resize(numTarget);                                    // Shrink target bodies vector to save time
  FMM.evalP2P(bodies2,jbodies);                                 // Direct summation between bodies2 and jbodies
  FMM.stopTimer("Direct sum",FMM.printNow);                     // Stop timer
  FMM.eraseTimer("Direct sum");                                 // Erase entry from timer to avoid timer overlap

  FMM.startTimer("Set domain");                                 // Start timer
  FMM.initTarget(bodies);                                       // Reinitialize target values
  FMM.setDomain(bodies);                                        // Set domain size of FMM
  FMM.stopTimer("Set domain",FMM.printNow);                     // Stop timer
  FMM.eraseTimer("Set domain");                                 // Erase entry from timer to avoid timer overlap

#ifdef TOPDOWN
  FMM.topdown(bodies,cells);                                    // Tree construction (top down) & upward sweep
#else
  FMM.bottomup(bodies,cells);                                   // Tree construction (bottom up) & upward sweep
#endif
  jcells = cells;                                               // Vector of source cells
  FMM.startTimer("Downward");                                   // Start timer
  FMM.downward(cells,jcells);                                   // Downward sweep
  FMM.stopTimer("Downward",FMM.printNow);                       // Stop timer
  FMM.eraseTimer("Downward");                                   // Erase entry from timer to avoid timer overlap

  FMM.startTimer("Unsort bodies");                              // Start timer
  std::sort(bodies.begin(),bodies.end());                       // Sort bodies back to original order
  FMM.stopTimer("Unsort bodies",FMM.printNow);                  // Stop timer
  FMM.eraseTimer("Unsort bodies");                              // Erase entry from timer to avoid timer overlap
  FMM.writeTime();                                              // Write timings of all events to file
  FMM.writeTime();                                              // Write again to have at least two data sets

#ifndef VTK
  real diff1 = 0, norm1 = 0, diff2 = 0, norm2 = 0;              // Initialize accumulators
  bodies.resize(numTarget);                                     // Shrink target bodies vector to save time
  FMM.evalError(bodies,bodies2,diff1,norm1,diff2,norm2);        // Evaluate error on the reduced set of bodies
  FMM.printError(diff1,norm1,diff2,norm2);                      // Print the L2 norm error
#else
  int Ncell = 0;                                                // Initialize number of cells
  vtkPlot vtk;                                                  // Instantiate vtkPlot class
  vtk.setDomain(FMM.getR0(),FMM.getX0());                       // Set bounding box for VTK
  vtk.setGroupOfPoints(bodies,Ncell);                           // Set group of points
  vtk.plot(Ncell);                                              // plot using VTK
#endif
  FMM.finalize();                                               // Finalize FMM
}
