#ifndef fft_h
#define fft_h
#include <mpi.h>
#include <fftw3.h>
#include <cmath>
#include <fstream>

class FastFourierTransform {
private:
  int nxLocal;
  int numSend;
  float *Ek;
  int   *Nk;
  fftw_complex *vec1d;
  fftw_complex *vec2d;
  fftw_plan forward1d;
  fftw_plan forward2d;
  fftw_plan backward1d;
  fftw_plan backward2d;

protected:
  float *realSend;
  float *imagSend;
  float *realRecv;
  float *imagRecv;

public:
  const int nx;
  int numBodies;

public:
  FastFourierTransform(int N) : nx(N) {
    nxLocal    = nx;
    numBodies  = nx * nx * nxLocal;
    numSend    = nx * nxLocal * nxLocal;
    Nk         = new int   [nx];
    Ek         = new float [nx];
    realSend   = new float [numBodies];
    imagSend   = new float [numBodies];
    realRecv   = new float [numBodies];
    imagRecv   = new float [numBodies];
    vec1d      = (fftw_complex*) fftw_malloc(nx *      sizeof(fftw_complex));
    vec2d      = (fftw_complex*) fftw_malloc(nx * nx * sizeof(fftw_complex));
    forward1d  = fftw_plan_dft_1d(nx,     vec1d, vec1d, FFTW_FORWARD,  FFTW_ESTIMATE);
    forward2d  = fftw_plan_dft_2d(nx, nx, vec2d, vec2d, FFTW_FORWARD,  FFTW_ESTIMATE);
    backward1d = fftw_plan_dft_1d(nx,     vec1d, vec1d, FFTW_BACKWARD, FFTW_ESTIMATE);
    backward2d = fftw_plan_dft_2d(nx, nx, vec2d, vec2d, FFTW_BACKWARD, FFTW_ESTIMATE);
  }

  ~FastFourierTransform() {
    fftw_destroy_plan(forward1d);
    fftw_destroy_plan(forward2d);
    fftw_destroy_plan(backward1d);
    fftw_destroy_plan(backward2d);
    fftw_free(vec1d);
    fftw_free(vec2d);
    delete[] Nk;
    delete[] Ek;
    delete[] realSend;
    delete[] imagSend;
    delete[] realRecv;
    delete[] imagRecv;
  }

  void forwardFFT() {
    for( int ix=0; ix<nxLocal; ++ix ) {
      for( int iy=0; iy<nx; ++iy ) {
        for( int iz=0; iz<nx; ++iz ) {
          int i = ix * nx * nx + iy * nx + iz;
          vec2d[iz+iy*nx][0] = realRecv[i] / nx / nx;
          vec2d[iz+iy*nx][1] = 0;
        }
      }
      fftw_execute(forward2d);
      for( int iy=0; iy<nx; ++iy ) {
        for( int iz=0; iz<nx; ++iz ) {
          int i = iz * nx * nxLocal + iy * nxLocal + ix;
          realSend[i] = vec2d[iz+iy*nx][0];
          imagSend[i] = vec2d[iz+iy*nx][1];
        }
      }
    }
    MPI_Alltoall(realSend,numSend,MPI_FLOAT,realRecv,numSend,MPI_FLOAT,MPI_COMM_WORLD);
    MPI_Alltoall(imagSend,numSend,MPI_FLOAT,imagRecv,numSend,MPI_FLOAT,MPI_COMM_WORLD);
    for( int ix=0; ix<nxLocal; ++ix ) {
      for( int iy=0; iy<nx; ++iy ) {
        for( int iz=0; iz<nx; ++iz ) {
          int iiz = iz % nxLocal;
          int iix = ix + (iz / nxLocal) * nxLocal;
          int i = iix * nx * nxLocal + iy * nxLocal + iiz;
          vec1d[iz][0] = realRecv[i] / nx;
          vec1d[iz][1] = imagRecv[i] / nx;
        }
        fftw_execute(forward1d);
        for( int iz=0; iz<nx; ++iz ) {
          int i = ix * nx * nx + iy * nx + iz;
          realSend[i] = vec1d[iz][0];
          imagSend[i] = vec1d[iz][1];
        }
      }
    }
  }

  void initSpectrum() {
    for( int k=0; k<nx; ++k ) {
      Ek[k] = Nk[k] = 0;
    }
    for( int ix=0; ix<nx/2; ++ix ) {
      for( int iy=0; iy<nx/2; ++iy ) {
        for( int iz=0; iz<nx/2; ++iz ) {
          int k = floor(sqrtf(ix * ix + iy * iy + iz * iz));
          Nk[k]++;
        }
      }
    }
  }

  void addSpectrum() {
    for( int ix=0; ix<nx/2; ++ix ) {
      for( int iy=0; iy<nx/2; ++iy ) {
        for( int iz=0; iz<nx/2; ++iz ) {
          int i = ix * nx * nx + iy * nx + iz;
          int k = floor(sqrtf(ix * ix + iy * iy + iz * iz));
          Ek[k] += (realSend[i] * realSend[i] + imagSend[i] * imagSend[i]) * 4 * M_PI * k * k;
        }
      }
    }
  }

  void writeSpectrum() {
    std::ofstream fid("statistics.dat",std::ios::in | std::ios::app);
    for( int k=0; k<nx; ++k ) {
      if( Nk[k] == 0 ) Nk[k] = 1;
      Ek[k] /= Nk[k];
      fid << Ek[k] << std::endl;
    }
    fid.close();
  }

};

#endif