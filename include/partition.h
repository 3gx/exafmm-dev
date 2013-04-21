#ifndef partition_h
#define partition_h
#include "mympi.h"
#include "logger.h"

//! Handles all the partitioning of domains
class Partition : public MyMPI, public Logger {
 protected:
  Bodies recvBodies;                                            //!< Receive buffer for bodies
  int * sendBodyCount;                                          //!< Send count
  int * sendBodyDispl;                                          //!< Send displacement
  int * recvBodyCount;                                          //!< Receive count
  int * recvBodyDispl;                                          //!< Receive displacement

 public:
  Bodies sendBodies;                                            //!< Send buffer for bodies

 protected:
//! Exchange send count for bodies
  void alltoall(Bodies &bodies) {
    for (int i=0; i<MPISIZE; i++) {                             // Loop over ranks
      sendBodyCount[i] = 0;                                     //  Initialize send counts
    }                                                           // End loop over ranks
    for (B_iter B=bodies.begin(); B!=bodies.end(); B++) {       // Loop over bodies
      sendBodyCount[B->IPROC]++;                                //  Fill send count bucket
      B->IPROC = MPIRANK;                                       //  Tag for sending back to original rank
    }                                                           // End loop over bodies
    MPI_Alltoall(sendBodyCount, 1, MPI_INT,                     // Communicate send count to get receive count
                 recvBodyCount, 1, MPI_INT, MPI_COMM_WORLD);
    sendBodyDispl[0] = recvBodyDispl[0] = 0;                    // Initialize send/receive displacements
    for (int irank=0; irank<MPISIZE-1; irank++) {               // Loop over ranks
      sendBodyDispl[irank+1] = sendBodyDispl[irank] + sendBodyCount[irank];//  Set send displacement
      recvBodyDispl[irank+1] = recvBodyDispl[irank] + recvBodyCount[irank];//  Set receive displacement
    }                                                           // End loop over ranks
  }

//! Exchange bodies
  void alltoallv(Bodies &bodies) {
    int word = sizeof(bodies[0]) / 4;                           // Word size of body structure
    recvBodies.resize(recvBodyDispl[MPISIZE-1]+recvBodyCount[MPISIZE-1]);// Resize receive buffer
    for (int irank=0; irank<MPISIZE; irank++) {                 // Loop over ranks
      sendBodyCount[irank] *= word;                             //  Multiply send count by word size of data
      sendBodyDispl[irank] *= word;                             //  Multiply send displacement by word size of data
      recvBodyCount[irank] *= word;                             //  Multiply receive count by word size of data
      recvBodyDispl[irank] *= word;                             //  Multiply receive displacement by word size of data
    }                                                           // End loop over ranks
    MPI_Alltoallv(&bodies[0], sendBodyCount, sendBodyDispl, MPI_INT,// Communicate bodies
                  &recvBodies[0], recvBodyCount, recvBodyDispl, MPI_INT, MPI_COMM_WORLD);
    for (int irank=0; irank<MPISIZE; irank++) {                 // Loop over ranks
      sendBodyCount[irank] /= word;                             //  Divide send count by word size of data
      sendBodyDispl[irank] /= word;                             //  Divide send displacement by word size of data
      recvBodyCount[irank] /= word;                             //  Divide receive count by word size of data
      recvBodyDispl[irank] /= word;                             //  Divide receive displacement by word size of data
    }                                                           // End loop over ranks
  }

 public:
//! Constructor
  Partition() {
    sendBodyCount = new int [MPISIZE];                          // Allocate send count
    sendBodyDispl = new int [MPISIZE];                          // Allocate send displacement
    recvBodyCount = new int [MPISIZE];                          // Allocate receive count
    recvBodyDispl = new int [MPISIZE];                          // Allocate receive displacement
  }
//! Destructor
  ~Partition() {
    delete[] sendBodyCount;                                     // Deallocate send count
    delete[] sendBodyDispl;                                     // Deallocate send displacement
    delete[] recvBodyCount;                                     // Deallocate receive count
    delete[] recvBodyDispl;                                     // Deallocate receive displacement
  }

//! Send bodies to next rank (round robin)
  void shiftBodies(Bodies &bodies) {
    int newSize;                                                // New number of bodies
    int oldSize = bodies.size();                                // Current number of bodies
    const int word = sizeof(bodies[0]) / 4;                     // Word size of body structure
    const int isend = (MPIRANK + 1          ) % MPISIZE;        // Send to next rank (wrap around)
    const int irecv = (MPIRANK - 1 + MPISIZE) % MPISIZE;        // Receive from previous rank (wrap around)
    MPI_Request sreq,rreq;                                      // Send, receive request handles

    MPI_Isend(&oldSize, 1, MPI_INT, irecv, 0, MPI_COMM_WORLD, &sreq);// Send current number of bodies
    MPI_Irecv(&newSize, 1, MPI_INT, isend, 0, MPI_COMM_WORLD, &rreq);// Receive new number of bodies
    MPI_Wait(&sreq, MPI_STATUS_IGNORE);                         // Wait for send to complete
    MPI_Wait(&rreq, MPI_STATUS_IGNORE);                         // Wait for receive to complete

    recvBodies.resize(newSize);                                 // Resize buffer to new number of bodies
    MPI_Isend(&bodies[0], oldSize*word, MPI_INT, irecv,         // Send bodies to next rank
              1, MPI_COMM_WORLD, &sreq);
    MPI_Irecv(&recvBodies[0], newSize*word, MPI_INT, isend,     // Receive bodies from previous rank
              1, MPI_COMM_WORLD, &rreq);
    MPI_Wait(&sreq, MPI_STATUS_IGNORE);                         // Wait for send to complete
    MPI_Wait(&rreq, MPI_STATUS_IGNORE);                         // Wait for receive to complete
    bodies = recvBodies;                                        // Copy bodies from buffer
  }

//! Allreduce bounds from all ranks
  Bounds allreduceBounds(Bounds local) {
    fvec3 localXmin, localXmax, globalXmin, globalXmax;
    for (int d=0; d<3; d++) {                                   // Loop over dimensions
      localXmin[d] = local.Xmin[d];                             //  Convert Xmin to float
      localXmax[d] = local.Xmax[d];                             //  Convert Xmax to float
    }                                                           // End loop over dimensions
    MPI_Allreduce(localXmin, globalXmin, 3, MPI_FLOAT, MPI_MIN, MPI_COMM_WORLD);// Reduce domain Xmin
    MPI_Allreduce(localXmax, globalXmax, 3, MPI_FLOAT, MPI_MAX, MPI_COMM_WORLD);// Reduce domain Xmax
    Bounds global;
    for (int d=0; d<3; d++) {                                   // Loop over dimensions
      global.Xmin[d] = globalXmin[d];                           //  Convert Xmin to real_t
      global.Xmax[d] = globalXmax[d];                           //  Convert Xmax to real_t
    }                                                           // End loop over dimensions
    return global;                                              // Return global bounds
  }

//! Partition bodies
  Bounds partition(Bodies &bodies, Bounds global) {
    startTimer("Partition");                                    // Start timer
    int mpisize = MPISIZE;                                      // Initialize MPI size counter
    vec<3,int> Npartition = 1;                                  // Number of partitions in each direction
    int d = 0;                                                  // Initialize dimension counter
    while (mpisize != 1) {                                      // Divide domain while counter is not one
      Npartition[d] <<= 1;                                      //  Divide this dimension
      d = (d+1) % 3;                                            //  Increment dimension
      mpisize >>= 1;                                            //  Right shift the bits of counter
    }                                                           // End while loop for domain subdivision
    vec3 Xpartition;                                            // Size of partitions in each direction
    for (d=0; d<3; d++) {                                       // Loop over dimensions
      Xpartition[d] = (global.Xmax[d] - global.Xmin[d]) / Npartition[d];//  Size of partition in each direction
    }                                                           // End loop over dimensions
    int ix[3];                                                  // Index vector
    ix[0] = MPIRANK % Npartition[0];                            // x index of partition
    ix[1] = MPIRANK / Npartition[0] % Npartition[1];            // y index
    ix[2] = MPIRANK / Npartition[0] / Npartition[1];            // z index
    Bounds local;                                               // Local bounds
    for (d=0; d<3; d++) {                                       // Loop over dimensions
      local.Xmin[d] = global.Xmin[d] + ix[d] * Xpartition[d];   // Xmin of local domain at current rank
      local.Xmax[d] = global.Xmin[d] + (ix[d] + 1) * Xpartition[d];// Xmax of local domain at current rank
    }                                                           // End loop over dimensions
    for (B_iter B=bodies.begin(); B!=bodies.end(); B++) {       // Loop over bodies
      for (d=0; d<3; d++) {                                     //  Loop over dimensions
        ix[d] = int((B->X[d] - global.Xmin[d]) / Xpartition[d]);//   Index vector of partition
      }                                                         //  End loop over dimensions
      B->IPROC = ix[0] + Npartition[0] * (ix[1] + ix[2] * Npartition[1]);//  Set send rank
      B->ICELL = B->IPROC;                                      //  Do this to sort accroding to IPROC
    }                                                           // End loop over bodies
    stopTimer("Partition",printNow);                            // Stop timer
    return local;
  }

//! Send bodies back to where they came from
  void unpartition(Bodies &bodies) {
    startTimer("Unpartition");                                  // Start timer
    for (B_iter B=bodies.begin(); B!=bodies.end(); B++) {       // Loop over bodies
      B->ICELL = B->IPROC;                                      //  Do this to sortaccroding to IPROC
    }                                                           // End loop over bodies
    stopTimer("Unpartition", printNow);                         // Stop timer
  }
};
#endif
