#ifndef dataset_h
#define dataset_h
#include "types.h"

class Dataset {                                                 // Contains all the different datasets
private:
  long filePosition;                                            // Position of file stream

#if PARALLEL_EVERYTHING
  void initTargetRec1(B_iter B_beg, B_iter B0, B_iter B1, bool IeqJ) {             // Initialize target values
    if (B1 - B0 < 1000) {
      for (B_iter B = B0; B < B1; B++) {
        B->IBODY = B - B_beg;                                   //  Tag body with initial index
        B->IPROC = MPIRANK;                                     //  Tag body with initial MPI rank
        B->TRG = 0 * IeqJ;                                      //  Clear previous target values (IeqJ is dummy)
      } 
    } else {
      int nh = (B1 - B0) / 2;
      __spawn_tasks__;
#if _OPENMP
#pragma omp task
#endif
      spawn_task0(spawn initTargetRec1(B_beg, B0, B0 + nh, IeqJ));
      call_task(spawn initTargetRec1(B_beg, B0 + nh, B1, IeqJ));
#if _OPENMP
#pragma omp taskwait
#endif
      __sync__;
    }
  }
#endif

public:
  Dataset() : filePosition(0) {}                                // Constructor
  ~Dataset() {}                                                 // Destructor

  void initSource(Bodies &bodies) {                             // Initialize source values
    for (B_iter B=bodies.begin(); B!=bodies.end(); B++) {       // Loop over bodies
      B->IBODY = B-bodies.begin();                              //  Tag body with initial index
      B->IPROC = MPIRANK;                                       //  Tag body with initial MPI rank
//      B->SRC = (drand48() - .5) / bodies.size() / MPISIZE;      //  Initialize mass/charge
      B->SRC = 1. / bodies.size() / MPISIZE;                    //  Initialize mass/charge
    }                                                           // End loop over bodies
  }

  void initTarget(Bodies &bodies, bool IeqJ=true) {             // Initialize target values
    srand48(0);                                                 // Set seed for random number generator
    for (B_iter B=bodies.begin(); B!=bodies.end(); B++) {       // Loop over bodies
      B->IBODY = B-bodies.begin();                              //  Tag body with initial index
      B->IPROC = MPIRANK;                                       //  Tag body with initial MPI rank
      B->TRG = 0 * IeqJ;                                        //  Clear previous target values (IeqJ is dummy)
    }                                                           // End loop over bodies
  }

#if PARALLEL_EVERYTHING
  void initTargetRec(Bodies &bodies, bool IeqJ=true) {
    initTargetRec1(bodies.begin(), bodies.begin(), bodies.end(), IeqJ);
  }
#endif

  void lattice(Bodies &bodies) {                                // Uniform distribution on [-1,1]^3 lattice (for debug)
    int level = int(log(bodies.size()*MPISIZE+1.)/M_LN2/3);     // Level of tree
    for (B_iter B=bodies.begin(); B!=bodies.end(); B++) {       // Loop over bodies
      int d = 0, l = 0;                                         //  Initialize dimension and level
      int index = MPIRANK * bodies.size() + (B-bodies.begin()); //  Set index of body iterator
      vec<3,int> nx = 0;                                        //  Initialize 3-D cell index
      while (index != 0) {                                      //  Deinterleave bits while index is nonzero
        nx[d] += (index % 2) * (1 << l);                        //   Add deinterleaved bit to 3-D cell index
        index >>= 1;                                            //   Right shift the bits
        d = (d+1) % 3;                                          //   Increment dimension
        if( d == 0 ) l++;                                       //   If dimension is 0 again, increment level
      }                                                         //  End while loop for deinterleaving bits
      for (d=0; d<3; d++) {                                     //  Loop over dimensions
        B->X[d] = -1 + (2 * nx[d] + 1.) / (1 << level);         //   Calculate cell center from 3-D cell index
      }                                                         //  End loop over dimensions
    }                                                           // End loop over bodies
    initSource(bodies);                                         // Initialize source values
    initTarget(bodies);                                         // Initialize target values
  }

  void cube(Bodies &bodies, int seed=0, int numSplit=1) {       // Random distribution in [-1,1]^3 cube
    srand48(seed);                                              // Set seed for random number generator
    for (B_iter B=bodies.begin(); B!=bodies.end(); B++) {       // Loop over bodies
      if (numSplit != 1 && B-bodies.begin() == int(seed*bodies.size()/numSplit)) {// Mimic parallel dataset
        seed++;                                                 //   Mimic seed at next rank
        srand48(seed);                                          //   Set seed for random number generator
      }                                                         //  Endif for mimicing parallel dataset
      for (int d=0; d<3; d++) {                                 //  Loop over dimension
        B->X[d] = drand48() * 2 * M_PI - M_PI;                  //   Initialize positions
      }                                                         //  End loop over dimension
    }                                                           // End loop over bodies
    initSource(bodies);                                         // Initialize source values
    initTarget(bodies);                                         // Initialize target values
  }

  void sphere(Bodies &bodies, int seed=0, int numSplit=1) {     // Random distribution on r = 1 sphere
    srand48(seed);                                              // Set seed for random number generator
    for (B_iter B=bodies.begin(); B!=bodies.end(); B++) {       // Loop over bodies
      if (numSplit != 1 && B-bodies.begin() == int(seed*bodies.size()/numSplit)) {// Mimic parallel dataset
        seed++;                                                 //   Mimic seed at next rank
        srand48(seed);                                          //   Set seed for random number generator
      }                                                         //  Endif for mimicing parallel dataset
      for (int d=0; d<3; d++) {                                 //  Loop over dimension
        B->X[d] = drand48() * 2 - 1;                            //   Initialize positions
      }                                                         //  End loop over dimension
      real_t r = std::sqrt(norm(B->X));                         //  Distance from center
      for (int d=0; d<3; d++) {                                 //  Loop over dimension
        B->X[d] /= r * 1.1;                                     //   Normalize positions
      }                                                         //  End loop over dimension
    }                                                           // End loop over bodies
    initSource(bodies);                                         // Initialize source values
    initTarget(bodies);                                         // Initialize target values
  }

  void plummer(Bodies &bodies, int seed=0, int numSplit=1) {
    srand48(seed);                                              // Set seed for random number generator
    for (B_iter B=bodies.begin(); B!=bodies.end(); B++) {       // Loop over bodies
      if (numSplit != 1 && B-bodies.begin() == int(seed*bodies.size()/numSplit)) { // Mimic parallel dataset
        seed++;                                                 //   Mimic seed at next rank
        srand48(seed);                                          //   Set seed for random number generator
      }                                                         //  Endif for mimicing parallel dataset
      for (int d=0; d<3; d++) B->X[d] = drand48() * 2 - 1;      //  Generate random number between [-1,1]
      real_t R2 = powf(drand48()*.999, -2.0/3.0) - 1;           //  Generate distribution radius squared
      real_t rscale = 0.015 * M_PI / std::sqrt(norm(B->X) * R2);//  Scaling to fit in M_PI box
      for (int d=0; d<3; d++) B->X[d] *= rscale;                //  Rescale particle coordinates
    }                                                           // End loop over bodies
    initSource(bodies);                                         // Initialize source values
    initTarget(bodies);                                         // Initialize target values
  }

  void readTarget(Bodies &bodies) {                             // Read target values from file
    char fname[256];                                            // File name for saving direct calculation values
    sprintf(fname,"direct%4.4d",MPIRANK);                       // Set file name
    std::ifstream file(fname,std::ios::in | std::ios::binary);  // Open file
    file.seekg(filePosition);                                   // Set position in file
    for (B_iter B=bodies.begin(); B!=bodies.end(); B++) {       // Loop over bodies
      file >> B->TRG[0];                                        //  Read data for potential
      file >> B->TRG[1];                                        //  Read data for x acceleration
      file >> B->TRG[2];                                        //  Read data for y acceleration
      file >> B->TRG[3];                                        //  Read data for z acceleration
    }                                                           // End loop over bodies
    filePosition = file.tellg();                                // Get position in file
    file.close();                                               // Close file
  }

  void writeTarget(Bodies &bodies) {                            // Write target values to file
    char fname[256];                                            // File name for saving direct calculation values
    sprintf(fname,"direct%4.4d",MPIRANK);                       // Set file name
    std::ofstream file(fname,std::ios::out | std::ios::app | std::ios::binary);// Open file
    for (B_iter B=bodies.begin(); B!=bodies.end(); B++) {       // Loop over bodies
      file << B->TRG[0] << std::endl;                           //  Write data for potential
      file << B->TRG[1] << std::endl;                           //  Write data for x acceleration
      file << B->TRG[2] << std::endl;                           //  Write data for y acceleration
      file << B->TRG[3] << std::endl;                           //  Write data for z acceleration
    }                                                           // End loop over bodies
    file.close();                                               // Close file

  }

  void evalError(Bodies &bodies, Bodies &bodies2,               // Evaluate error
                 real_t &diff1, real_t &norm1, real_t &diff2, real_t &norm2) {
    B_iter B2 = bodies2.begin();                                // Set iterator for bodies2
    for (B_iter B=bodies.begin(); B!=bodies.end(); B++, B2++) { // Loop over bodies & bodies2
#ifdef DEBUG
      std::cout << B->IBODY << " " << B->TRG[0] << " " << B2->TRG[0] << std::endl;// Compare every element
#endif
      diff1 += (B->TRG[0] - B2->TRG[0]) * (B->TRG[0] - B2->TRG[0]);// Difference of potential
      norm1 += B2->TRG[0] * B2->TRG[0];                         //  Value of potential
      diff2 += (B->TRG[1] - B2->TRG[1]) * (B->TRG[1] - B2->TRG[1]);// Difference of x acceleration
      diff2 += (B->TRG[2] - B2->TRG[2]) * (B->TRG[2] - B2->TRG[2]);// Difference of y acceleration
      diff2 += (B->TRG[3] - B2->TRG[3]) * (B->TRG[3] - B2->TRG[3]);// Difference of z acceleration
      norm2 += B2->TRG[1] * B2->TRG[1];                         //  Value of x acceleration
      norm2 += B2->TRG[2] * B2->TRG[2];                         //  Value of y acceleration
      norm2 += B2->TRG[3] * B2->TRG[3];                         //  Value of z acceleration
    }                                                           //  End loop over bodies & bodies2
  }

  void printError(real_t diff1, real_t norm1, real_t diff2, real_t norm2) {// Print relative L2 norm error
    std::cout << std::setw(20) << std::left
              << "Rel. L2 Error (pot)" << " : " << std::sqrt(diff1/norm1) << std::endl;
    std::cout << std::setw(20) << std::left
              << "Rel. L2 Error (acc)" << " : " << std::sqrt(diff2/norm2) << std::endl;
  }
};

#endif
