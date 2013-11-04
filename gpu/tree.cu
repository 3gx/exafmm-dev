#include "octree.h"

static __device__ void pairMinMax(float3 &xmin, float3 &xmax,
                                  float4 reg_min, float4 reg_max) {
  xmin.x = fminf(xmin.x, reg_min.x);
  xmin.y = fminf(xmin.y, reg_min.y);
  xmin.z = fminf(xmin.z, reg_min.z);
  xmax.x = fmaxf(xmax.x, reg_max.x);
  xmax.y = fmaxf(xmax.y, reg_max.y);
  xmax.z = fmaxf(xmax.z, reg_max.z);
}

static __device__ void pairMinMax(int i, int j, float3 &xmin, float3 &xmax,
                                  volatile float3 *sh_xmin, volatile float3 *sh_xmax) {
  sh_xmin[i].x = xmin.x = fminf(xmin.x, sh_xmin[j].x);
  sh_xmin[i].y = xmin.y = fminf(xmin.y, sh_xmin[j].y);
  sh_xmin[i].z = xmin.z = fminf(xmin.z, sh_xmin[j].z);
  sh_xmax[i].x = xmax.x = fmaxf(xmax.x, sh_xmax[j].x);
  sh_xmax[i].y = xmax.y = fmaxf(xmax.y, sh_xmax[j].y);
  sh_xmax[i].z = xmax.z = fmaxf(xmax.z, sh_xmax[j].z);
}

static __device__ void sharedMinMax(float3 &xmin, float3 &xmax) {
  volatile __shared__ float3 sh_xmin[NCRIT];
  volatile __shared__ float3 sh_xmax[NCRIT];
  sh_xmin[threadIdx.x].x = xmin.x;
  sh_xmin[threadIdx.x].y = xmin.y;
  sh_xmin[threadIdx.x].z = xmin.z;
  sh_xmax[threadIdx.x].x = xmax.x;
  sh_xmax[threadIdx.x].y = xmax.y;
  sh_xmax[threadIdx.x].z = xmax.z;

  __syncthreads();
  if(blockDim.x >= 512 && threadIdx.x < 256)
    pairMinMax(threadIdx.x, threadIdx.x + 256, xmin, xmax, sh_xmin, sh_xmax);
  __syncthreads();
  if(blockDim.x >= 256 && threadIdx.x < 128)
    pairMinMax(threadIdx.x, threadIdx.x + 128, xmin, xmax, sh_xmin, sh_xmax);
  __syncthreads();
  if(blockDim.x >= 128 && threadIdx.x < 64)
    pairMinMax(threadIdx.x, threadIdx.x + 64, xmin, xmax, sh_xmin, sh_xmax);
  __syncthreads();
  if(blockDim.x >= 64 && threadIdx.x < 32)
    pairMinMax(threadIdx.x, threadIdx.x + 32, xmin, xmax, sh_xmin, sh_xmax);
  if(blockDim.x >= 32 && threadIdx.x < 16)
    pairMinMax(threadIdx.x, threadIdx.x + 16, xmin, xmax, sh_xmin, sh_xmax);
  if(threadIdx.x < 8) {
    pairMinMax(threadIdx.x, threadIdx.x +  8, xmin, xmax, sh_xmin, sh_xmax);
    pairMinMax(threadIdx.x, threadIdx.x +  4, xmin, xmax, sh_xmin, sh_xmax);
    pairMinMax(threadIdx.x, threadIdx.x +  2, xmin, xmax, sh_xmin, sh_xmax);
    pairMinMax(threadIdx.x, threadIdx.x +  1, xmin, xmax, sh_xmin, sh_xmax);
  }
}

static __device__ uint4 getKey(int4 index3) {
  const int bits = 30;
  const int C[8] = {0, 1, 7, 6, 3, 2, 4, 5};
  uint4 key4 = {0, 0, 0, 0};
  int mask = 1 << (bits - 1);
  int key = 0;
  for( int i=0; i<bits; i++, mask >>= 1) {
    int xi = (index3.x & mask) ? 1 : 0;
    int yi = (index3.y & mask) ? 1 : 0;
    int zi = (index3.z & mask) ? 1 : 0;        
    int index = (xi << 2) + (yi << 1) + zi;
    if(index == 0) {
      index3.w = index3.z;
      index3.z = index3.y;
      index3.y = index3.w;
    } else if(index == 1 || index == 5) {
      index3.w = index3.x;
      index3.x = index3.y;
      index3.y = index3.w;
    } else if(index == 4 || index == 6) {
      index3.x = (index3.x) ^ (-1);
      index3.z = (index3.z) ^ (-1);
    } else if(index == 7 || index == 3) {
      index3.w = (index3.x) ^ (-1);         
      index3.x = (index3.y) ^ (-1);
      index3.y = index3.w;
    } else {
      index3.w = (index3.z) ^ (-1);         
      index3.z = (index3.y) ^ (-1);
      index3.y = index3.w;          
    }   
    key = (key << 3) + C[index];
    if(i == 19) {
      key4.y = key;
      key = 0;
    }
    if(i == 9) {
      key4.x = key;
      key = 0;
    }
  }
  key4.z = key;
  return key4;
}

static __device__ uint4 getMask(int level) {
  int mask_levels = 3 * (MAXLEVELS - level);
  uint4 mask = {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF};
  if (mask_levels > 60) {
    mask.z = 0;
    mask.y = 0;
    mask.x = (mask.x >> (mask_levels - 60)) << (mask_levels - 60);
  } else if (mask_levels > 30) {
    mask.z = 0;
    mask.y = (mask.y >> (mask_levels - 30)) << (mask_levels - 30);
  } else {
    mask.z = (mask.z >> mask_levels) << mask_levels;
  }
  return mask;
}

static __device__ int compareKey(uint4 a, uint4 b) {
  if      (a.x < b.x) return -1;
  else if (a.x > b.x) return +1;
  else {
    if       (a.y < b.y) return -1;
    else  if (a.y > b.y) return +1;
    else {
      if       (a.z < b.z) return -1;
      else  if (a.z > b.z) return +1;
      return 0;
    }
  }
}

//Binary search of the key within certain bounds (cij.x, cij.y)
static __device__ int findKey(uint4 key, uint2 cij, uint4 *keys) {
  int l = cij.x;
  int r = cij.y - 1;
  while (r - l > 1) {
    int m = (r + l) >> 1;
    int cmp = compareKey(keys[m], key);
    if (cmp == -1)
      l = m;
    else 
      r = m;
  }
  if (compareKey(keys[l], key) >= 0) return l;
  return r;
}

extern "C" __global__ void boundaryReduction(const int numBodies,
                                             float4 *bodyPos,
                                             float3 *output_xmin,
                                             float3 *output_xmax)
{
  float3 xmin = make_float3(+1e10f, +1e10f, +1e10f);
  float3 xmax = make_float3(-1e10f, -1e10f, -1e10f);
  uint idx = blockIdx.x * blockDim.x + threadIdx.x;
  const uint stride = blockDim.x * gridDim.x;
  while (idx < numBodies) {
    float4 pos = bodyPos[idx];
    pairMinMax(xmin, xmax, pos, pos);
    idx += stride;
  }
  sharedMinMax(xmin,xmax);

  if( threadIdx.x == 0 ) {
    output_xmin[blockIdx.x].x = xmin.x;
    output_xmin[blockIdx.x].y = xmin.y;
    output_xmin[blockIdx.x].z = xmin.z;
    output_xmax[blockIdx.x].x = xmax.x;
    output_xmax[blockIdx.x].y = xmax.y;
    output_xmax[blockIdx.x].z = xmax.z;
  }
}

extern "C" __global__ void getKeyKernel(int numBodies,
                                        float4 corner,
                                        float4 *bodyPos,
                                        uint4 *bodyKeys) {
  const uint idx = blockIdx.x * blockDim.x + threadIdx.x;
  if (idx >= numBodies) return;
  float4 pos = bodyPos[idx];
  int4 index3;
  index3.x = (int)roundf(__fdividef(pos.x - corner.x, corner.w));
  index3.y = (int)roundf(__fdividef(pos.y - corner.y, corner.w));
  index3.z = (int)roundf(__fdividef(pos.z - corner.z, corner.w));
  uint4 key = getKey(index3);
  key.w = idx;
  bodyKeys[idx] = key;
}

extern "C" __global__ void getValidRange(int numBodies,
                                         int level,
                                         uint4 *bodyKeys,
                                         uint *validRange,
                                         const uint *workToDo) {
  if (*workToDo == 0) return;
  const uint idx = blockIdx.x * blockDim.x + threadIdx.x;
  if (idx >= numBodies) return;
  const uint4 key_F = {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF};
  uint4 mask = getMask(level);
  uint4 key_c = bodyKeys[idx];
  uint4 key_m;
  if( idx == 0 )
    key_m = key_F;
  else
    key_m = bodyKeys[idx-1];

  uint4 key_p;
  if( idx == numBodies-1 )
    key_p = key_F;
  else
    key_p = bodyKeys[idx+1];

  int valid0 = 0;
  int valid1 = 0;
  if (compareKey(key_c, key_F) != 0) {
    key_c.x = key_c.x & mask.x;
    key_c.y = key_c.y & mask.y;
    key_c.z = key_c.z & mask.z;
    key_p.x = key_p.x & mask.x;
    key_p.y = key_p.y & mask.y;
    key_p.z = key_p.z & mask.z;
    key_m.x = key_m.x & mask.x;
    key_m.y = key_m.y & mask.y;
    key_m.z = key_m.z & mask.z;
    valid0 = abs(compareKey(key_c, key_m));
    valid1 = abs(compareKey(key_c, key_p));
  }
  validRange[idx*2]   = idx | ((valid0) << 31);
  validRange[idx*2+1] = idx | ((valid1) << 31);
}

extern "C" __global__ void buildNodes(
                             uint level,
                             uint *workToDo,
                             uint *maxLevel,
                             uint2 *levelRange,
                             uint *bodyOffset,
                             uint4 *bodyKeys,
                             uint4 *cellKeys,
                             uint2 *bodyRange) {
  if( *workToDo == 0 ) return;
  uint idx  = blockIdx.x * blockDim.x + threadIdx.x;
  const uint stride = gridDim.x * blockDim.x;
  uint n = (*workToDo) / 2;
  uint offset;
  if( level == 0 )
    offset = 0;
  else
    offset = levelRange[level-1].y;

  while( idx < n ) {
    uint begin = bodyOffset[idx*2];
    uint end = bodyOffset[idx*2+1]+1;
    uint4 key  = bodyKeys[begin];
    uint4 mask = getMask(level);
    key = make_uint4(key.x & mask.x, key.y & mask.y, key.z & mask.z, level); 
    bodyRange[offset+idx] = make_uint2(begin, end);
    cellKeys  [offset+idx] = key;
    if( end - begin <= NLEAF )
      for( int i=begin; i<end; i++ )
        bodyKeys[i] = make_uint4(0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF);
    idx += stride;
  }

  if( threadIdx.x == 0 && blockIdx.x == 0 ) {
    levelRange[level] = make_uint2(offset, offset + n);
    *maxLevel = level;
  }
}

extern "C" __global__ void linkNodes(int numSources,
                                     float4 corner,
                                     uint2 *bodyRange,
                                     uint4 *cellKeys,
                                     uint *childRange,
                                     uint2 *levelRange,
                                     uint* validRange) {
  const uint idx = blockIdx.x * blockDim.x + threadIdx.x;
  if (idx >= numSources) return;
  uint4 key = cellKeys[idx];
  uint level = key.w;
  uint begin = bodyRange[idx].x;
  uint end   = bodyRange[idx].y;

  uint4 mask = getMask(level-1);
  key = make_uint4(key.x & mask.x, key.y & mask.y,  key.z & mask.z, 0);
  if(idx > 0) {
    int ci = findKey(key,levelRange[level-1],cellKeys);
    atomicAdd(&childRange[ci], (1 << 28));
  }

  key = cellKeys[idx];
  mask = getMask(level);
  key = make_uint4(key.x & mask.x, key.y & mask.y, key.z & mask.z, 0);
  int cj = findKey(key,levelRange[level+1],cellKeys);
  atomicOr(&childRange[idx], cj);

  uint valid = idx;
  if( end - begin <= NLEAF )
    valid = idx | (uint)(1 << 31);
  validRange[idx] = valid;
}

extern "C" __global__ void getLevelRange(const int numSources,
                                         const int numLeafs,
                                         uint *cellIndex,
                                         uint4 *cellKeys,
                                         uint* validRange) {
  uint idx = blockIdx.x * blockDim.x + threadIdx.x + numLeafs;
  if (idx >= numSources) return;
  const int cellIdx = cellIndex[idx];
  int level_c, level_m, level_p;
  level_c = cellKeys[cellIndex[idx]].w;
  if( idx+1 < numSources )
    level_p = cellKeys[cellIndex[idx+1]].w;
  else
    level_p = MAXLEVELS+5;
  if(cellIdx == 0)
    level_m = -1;    
  else
    level_m = cellKeys[cellIndex[idx-1]].w;
  validRange[(idx-numLeafs)*2]   = idx | (level_c != level_m) << 31;
  validRange[(idx-numLeafs)*2+1] = idx | (level_c != level_p) << 31;
}

extern "C" __global__ void getTargetRange(int numBodies,
                                          uint *validRange,
                                          uint *levelOffset,
                                          int treeDepth) {
  const uint idx = blockIdx.x * blockDim.x + threadIdx.x;
  if (idx >= numBodies) return;
  __shared__ int shmem[128];
  if(blockIdx.x == 0) {
    if(threadIdx.x < (MAXLEVELS*2))
      shmem[threadIdx.x] = levelOffset[threadIdx.x];
    __syncthreads();
    if(threadIdx.x < MAXLEVELS) {
      levelOffset[threadIdx.x]  = shmem[threadIdx.x*2];
      if(threadIdx.x == treeDepth-1)
        levelOffset[threadIdx.x] = shmem[threadIdx.x*2-1]+1;
    }
  }
  int validBegin = ((idx     % NCRIT) == 0);
  int validEnd   = (((idx+1) % NCRIT) == 0);
  if(idx+1 == numBodies) validEnd = 1;
  validRange[2*idx + 0] = (idx)   | (uint)(validBegin << 31);
  validRange[2*idx + 1] = (idx+1) | (uint)(validEnd   << 31);    
}

extern "C" __global__ void storeTargetRange(int numTargets,
                                            uint *validRange,
                                            uint2 *targetRange) {
  if(blockIdx.x >= numTargets) return;
  if(threadIdx.x == 0) {
    int begin = validRange[2*blockIdx.x];
    int end   = validRange[2*blockIdx.x+1];
    targetRange[blockIdx.x] = make_uint2(begin,end);
  }
}

extern "C" __global__ void reorder(const int size, uint4 *index, float4 *input, float4* output) {
  const uint idx = blockIdx.x * blockDim.x + threadIdx.x;
  if (idx >= size) return;
  int newIndex = index[idx].w;
  output[idx] = input[newIndex];
}

extern "C" __global__ void P2M(const int numLeafs,
                               uint *cellIndex,
                               uint2 *bodyRange,
                               float4 *bodyPos,
                               float4 *cellPos,
                               float4 *cellXmin,
                               float4 *cellXmax,
                               float  *multipole) {
  const uint idx = blockIdx.x * blockDim.x + threadIdx.x;
  if (idx >= numLeafs) return;
  int cellIdx = cellIndex[idx];
  const uint begin = bodyRange[cellIdx].x;
  const uint end   = bodyRange[cellIdx].y;
  float4 mon = {0.0f, 0.0f, 0.0f, 0.0f};
  float3 xmin, xmax;
  xmin = make_float3(+1e10f, +1e10f, +1e10f);
  xmax = make_float3(-1e10f, -1e10f, -1e10f);
  for( int i=begin; i<end; i++ ) {
    float4 pos = bodyPos[i];
    mon.w += pos.w;
    mon.x += pos.w * pos.x;
    mon.y += pos.w * pos.y;
    mon.z += pos.w * pos.z;
    pairMinMax(xmin, xmax, pos, pos);
  }
  float im = 1.0/mon.w;
  if(mon.w == 0) im = 0;
  mon.x *= im;
  mon.y *= im;
  mon.z *= im;
  cellPos[cellIdx] = make_float4(mon.x, mon.y, mon.z, mon.w);
  cellXmin[cellIdx] = make_float4(xmin.x, xmin.y, xmin.z, 0.0f);
  cellXmax[cellIdx] = make_float4(xmax.x, xmax.y, xmax.z, 1.0f);
  return;
}

extern "C" __global__ void M2M(const int level,
                               uint *cellIndex,
                               uint *levelOffset,
                               uint *childRange,
                               float4 *cellPos,
                               float4 *cellXmin,
                               float4 *cellXmax,
                               float  *multipole) {
  const uint idx = blockIdx.x * blockDim.x + threadIdx.x + levelOffset[level-1];
  if(idx >= levelOffset[level]) return;
  const int cellIdx = cellIndex[idx];
  const uint begin = childRange[cellIdx] & 0x0FFFFFFF;
  const uint nchild = ((childRange[cellIdx] & 0xF0000000) >> 28);
  const uint end = begin + nchild;
  childRange[cellIdx] = begin | ((nchild-1) << LEAFBIT);
  float4 mon = {0.0f, 0.0f, 0.0f, 0.0f};
  float3 xmin = make_float3(+1e10f, +1e10f, +1e10f);
  float3 xmax = make_float3(-1e10f, -1e10f, -1e10f);
  for( int i=begin; i<end; i++ ) {
    float4 pos = cellPos[i];
    mon.w += pos.w;
    mon.x += pos.w * pos.x;
    mon.y += pos.w * pos.y;
    mon.z += pos.w * pos.z;
    pairMinMax(xmin, xmax, cellXmin[i], cellXmax[i]);
  }
  float im = 1.0 / mon.w;
  if(mon.w == 0) im = 0;
  mon.x *= im;
  mon.y *= im;
  mon.z *= im;
  cellPos[cellIdx] = make_float4(mon.x, mon.y, mon.z, mon.w);
  cellXmin[cellIdx] = make_float4(xmin.x, xmin.y, xmin.z, 0.0f);
  cellXmax[cellIdx] = make_float4(xmax.x, xmax.y, xmax.z, 0.0f);
  return;
}

extern "C" __global__ void rescale(const int node_count,
                                   float4 *cellPos,
                                   float4 *cellXmin,
                                   float4 *cellXmax,
                                   uint  *childRange,
                                   float *openingAngle,
                                   uint2 *bodyRange){
  const uint idx = blockIdx.x * blockDim.x + threadIdx.x;
  if(idx >= node_count) return;
  float4 mon = cellPos[idx];
  float4 xmin = cellXmin[idx];
  float4 xmax = cellXmax[idx];
  float3 boxCenter = make_float3(0.5*(xmin.x + xmax.x),
                                 0.5*(xmin.y + xmax.y),
                                 0.5*(xmin.z + xmax.z));
  float3 boxSize = make_float3(fmaxf(fabs(boxCenter.x-xmin.x), fabs(boxCenter.x-xmax.x)),
                               fmaxf(fabs(boxCenter.y-xmin.y), fabs(boxCenter.y-xmax.y)),
                               fmaxf(fabs(boxCenter.z-xmin.z), fabs(boxCenter.z-xmax.z)));
  float3 dX = make_float3((boxCenter.x - mon.x), (boxCenter.y - mon.y), (boxCenter.z - mon.z));
  float R = sqrt((dX.x*dX.x) + (dX.y*dX.y) + (dX.z*dX.z));
  if(fabs(mon.w) < EPS) R = 0;

  float length = 2 * fmaxf(boxSize.x, fmaxf(boxSize.y, boxSize.z));
  if(length < EPS) length = EPS;
  float cellOp = length / THETA + R;
  cellOp = cellOp * cellOp;
  uint pfirst = bodyRange[idx].x;
  uint nchild = bodyRange[idx].y - pfirst;
  bool leaf = (xmax.w > 0);

  if( leaf ) {
    cellOp = -cellOp;
    pfirst = pfirst | ((nchild-1) << LEAFBIT);
    childRange[idx] = pfirst;
  }
  openingAngle[idx] = cellOp;
  return;
}

extern "C" __global__ void setTargets(const int numTargets,
                                      float4 *bodyPos,
                                      int2   *targetRange,
                                      float4 *targetCenterInfo,
                                      float4 *targetSizeInfo){
  if(blockIdx.x >= numTargets) return;
  float3 xmin = make_float3(+1e10f, +1e10f, +1e10f);
  float3 xmax = make_float3(-1e10f, -1e10f, -1e10f);
  int begin = targetRange[blockIdx.x].x;
  int end   = targetRange[blockIdx.x].y;
  int idx = begin + threadIdx.x;
  if( idx < end ) {
    float4 pos = bodyPos[idx];
    pairMinMax(xmin, xmax, pos, pos);
  }
  sharedMinMax(xmin,xmax);
  if( threadIdx.x == 0 ) {
    float3 targetCenter = make_float3(0.5*(xmin.x + xmax.x),
                                     0.5*(xmin.y + xmax.y),
                                     0.5*(xmin.z + xmax.z));
    float3 targetSize = make_float3(fmaxf(fabs(targetCenter.x-xmin.x), fabs(targetCenter.x-xmax.x)),
                                   fmaxf(fabs(targetCenter.y-xmin.y), fabs(targetCenter.y-xmax.y)),
                                   fmaxf(fabs(targetCenter.z-xmin.z), fabs(targetCenter.z-xmax.z)));
    int nchild = end-begin;
    begin = begin | (nchild-1) << CRITBIT;
    targetSizeInfo[blockIdx.x].x = targetSize.x;
    targetSizeInfo[blockIdx.x].y = targetSize.y;
    targetSizeInfo[blockIdx.x].z = targetSize.z;
    targetSizeInfo[blockIdx.x].w = __int_as_float(begin);
    float length = max(targetSize.x, max(targetSize.y, targetSize.z));
    targetCenterInfo[blockIdx.x].x = targetCenter.x;
    targetCenterInfo[blockIdx.x].y = targetCenter.y;
    targetCenterInfo[blockIdx.x].z = targetCenter.z;
    targetCenterInfo[blockIdx.x].w = length;
  }
}

void octree::getBoundaries() {
  boundaryReduction<<<64,NCRIT>>>(numBodies,bodyPos.devc(),XMIN.devc(),XMAX.devc());
  XMIN.d2h();
  XMAX.d2h();
  float4 xmin = make_float4(+1e10, +1e10, +1e10, +1e10);
  float4 xmax = make_float4(-1e10, -1e10, -1e10, -1e10);
  for (int i = 0; i < 64; i++) {
    xmin.x = std::min(xmin.x, XMIN[i].x);
    xmin.y = std::min(xmin.y, XMIN[i].y);
    xmin.z = std::min(xmin.z, XMIN[i].z);
    xmax.x = std::max(xmax.x, XMAX[i].x);
    xmax.y = std::max(xmax.y, XMAX[i].y);
    xmax.z = std::max(xmax.z, XMAX[i].z);
  }
  float size = 1.001f*std::max(xmax.z - xmin.z,
                      std::max(xmax.y - xmin.y, xmax.x - xmin.x));
  corner = make_float4(0.5f*(xmin.x + xmax.x) - 0.5f*size,
                       0.5f*(xmin.y + xmax.y) - 0.5f*size,
                       0.5f*(xmin.z + xmax.z) - 0.5f*size,
                       size / (1 << MAXLEVELS));
}

void octree::getKeys() {
  int threads = 128;
  int blocks = ALIGN(numBodies,threads);
  getKeyKernel<<<blocks,threads>>>(numBodies,corner,bodyPos.devc(),uint4buffer);
}

void octree::sortKeys() {
  sorter->sort(uint4buffer,bodyKeys,numBodies);
}

void octree::sortBodies() {
  int threads = 512;
  int blocks = ALIGN(numBodies,threads);
  reorder<<<blocks,threads>>>(numBodies,bodyKeys.devc(),bodyPos.devc(),float4buffer);
  CU_SAFE_CALL(cudaMemcpy(bodyPos.devc(),float4buffer,numBodies*sizeof(float4),cudaMemcpyDeviceToDevice));
}

void octree::buildTree() {
  cudaVec<uint> maxLevel;
  maxLevel.alloc(1);
  validRange.zeros();
  levelRange.zeros();
  workToDo.ones();
  int threads = 128;
  int blocks = ALIGN(numBodies,threads);
  for( int level=0; level<MAXLEVELS; level++ ) {
    getValidRange<<<blocks,threads>>>(numBodies,level,bodyKeys.devc(),validRange.devc(),workToDo.devc());
    gpuCompact(validRange,compactRange,2*numBodies);
    buildNodes<<<64,threads>>>(level,workToDo.devc(),maxLevel.devc(),levelRange.devc(),compactRange.devc(),bodyKeys.devc(),cellKeys.devc(),bodyRange.devc());
  }
  maxLevel.d2h();
  numLevels = maxLevel[0];
  levelRange.d2h();
  numSources = levelRange[numLevels].y;
}

void octree::linkTree() {
  // cellIndex
  childRange.zeros();
  int threads = 128;
  int blocks = ALIGN(numSources,threads);
  linkNodes<<<blocks,threads>>>(numSources,corner,bodyRange.devc(),cellKeys.devc(),childRange.devc(),levelRange.devc(),validRange.devc());
  cellIndex.alloc(numSources);
  workToDo.ones();
  gpuSplit(validRange, cellIndex, numSources);
  workToDo.d2h();
  numLeafs = workToDo[0];
  // levelOffset
  validRange.zeros();
  blocks = ALIGN(numSources-numLeafs,threads);
  getLevelRange<<<blocks,threads>>>(numSources,numLeafs,cellIndex.devc(),cellKeys.devc(),validRange.devc());
  gpuCompact(validRange, levelOffset, 2*(numSources-numLeafs));
  // targetRange
  validRange.zeros();
  blocks = ALIGN(numBodies,threads);
  getTargetRange<<<blocks,threads>>>(numBodies,validRange.devc(),levelOffset.devc(),numLevels+1);
  gpuCompact(validRange, compactRange, numBodies*2);
  workToDo.d2h();
  numTargets = workToDo[0] / 2;
  targetRange.alloc(numTargets);
  storeTargetRange<<<numTargets,NCRIT>>>(numTargets,compactRange.devc(),targetRange.devc());
}

void octree::allocateTreePropMemory()
{
  cellPos.alloc(numSources);
  multipole.alloc(MTERM*numSources);
  targetSizeInfo.alloc(numSources);
  openingAngle.alloc(numSources);
  targetCenterInfo.alloc(numSources);
}

void octree::upward() {
  cudaVec<float4> cellXmin;
  cudaVec<float4> cellXmax;
  cellXmin.alloc(numSources);
  cellXmax.alloc(numSources);

  int threads = 128;
  int blocks = ALIGN(numLeafs,threads);
  P2M<<<blocks,threads>>>(numLeafs,cellIndex.devc(),bodyRange.devc(),bodyPos.devc(),
                                       cellPos.devc(),cellXmin.devc(),cellXmax.devc(),multipole.devc());

  levelOffset.d2h();
  for( int level=numLevels; level>=1; level-- ) {
    int totalOnThisLevel = levelOffset[level] - levelOffset[level-1];
    blocks = ALIGN(totalOnThisLevel,threads);
    M2M<<<blocks,threads>>>(level,cellIndex.devc(),levelOffset.devc(),childRange.devc(),
                                         cellPos.devc(),cellXmin.devc(),cellXmax.devc(),multipole.devc());
  }

  blocks = ALIGN(numSources,threads);
  rescale<<<blocks,threads>>>(numSources,cellPos.devc(),cellXmin.devc(),cellXmax.devc(),childRange.devc(),openingAngle.devc(),bodyRange.devc());
  setTargets<<<numTargets,NCRIT>>>(numTargets,bodyPos.devc(),(int2*)targetRange.devc(),targetCenterInfo.devc(),targetSizeInfo.devc());
}
