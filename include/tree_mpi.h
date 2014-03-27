#ifndef tree_mpi_h
#define tree_mpi_h
#include "logger.h"

//! Handles all the communication of local essential trees
class TreeMPI {
private:
  int mpirank;                                                  //!< Rank of MPI communicator
  int mpisize;                                                  //!< Size of MPI communicator
  int irank;                                                    //!< MPI rank loop counter
  int images;                                                   //!< Number of periodic image sublevels
  fvec3 localXmin;                                              //!< Local Xmin for a given rank
  fvec3 localXmax;                                              //!< Local Xmax for a given rank
  fvec3 * allLocalXmin;                                         //!< Array for local Xmin for all ranks
  fvec3 * allLocalXmax;                                         //!< Array for local Xmax for all ranks
  Bodies sendBodies;                                            //!< Send buffer for bodies
  Bodies recvBodies;                                            //!< Receive buffer for bodies
  Cells sendCells;                                              //!< Send buffer for cells
  Cells recvCells;                                              //!< Receive buffer for cells
  C_iter C0;                                                    //!< Iterator of first cell
  int * sendBodyCount;                                          //!< Send count
  int * sendBodyDispl;                                          //!< Send displacement
  int * recvBodyCount;                                          //!< Receive count
  int * recvBodyDispl;                                          //!< Receive displacement
  int * sendCellCount;                                          //!< Send count
  int * sendCellDispl;                                          //!< Send displacement
  int * recvCellCount;                                          //!< Receive count
  int * recvCellDispl;                                          //!< Receive displacement

private:
  //! Allgather bounds from all ranks
  void allgatherBounds(Bounds local) {
    fvec3 Xmin, Xmax;
    for (int d=0; d<3; d++) {                                   // Loop over dimensions
      Xmin[d] = local.Xmin[d];                                  //  Convert Xmin to float
      Xmax[d] = local.Xmax[d];                                  //  Convert Xmax to float
    }                                                           // End loop over dimensions
    MPI_Allgather(Xmin, 3, MPI_FLOAT, &allLocalXmin[0], 3, MPI_FLOAT, MPI_COMM_WORLD);// Gather all domain bounds
    MPI_Allgather(Xmax, 3, MPI_FLOAT, &allLocalXmax[0], 3, MPI_FLOAT, MPI_COMM_WORLD);// Gather all domain bounds
  }

  //! Get distance to other domain
  real_t getDistance(C_iter C, vec3 Xperiodic) {
    vec3 dX;                                                    // Distance vector
    for (int d=0; d<3; d++) {                                   // Loop over dimensions
      dX[d] = (C->X[d] + Xperiodic[d] > localXmax[d])*          //  Calculate the distance between cell C and
	(C->X[d] + Xperiodic[d] - localXmax[d])+                //  the nearest point in domain [xmin,xmax]^3
	(C->X[d] + Xperiodic[d] < localXmin[d])*                //  Take the differnece from xmin or xmax
	(C->X[d] + Xperiodic[d] - localXmin[d]);                //  or 0 if between xmin and xmax
    }                                                           // End loop over dimensions
    return norm(dX);                                            // Return distance squared
  }

  //! Add cells to send buffer
  void addSendCell(C_iter C, uint64_t & iparent, int & icell) {
    Cell cell(*C);                                              // Initialize send cell
    cell.NCHILD = cell.NBODY = 0;                               // Reset counters
    cell.PARENT = iparent;                                      // Index of parent
    sendCells.push_back(cell);                                  // Push to send cell vector
    icell++;                                                    // Increment cell counter
    C_iter Cparent = sendCells.begin() + sendCellDispl[irank] + iparent;// Get parent iterator
    if (Cparent->NCHILD == 0) Cparent->ICHILD = icell;          // Index of parent's first child
    Cparent->NCHILD++;                                          // Increment parent's child counter
  }

  //! Add bodies to send buffer
  void addSendBody(C_iter C, int & ibody, int icell) {
    C_iter Csend = sendCells.begin() + sendCellDispl[irank] + icell;// Get send cell iterator
    Csend->NBODY = C->NBODY;                                    // Set number of bodies
    Csend->IBODY = ibody;                                       // Set body index per rank
    for (B_iter B=C->BODY; B!=C->BODY+C->NBODY; B++) {          // Loop over bodies in cell
      sendBodies.push_back(*B);                                 //  Push to send body vector
      sendBodies.back().IPROC = irank;                          //  Set current rank
    }                                                           // End loop over bodies in cell
    ibody+=C->NBODY;                                            // Increment body counter
  }

  //! Determine which cells to send
  void traverseLET(C_iter C, real_t cycle, int & ibody, int & icell) {
    int level = int(logf(mpisize-1) / M_LN2 / 3) + 1;           // Level of local root cell
    if (mpisize == 1) level = 0;                                // Account for serial case
    for (C_iter CC=C0+C->ICHILD; CC!=C0+C->ICHILD+C->NCHILD; CC++) {  //Loop over child cells
      addSendCell(CC, C->ICELL, icell);                         //  Add cells to send
      CC->ICELL = icell;                                        //  Store cell index
      if (CC->NCHILD == 0) {                                    //  If cell is leaf
	addSendBody(CC, ibody, icell);                          //   Add bodies to send
      } else {                                                  //  If cell is not leaf
	bool divide = false;                                    //   Initialize logical for dividing
	vec3 Xperiodic = 0;                                     //   Periodic coordinate offset
	if (images == 0) {                                      //   If free boundary condition
	  real_t R2 = getDistance(CC, Xperiodic);               //    Get distance to other domain
	  divide |= 4 * CC->RCRIT * CC->RCRIT > R2;             //    Divide if the cell seems too close
	} else {                                                //   If periodic boundary condition
	  for (int ix=-1; ix<=1; ix++) {                        //    Loop over x periodic direction
	    for (int iy=-1; iy<=1; iy++) {                      //     Loop over y periodic direction
	      for (int iz=-1; iz<=1; iz++) {                    //      Loop over z periodic direction
		Xperiodic[0] = ix * cycle;                      //       Coordinate offset for x periodic direction
		Xperiodic[1] = iy * cycle;                      //       Coordinate offset for y periodic direction
		Xperiodic[2] = iz * cycle;                      //       Coordinate offset for z periodic direction
		real_t R2 = getDistance(CC, Xperiodic);         //       Get distance to other domain
		divide |= 4 * CC->RCRIT * CC->RCRIT > R2;       //       Divide if cell seems too close
	      }                                                 //      End loop over z periodic direction
	    }                                                   //     End loop over y periodic direction
	  }                                                     //    End loop over x periodic direction
	}                                                       //   Endif for periodic boundary condition
	divide |= CC->R > (cycle / (1 << (level+1)));           //   Divide if cell is larger than local root cell
	if (!divide) {                                          //   If cell does not have to be divided
	  CC->NCHILD = 0;                                       //    Cut off child links
	}                                                       //   Endif for cell division
      }                                                         //  Endif for leaf
    }                                                           // End loop over child cells
    for (C_iter CC=C0+C->ICHILD; CC!=C0+C->ICHILD+C->NCHILD; CC++) {// Loop over child cells
      traverseLET(CC, cycle, ibody, icell);                     //  Recursively traverse tree to get LET
    }                                                           // End loop over child cells
  }

  //! Exchange send count for bodies
  void alltoall(Bodies & bodies) {
    for (int i=0; i<mpisize; i++) {                             // Loop over ranks
      sendBodyCount[i] = 0;                                     //  Initialize send counts
    }                                                           // End loop over ranks
    for (B_iter B=bodies.begin(); B!=bodies.end(); B++) {       // Loop over bodies
      assert(0 <= B->IPROC && B->IPROC < mpisize);              //  Check bounds for process ID
      sendBodyCount[B->IPROC]++;                                //  Fill send count bucket
      B->IPROC = mpirank;                                       //  Tag for sending back to original rank
    }                                                           // End loop over bodies
    MPI_Alltoall(sendBodyCount, 1, MPI_INT,                     // Communicate send count to get receive count
                 recvBodyCount, 1, MPI_INT, MPI_COMM_WORLD);
    sendBodyDispl[0] = recvBodyDispl[0] = 0;                    // Initialize send/receive displacements
    for (int irank=0; irank<mpisize-1; irank++) {               // Loop over ranks
      sendBodyDispl[irank+1] = sendBodyDispl[irank] + sendBodyCount[irank];//  Set send displacement
      recvBodyDispl[irank+1] = recvBodyDispl[irank] + recvBodyCount[irank];//  Set receive displacement
    }                                                           // End loop over ranks
  }

  //! Exchange bodies
  void alltoallv(Bodies & bodies) {
    int word = sizeof(bodies[0]) / 4;                           // Word size of body structure
    recvBodies.resize(recvBodyDispl[mpisize-1]+recvBodyCount[mpisize-1]);// Resize receive buffer
    for (int irank=0; irank<mpisize; irank++) {                 // Loop over ranks
      sendBodyCount[irank] *= word;                             //  Multiply send count by word size of data
      sendBodyDispl[irank] *= word;                             //  Multiply send displacement by word size of data
      recvBodyCount[irank] *= word;                             //  Multiply receive count by word size of data
      recvBodyDispl[irank] *= word;                             //  Multiply receive displacement by word size of data
    }                                                           // End loop over ranks
    MPI_Alltoallv(&bodies[0], sendBodyCount, sendBodyDispl, MPI_INT,// Communicate bodies
                  &recvBodies[0], recvBodyCount, recvBodyDispl, MPI_INT, MPI_COMM_WORLD);
    for (int irank=0; irank<mpisize; irank++) {                 // Loop over ranks
      sendBodyCount[irank] /= word;                             //  Divide send count by word size of data
      sendBodyDispl[irank] /= word;                             //  Divide send displacement by word size of data
      recvBodyCount[irank] /= word;                             //  Divide receive count by word size of data
      recvBodyDispl[irank] /= word;                             //  Divide receive displacement by word size of data
    }                                                           // End loop over ranks
  }

  //! Exchange send count for cells
  void alltoall(Cells) {
    MPI_Alltoall(sendCellCount, 1, MPI_INT,                     // Communicate send count to get receive count
                 recvCellCount, 1, MPI_INT, MPI_COMM_WORLD);
    recvCellDispl[0] = 0;                                       // Initialize receive displacements
    for (int irank=0; irank<mpisize-1; irank++) {               // Loop over ranks
      recvCellDispl[irank+1] = recvCellDispl[irank] + recvCellCount[irank];//  Set receive displacement
    }                                                           // End loop over ranks
  }

  //! Exchange cells
  void alltoallv(Cells & cells) {
    int word = sizeof(cells[0]) / 4;                            // Word size of body structure
    recvCells.resize(recvCellDispl[mpisize-1]+recvCellCount[mpisize-1]);// Resize receive buffer
    for (int irank=0; irank<mpisize; irank++) {                 // Loop over ranks
      sendCellCount[irank] *= word;                             //  Multiply send count by word size of data
      sendCellDispl[irank] *= word;                             //  Multiply send displacement by word size of data
      recvCellCount[irank] *= word;                             //  Multiply receive count by word size of data
      recvCellDispl[irank] *= word;                             //  Multiply receive displacement by word size of data
    }                                                           // End loop over ranks
    MPI_Alltoallv(&cells[0], sendCellCount, sendCellDispl, MPI_INT,// Communicate cells
                  &recvCells[0], recvCellCount, recvCellDispl, MPI_INT, MPI_COMM_WORLD);
    for (int irank=0; irank<mpisize; irank++) {                 // Loop over ranks
      sendCellCount[irank] /= word;                             //  Divide send count by word size of data
      sendCellDispl[irank] /= word;                             //  Divide send displacement by word size of data
      recvCellCount[irank] /= word;                             //  Divide receive count by word size of data
      recvCellDispl[irank] /= word;                             //  Divide receive displacement by word size of data
    }                                                           // End loop over ranks
  }

public:
  //! Constructor
  TreeMPI(int _images) : images(_images) {                      // Initialize variables
    MPI_Comm_rank(MPI_COMM_WORLD, &mpirank);                    // Get rank of current MPI process
    MPI_Comm_size(MPI_COMM_WORLD, &mpisize);                    // Get number of MPI processes
    allLocalXmin = new fvec3 [mpisize];                         // Allocate array for minimum of local domains
    allLocalXmax = new fvec3 [mpisize];                         // Allocate array for maximum of local domains
    sendBodyCount = new int [mpisize];                          // Allocate send count
    sendBodyDispl = new int [mpisize];                          // Allocate send displacement
    recvBodyCount = new int [mpisize];                          // Allocate receive count
    recvBodyDispl = new int [mpisize];                          // Allocate receive displacement
    sendCellCount = new int [mpisize];                          // Allocate send count
    sendCellDispl = new int [mpisize];                          // Allocate send displacement
    recvCellCount = new int [mpisize];                          // Allocate receive count
    recvCellDispl = new int [mpisize];                          // Allocate receive displacement
  }
  //! Destructor
  ~TreeMPI() {
    delete[] allLocalXmin;                                      // Deallocate array for minimum of local domains
    delete[] allLocalXmax;                                      // Deallocate array for maximum of local domains
    delete[] sendBodyCount;                                     // Deallocate send count
    delete[] sendBodyDispl;                                     // Deallocate send displacement
    delete[] recvBodyCount;                                     // Deallocate receive count
    delete[] recvBodyDispl;                                     // Deallocate receive displacement
    delete[] sendCellCount;                                     // Deallocate send count
    delete[] sendCellDispl;                                     // Deallocate send displacement
    delete[] recvCellCount;                                     // Deallocate receive count
    delete[] recvCellDispl;                                     // Deallocate receive displacement
  }

  //! Set local essential tree to send to each process
  void setLET(Cells & cells, Bounds bounds, real_t cycle) {
    logger::startTimer("Set LET");                              // Start timer
    allgatherBounds(bounds);                                    // Gather local bounds from all ranks
    sendBodies.clear();                                         // Clear send buffer for bodies
    sendCells.clear();                                          // Clear send buffer for cells
    sendCellDispl[0] = 0;                                       // Initialize displacement vector
    for (irank=0; irank<mpisize; irank++) {                     // Loop over ranks
      if (irank != 0) sendCellDispl[irank] = sendCellDispl[irank-1] + sendCellCount[irank-1];// Update displacement
      if (irank != mpirank) {                                   //  If not current rank
        recvCells = cells;                                      //   Use recvCells as temporary storage
        C0 = recvCells.begin();                                 //   Set cells begin iterator
        localXmin = allLocalXmin[irank];                        //   Set local Xmin for irank
        localXmax = allLocalXmax[irank];                        //   Set local Xmax for irank
        Cell cell(*C0);                                         //   Send root cell
        cell.NCHILD = cell.NBODY = 0;                           //   Reset link to children and bodies
        sendCells.push_back(cell);                              //   Push it into send buffer
	int ibody = 0;                                          //   Current send body's offset
	int icell = 0;                                          //   Current send cell's offset
	if (C0->NCHILD == 0) {                                  //   If root cell is leaf
	  addSendBody(C0, ibody, icell);                        //    Add bodies to send
	}                                                       //   End if for root cell leaf
        traverseLET(C0, cycle, ibody, icell);                   //   Traverse tree to get LET
      }                                                         //  Endif for current rank
      sendCellCount[irank] = sendCells.size() - sendCellDispl[irank];// Send count for irank
    }                                                           // End loop over ranks
    logger::stopTimer("Set LET");                               // Stop timer
  }

  //! Get local essential tree from rank "irank".
  void getLET(Cells & cells, int irank) {
    std::stringstream event;                                    // Event name
    event << "Get LET from rank " << irank;                     // Create event name based on irank
    logger::startTimer(event.str());                            // Start timer
    for (int i=recvCellCount[irank]-1; i>=0; i--) {             // Loop over receive cells
      C_iter C = recvCells.begin() + recvCellDispl[irank] + i;  //  Iterator of receive cell
      if (C->NBODY != 0) {                                      //  If cell has bodies
        C->BODY = recvBodies.begin() + recvBodyDispl[irank] + C->IBODY;// Iterator of first body
      }                                                         //  End if for bodies
      if (i != 0) {                                             //  If cell is not root
        C_iter Cparent = recvCells.begin() + recvCellDispl[irank] + C->PARENT;// Iterator of parent cell
        Cparent->NBODY += C->NBODY;                             //   Accululate number of bodies
      }                                                         //  End if for root cell
    }                                                           // End loop over receive cells
    cells.resize(recvCellCount[irank]);                         // Resize cell vector for LET
    cells.assign(recvCells.begin()+recvCellDispl[irank],
		 recvCells.begin()+recvCellDispl[irank]+recvCellCount[irank]);
    logger::stopTimer(event.str());                             // Stop timer
  }

  //! Send bodies
  Bodies commBodies() {
    logger::startTimer("Comm bodies");                          // Start timer
    alltoall(sendBodies);                                       // Send body count
    alltoallv(sendBodies);                                      // Send bodies
    logger::stopTimer("Comm bodies");                           // Stop timer
    return recvBodies;                                          // Return received bodies
  }

  //! Send bodies
  Bodies commBodies(Bodies sendBodies) {
    logger::startTimer("Comm bodies");                          // Start timer
    alltoall(sendBodies);                                       // Send body count
    alltoallv(sendBodies);                                      // Send bodies
    logger::stopTimer("Comm bodies");                           // Stop timer
    return recvBodies;                                          // Return received bodies
  }

  //! Send cells
  void commCells() {
    logger::startTimer("Comm cells");                           // Start timer
    alltoall(sendCells);                                        // Send cell count
    alltoallv(sendCells);                                       // Senc cells
    logger::stopTimer("Comm cells");                            // Stop timer
  }

  //! Copy recvBodies to bodies
  Bodies getRecvBodies() {
    return recvBodies;                                          // Return recvBodies
  }

  //! Send bodies to next rank (round robin)
  void shiftBodies(Bodies & bodies) {
    int newSize;                                                // New number of bodies
    int oldSize = bodies.size();                                // Current number of bodies
    const int word = sizeof(bodies[0]) / 4;                     // Word size of body structure
    const int isend = (mpirank + 1          ) % mpisize;        // Send to next rank (wrap around)
    const int irecv = (mpirank - 1 + mpisize) % mpisize;        // Receive from previous rank (wrap around)
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

  //! Allgather bodies
  Bodies allgatherBodies(Bodies & bodies) {
    const int word = sizeof(bodies[0]) / 4;                     // Word size of body structure
    sendBodyCount[0] = bodies.size();                           // Determine send count
    MPI_Allgather(sendBodyCount, 1, MPI_INT,                    // Allgather number of bodies
                  recvBodyCount, 1, MPI_INT, MPI_COMM_WORLD);
    recvBodyDispl[0] = 0;                                       // Initialize receive displacement
    for (int irank=0; irank<mpisize-1; irank++) {               // Loop over ranks
      recvBodyDispl[irank+1] = recvBodyDispl[irank] + recvBodyCount[irank];// Set receive displacement
    }                                                           // End loop over ranks
    recvBodies.resize(recvBodyDispl[mpisize-1]+recvBodyCount[mpisize-1]);// Resize receive buffer
    for (int irank=0; irank<mpisize; irank++) {                 // Loop over ranks
      recvBodyCount[irank] *= word;                             //  Multiply receive count by word size of data
      recvBodyDispl[irank] *= word;                             //  Multiply receive displacement by word size of data
    }                                                           // End loop over ranks
    MPI_Allgatherv(&bodies[0], sendBodyCount[0]*word, MPI_FLOAT,// Allgather bodies
                   &recvBodies[0], recvBodyCount, recvBodyDispl, MPI_FLOAT, MPI_COMM_WORLD);
    return recvBodies;                                          // Return bodies
  }
};
#endif
