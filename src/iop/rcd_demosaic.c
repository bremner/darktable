/*
    This file is part of darktable,
    Copyright (C) 2010-2021 darktable developers.

    darktable is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    darktable is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with darktable.  If not, see <http://www.gnu.org/licenses/>.
*/

/*
* RATIO CORRECTED DEMOSAICING
* Luis Sanz Rodríguez (luis.sanz.rodriguez(at)gmail(dot)com)
*
* Release 2.3 @ 171125
*
* Original code from https://github.com/LuisSR/RCD-Demosaicing
* Licensed under the GNU GPL version 3
*/

/*
* The tiling coding was done by Ingo Weyrich (heckflosse67@gmx.de) from rawtherapee
*/

/*
* Luis Sanz Rodríguez significantly optimised the v 2.3 code and simplified the directional
* coefficients in an exact, shorter and more performant formula.
* In cooperation with Ingo Weyrich and Luis Sanz Rodríguez this has been tuned for performance.
* Hanno Schwalm (hanno@schwalm-bremen.de)
*/

/* Some notes about the algorithm
* 1. The calculated data at the tiling borders RCD_BORDER must be at least 9 to be stable.
* 2. For the outermost tiles we only have to discard a 6 pixel border region interpolated otherwise.
* 3. The tilesize has a significant influence on performance, the default is a good guess for modern
*    x86/64 machines, tested on Xeon E-2288G, i5-8250U.
*/

#ifndef RCD_TILESIZE
  #define RCD_TILESIZE 112
#endif

/* We don't want to use the -Ofast option in dt as it effects are not well specified and there have been issues
   leading to crashes.
   But we can use the 'fast-math' option in code sections if input data and algorithms are well defined.

   We have defined input data and make sure there are no divide-by-zero or overflows by chosen eps
   Reordering of instructions might lead to a slight loss of presision whigh is not significant here.
   Not necessary in this code section
     threadsafe handling of errno
     signed zero handling
     handling of math interrupts
     handling of rounding
     handling of overflows

   The 'fp-contract=fast' option enables fused multiply&add if available
*/

#ifdef __GNUC__
  #pragma GCC push_options
  #pragma GCC optimize ("fast-math", "fp-contract=fast")
#endif

#ifdef __GNUC__
  #define INLINE __inline
#else
  #define INLINE inline
#endif

#define FCRCD(row, col) (cfarray[(((row) & 1)<<1) | ((col) & 1)])

#define RCD_BORDER 9          // avoid tile-overlap errors
#define RCD_MARGIN 6          // for the outermost tiles we can have a smaller outer border
#define RCD_TILEVALID (RCD_TILESIZE - 2 * RCD_BORDER)
#define w1 RCD_TILESIZE
#define w2 (2 * RCD_TILESIZE)
#define w3 (3 * RCD_TILESIZE)
#define w4 (4 * RCD_TILESIZE)

#define eps 1e-5f              // Tolerance to avoid dividing by zero
#define epssq 1e-10f

static INLINE float intp(float a, float b, float c)
{   // taken from rt code
    // calculate a * b + (1 - a) * c
    // following is valid:
    // intp(a, b+x, c+x) = intp(a, b, c) + x
    // intp(a, b*x, c*x) = intp(a, b, c) * x
    return a * (b - c) + c;
}

// We might have negative data in input and also want to normalise
static INLINE float safe_in(float a, float scale)
{
  return fmaxf(0.0f, a) * scale;
}

// We don't want to use the SIMD version as we might access unaligned memory
static INLINE float sqrf(float a)
{
  return a * a;
}

// The border interpolation has been taken from rt, adapted to dt.
// The original dcraw based code had much stronger color artefacts in the border region.
static INLINE void approxit(float *out, const float *cfa, const float *sum, const int idx, const int c)
{
  float (*rgb)[4] = (void *)out;
  if(c == 1)
  {
    rgb[idx][0] = sum[0] / sum[3];
    rgb[idx][1] = fmaxf(0.0f, cfa[idx]);
    rgb[idx][2] = sum[2] / sum[5];
  }
  else
  {
    rgb[idx][1] =  sum[1] / sum[4];
    if (c == 0)
    {
      rgb[idx][0] = fmaxf(0.0f, cfa[idx]);
      rgb[idx][2] = sum[2] / sum[5];
    }
    else
    {
      rgb[idx][0] = sum[0] / sum[3];
      rgb[idx][2] = fmaxf(0.0f, cfa[idx]);
    }
  }
  rgb[idx][3] = 0.0f;
}

static void rcd_border_interpolate(dt_dev_pixelpipe_iop_t *piece, float *out, const float *cfa, dt_iop_roi_t *const roi_out,
                                   const dt_iop_roi_t *const roi_in, const uint32_t filters, const int border)
{
  const int width = roi_in->width;
  const int height = roi_in->height;

  if((width < 2 * border) || (height < 2 * border)) return;

  const int cfarray[4] = {FC(roi_in->y, roi_in->x, filters), FC(roi_in->y, roi_in->x + 1, filters), FC(roi_in->y + 1, roi_in->x, filters), FC(roi_in->y + 1, roi_in->x + 1, filters)};

  for(int i = 0; i < height; i++)
  {
    float sum[6];
    for(int j = 0; j < border; j++)
    {
      for(int x = 0; x < 6; x++) { sum[x] = 0.0f; }
      for(int i1 = i - 1; i1 < i + 2; i1++)
      {
        for(int j1 = j - 1; j1 < j + 2; j1++)
        {
          if((i1 > -1) && (i1 < height) && (j1 > -1))
          {
            const int c = FCRCD(i1, j1);
            sum[c] += fmaxf(0.0f, cfa[i1 * width + j1]);
            sum[c + 3]++;
          }
        }
      }
      approxit(out, cfa, sum, i * width + j, FCRCD(i, j));
    }

    for(int j = width - border; j < width; j++)
    {
      for(int x = 0; x < 6; x++) { sum[x] = 0.0f; }
      for(int i1 = i - 1; i1 < i + 2; i1++)
      {
        for(int j1 = j - 1; j1 < j + 2; j1++)
        {
          if((i1 > -1) && (i1 < height ) && (j1 < width))
          {
            const int c = FCRCD(i1, j1);
            sum[c] += fmaxf(0.0f, cfa[i1 * width + j1]);
            sum[c + 3]++;
          }
        }
      }
      approxit(out, cfa, sum, i * width + j, FCRCD(i, j));
    }
  }
  for(int i = 0; i < border; i++)
  {
    float sum[6];
    for(int j = border; j < width - border; j++)
    {
      for(int x = 0; x < 6; x++) { sum[x] = 0.0f; }
      for(int i1 = i - 1; i1 < i + 2; i1++)
      {
        for(int j1 = j - 1; j1 < j + 2; j1++)
        {
          if((i1 > -1) && (i1 < height) && (j1 > -1))
          {
            const int c = FCRCD(i1, j1);
            sum[c] += fmaxf(0.0f, cfa[i1 * width + j1]);
            sum[c + 3]++;
          }
        }
      }
      approxit(out, cfa, sum, i * width + j, FCRCD(i, j));
    }
  }
  for(int i = height - border; i < height; i++)
  {
    float sum[6];
    for(int j = border; j < width - border; j++)
    {
      for(int x = 0; x < 6; x++) { sum[x] = 0.0f; }
      for(int i1 = i - 1; i1 < i + 2; i1++)
      {
        for(int j1 = j - 1; j1 < j + 2; j1++)
        {
          if((i1 > -1) && (i1 < height) && (j1 < width))
          {
            const int c = FCRCD(i1, j1);
            sum[c] += fmaxf(0.0f, cfa[i1 * width + j1]);
            sum[c + 3]++;
          }
        }
      }
      approxit(out, cfa, sum, i * width + j, FCRCD(i, j));
    }
  }
}

#ifdef _OPENMP
  #pragma omp declare simd aligned(in, out)
#endif
static void rcd_demosaic(dt_dev_pixelpipe_iop_t *piece, float *const restrict out, const float *const restrict in, dt_iop_roi_t *const roi_out,
                                   const dt_iop_roi_t *const roi_in, const uint32_t filters)
{
  const int width = roi_in->width;
  const int height = roi_in->height;

  if((width < 16) || (height < 16))
  {
    dt_control_log(_("[rcd_demosaic] too small area"));
    return;
  }

  const float scaler = fmaxf(piece->pipe->dsc.processed_maximum[0], fmaxf(piece->pipe->dsc.processed_maximum[1], piece->pipe->dsc.processed_maximum[2]));
  const float revscaler = 1.0f / scaler;

  const int cfarray[4] = {FC(roi_in->y, roi_in->x, filters), FC(roi_in->y, roi_in->x + 1, filters), FC(roi_in->y + 1, roi_in->x, filters), FC(roi_in->y + 1, roi_in->x + 1, filters)};

  const int num_vertical = 1 + (height - 2 * RCD_BORDER -1) / RCD_TILEVALID;
  const int num_horizontal = 1 + (width - 2 * RCD_BORDER -1) / RCD_TILEVALID;

#ifdef _OPENMP
  #pragma omp parallel \
  dt_omp_firstprivate(width, height, cfarray, out, in, scaler, revscaler)
#endif
  {
    float *const VH_Dir = dt_alloc_align_float((size_t) RCD_TILESIZE * RCD_TILESIZE);
    memset(VH_Dir, 0, sizeof(*VH_Dir) * RCD_TILESIZE * RCD_TILESIZE);
    float *const PQ_Dir = dt_alloc_align_float((size_t) RCD_TILESIZE * RCD_TILESIZE / 2);
    memset(PQ_Dir, 0, sizeof(*PQ_Dir) * (RCD_TILESIZE * RCD_TILESIZE / 2));
    float *const cfa =    dt_alloc_align_float((size_t) RCD_TILESIZE * RCD_TILESIZE);
    memset(cfa, 0, sizeof(*cfa) * RCD_TILESIZE * RCD_TILESIZE);
    float *const P_CDiff_Hpf = dt_alloc_align_float((size_t) RCD_TILESIZE * RCD_TILESIZE / 2);
    memset(P_CDiff_Hpf, 0, sizeof(*P_CDiff_Hpf) * RCD_TILESIZE * RCD_TILESIZE / 2);
    float *const Q_CDiff_Hpf = dt_alloc_align_float((size_t) RCD_TILESIZE * RCD_TILESIZE / 2);
    memset(Q_CDiff_Hpf, 0, sizeof(*Q_CDiff_Hpf) * RCD_TILESIZE * RCD_TILESIZE / 2);

    float (*const rgb)[RCD_TILESIZE * RCD_TILESIZE] = (void *)dt_alloc_align_float((size_t)3 * RCD_TILESIZE * RCD_TILESIZE);

    // No overlapping use so re-use same buffer
    float *const lpf = PQ_Dir;

    // There has been a discussion about the schedule strategy, at least on the tested machines the
    // dynamic scheduling seems to be slightly faster.
#ifdef _OPENMP
  #pragma omp for schedule(simd:dynamic, 6) collapse(2) nowait
#endif
    for(int tile_vertical = 0; tile_vertical < num_vertical; tile_vertical++)
    {
      for(int tile_horizontal = 0; tile_horizontal < num_horizontal; tile_horizontal++)
      {
        const int rowStart = tile_vertical * RCD_TILEVALID;
        const int rowEnd = MIN(rowStart + RCD_TILESIZE, height);

        const int colStart = tile_horizontal * RCD_TILEVALID;
        const int colEnd = MIN(colStart + RCD_TILESIZE, width);

        const int tileRows = MIN(rowEnd - rowStart, RCD_TILESIZE);
        const int tileCols = MIN(colEnd - colStart, RCD_TILESIZE);

        // Step 0: fill data and make sure data are not negative.
        for(int row = rowStart; row < rowEnd; row++)
        {
          const int c0 = FCRCD(row, colStart);
          const int c1 = FCRCD(row, colStart + 1);
          for(int col = colStart, indx = (row - rowStart) * RCD_TILESIZE, in_indx = row * width + colStart; col < colEnd; col++, indx++, in_indx++)
          {
            cfa[indx] = rgb[c0][indx] = rgb[c1][indx] = safe_in(in[in_indx], revscaler);
          }
        }

        // STEP 1: Find vertical and horizontal interpolation directions
        float bufferV[3][RCD_TILESIZE - 8];
        // Step 1.1: Calculate the square of the vertical and horizontal color difference high pass filter
        for(int row = 3; row < MIN(tileRows - 3, 5); row++ )
        {
          for(int col = 4, indx = row * RCD_TILESIZE + col; col < tileCols - 4; col++, indx++ )
          {
            bufferV[row - 3][col - 4] = sqrf((cfa[indx - w3] - cfa[indx - w1] - cfa[indx + w1] + cfa[indx + w3]) - 3.0f * (cfa[indx - w2] + cfa[indx + w2]) + 6.0f * cfa[indx]);
          }
        }

        // Step 1.2: Obtain the vertical and horizontal directional discrimination strength
        float bufferH[RCD_TILESIZE] __attribute__((aligned(0x10)));
        // We start with V0, V1 and V2 pointing to row -1, row and row +1
        // After row is processed V0 must point to the old V1, V1 must point to the old V2 and V2 must point to the old V0
        // because the old V0 is not used anymore and will be filled with row + 1 data in next iteration
        float* V0 = bufferV[0];
        float* V1 = bufferV[1];
        float* V2 = bufferV[2];
        for(int row = 4; row < tileRows - 4; row++ )
        {
          for(int col = 3, indx = row * RCD_TILESIZE + col; col < tileCols - 3; col++, indx++)
          {
            bufferH[col - 3] = sqrf((cfa[indx -  3] - cfa[indx -  1] - cfa[indx +  1] + cfa[indx +  3]) - 3.0f * (cfa[indx -  2] + cfa[indx +  2]) + 6.0f * cfa[indx]);
          }
          for(int col = 4, indx = (row + 1) * RCD_TILESIZE + col; col < tileCols - 4; col++, indx++)
          {
            V2[col - 4] = sqrf((cfa[indx - w3] - cfa[indx - w1] - cfa[indx + w1] + cfa[indx + w3]) - 3.0f * (cfa[indx - w2] + cfa[indx + w2]) + 6.0f * cfa[indx]);
          }
          for(int col = 4, indx = row * RCD_TILESIZE + col; col < tileCols - 4; col++, indx++ )
          {
            const float V_Stat = fmaxf(epssq,      V0[col - 4] +      V1[col - 4] +      V2[col - 4]);
            const float H_Stat = fmaxf(epssq, bufferH[col - 4] + bufferH[col - 3] + bufferH[col - 2]);
            VH_Dir[indx] = V_Stat / ( V_Stat + H_Stat );
          }
          // rolling the line pointers
          float* tmp = V0; V0 = V1; V1 = V2; V2 = tmp;
        }

        // STEP 2: Calculate the low pass filter
        // Step 2.1: Low pass filter incorporating green, red and blue local samples from the raw data
        for(int row = 2; row < tileRows - 2; row++)
        {
          for(int col = 2 + (FCRCD(row, 0) & 1), indx = row * RCD_TILESIZE + col, lp_indx = indx / 2; col < tileCols - 2; col += 2, indx +=2, lp_indx++)
          {
            lpf[lp_indx] = cfa[indx]
                        + 0.5f * (cfa[indx - w1]     + cfa[indx + w1] +     cfa[indx - 1] +      cfa[indx + 1])
                       + 0.25f * (cfa[indx - w1 - 1] + cfa[indx - w1 + 1] + cfa[indx + w1 - 1] + cfa[indx + w1 + 1]);
          }
        }

        // STEP 3: Populate the green channel
        // Step 3.1: Populate the green channel at blue and red CFA positions
        for(int row = 4; row < tileRows - 4; row++)
        {
          for(int col = 4 + (FCRCD(row, 0) & 1), indx = row * RCD_TILESIZE + col, lpindx = indx / 2; col < tileCols - 4; col += 2, indx += 2, lpindx++)
          {
            const float cfai = cfa[indx];

            // Cardinal gradients
            const float N_Grad = eps + fabs(cfa[indx - w1] - cfa[indx + w1]) + fabs(cfai - cfa[indx - w2]) + fabs(cfa[indx - w1] - cfa[indx - w3]) + fabs(cfa[indx - w2] - cfa[indx - w4]);
            const float S_Grad = eps + fabs(cfa[indx - w1] - cfa[indx + w1]) + fabs(cfai - cfa[indx + w2]) + fabs(cfa[indx + w1] - cfa[indx + w3]) + fabs(cfa[indx + w2] - cfa[indx + w4]);
            const float W_Grad = eps + fabs(cfa[indx -  1] - cfa[indx +  1]) + fabs(cfai - cfa[indx -  2]) + fabs(cfa[indx -  1] - cfa[indx -  3]) + fabs(cfa[indx -  2] - cfa[indx -  4]);
            const float E_Grad = eps + fabs(cfa[indx -  1] - cfa[indx +  1]) + fabs(cfai - cfa[indx +  2]) + fabs(cfa[indx +  1] - cfa[indx +  3]) + fabs(cfa[indx +  2] - cfa[indx +  4]);

            // Cardinal pixel estimations
            const float lpfi = lpf[lpindx];
            const float N_Est = cfa[indx - w1] * (lpfi + lpfi) / (eps + lpfi + lpf[lpindx - w1]);
            const float S_Est = cfa[indx + w1] * (lpfi + lpfi) / (eps + lpfi + lpf[lpindx + w1]);
            const float W_Est = cfa[indx -  1] * (lpfi + lpfi) / (eps + lpfi + lpf[lpindx -  1]);
            const float E_Est = cfa[indx +  1] * (lpfi + lpfi) / (eps + lpfi + lpf[lpindx +  1]);

            // Vertical and horizontal estimations
            const float V_Est = (S_Grad * N_Est + N_Grad * S_Est) / (N_Grad + S_Grad);
            const float H_Est = (W_Grad * E_Est + E_Grad * W_Est) / (E_Grad + W_Grad);

            // G@B and G@R interpolation
            // Refined vertical and horizontal local discrimination
            const float VH_Central_Value = VH_Dir[indx];
            const float VH_Neighbourhood_Value = 0.25f * (VH_Dir[indx - w1 - 1] + VH_Dir[indx - w1 + 1] + VH_Dir[indx + w1 - 1] + VH_Dir[indx + w1 + 1]);
            const float VH_Disc = (fabs(0.5f - VH_Central_Value) < fabs(0.5f - VH_Neighbourhood_Value)) ? VH_Neighbourhood_Value : VH_Central_Value;

            rgb[1][indx] = intp(VH_Disc, H_Est, V_Est);
          }
        }

        // STEP 4: Populate the red and blue channels

        // Step 4.0: Calculate the square of the P/Q diagonals color difference high pass filter
        for(int row = 3; row < tileRows - 3; row++)
        {
          for(int col = 3, indx = row * RCD_TILESIZE + col, indx2 = indx / 2; col < tileCols - 3; col+=2, indx+=2, indx2++)
          {
            P_CDiff_Hpf[indx2] = sqrf((cfa[indx - w3 - 3] - cfa[indx - w1 - 1] - cfa[indx + w1 + 1] + cfa[indx + w3 + 3]) - 3.0f * (cfa[indx - w2 - 2] + cfa[indx + w2 + 2]) + 6.0f * cfa[indx]);
            Q_CDiff_Hpf[indx2] = sqrf((cfa[indx - w3 + 3] - cfa[indx - w1 + 1] - cfa[indx + w1 - 1] + cfa[indx + w3 - 3]) - 3.0f * (cfa[indx - w2 + 2] + cfa[indx + w2 - 2]) + 6.0f * cfa[indx]);
          }
        }
        // Step 4.1: Obtain the P/Q diagonals directional discrimination strength
        for(int row = 4; row < tileRows - 4; row++)
        {
          for(int col = 4 + (FCRCD(row, 0) & 1), indx = row * RCD_TILESIZE + col, indx2 = indx / 2, indx3 = (indx - w1 - 1) / 2, indx4 = (indx + w1 - 1) / 2; col < tileCols - 4; col += 2, indx += 2, indx2++, indx3++, indx4++ )
          {
            const float P_Stat = fmaxf(epssq, P_CDiff_Hpf[indx3]     + P_CDiff_Hpf[indx2] + P_CDiff_Hpf[indx4 + 1]);
            const float Q_Stat = fmaxf(epssq, Q_CDiff_Hpf[indx3 + 1] + Q_CDiff_Hpf[indx2] + Q_CDiff_Hpf[indx4]);
            PQ_Dir[indx2] = P_Stat / (P_Stat + Q_Stat);
          }
        }

        // Step 4.2: Populate the red and blue channels at blue and red CFA positions
        for(int row = 4; row < tileRows - 4; row++)
        {
          for(int col = 4 + (FCRCD(row, 0) & 1), indx = row * RCD_TILESIZE + col, c = 2 - FCRCD(row, col), pqindx = indx / 2, pqindx2 = (indx - w1 - 1) / 2, pqindx3 = (indx + w1 - 1) / 2; col < tileCols - 4; col += 2, indx += 2, pqindx++, pqindx2++, pqindx3++)
          {
            // Refined P/Q diagonal local discrimination
            const float PQ_Central_Value   = PQ_Dir[pqindx];
            const float PQ_Neighbourhood_Value = 0.25f * (PQ_Dir[pqindx2] + PQ_Dir[pqindx2 + 1] + PQ_Dir[pqindx3] + PQ_Dir[pqindx3 + 1]);

            const float PQ_Disc = (fabs(0.5f - PQ_Central_Value) < fabs(0.5f - PQ_Neighbourhood_Value)) ? PQ_Neighbourhood_Value : PQ_Central_Value;

            // Diagonal gradients
            const float NW_Grad = eps + fabs(rgb[c][indx - w1 - 1] - rgb[c][indx + w1 + 1]) + fabs(rgb[c][indx - w1 - 1] - rgb[c][indx - w3 - 3]) + fabs(rgb[1][indx] - rgb[1][indx - w2 - 2]);
            const float NE_Grad = eps + fabs(rgb[c][indx - w1 + 1] - rgb[c][indx + w1 - 1]) + fabs(rgb[c][indx - w1 + 1] - rgb[c][indx - w3 + 3]) + fabs(rgb[1][indx] - rgb[1][indx - w2 + 2]);
            const float SW_Grad = eps + fabs(rgb[c][indx - w1 + 1] - rgb[c][indx + w1 - 1]) + fabs(rgb[c][indx + w1 - 1] - rgb[c][indx + w3 - 3]) + fabs(rgb[1][indx] - rgb[1][indx + w2 - 2]);
            const float SE_Grad = eps + fabs(rgb[c][indx - w1 - 1] - rgb[c][indx + w1 + 1]) + fabs(rgb[c][indx + w1 + 1] - rgb[c][indx + w3 + 3]) + fabs(rgb[1][indx] - rgb[1][indx + w2 + 2]);

            // Diagonal colour differences
            const float NW_Est = rgb[c][indx - w1 - 1] - rgb[1][indx - w1 - 1];
            const float NE_Est = rgb[c][indx - w1 + 1] - rgb[1][indx - w1 + 1];
            const float SW_Est = rgb[c][indx + w1 - 1] - rgb[1][indx + w1 - 1];
            const float SE_Est = rgb[c][indx + w1 + 1] - rgb[1][indx + w1 + 1];

            // P/Q estimations
            const float P_Est = (NW_Grad * SE_Est + SE_Grad * NW_Est) / (NW_Grad + SE_Grad);
            const float Q_Est = (NE_Grad * SW_Est + SW_Grad * NE_Est) / (NE_Grad + SW_Grad);

            // R@B and B@R interpolation
            rgb[c][indx] = rgb[1][indx] + intp(PQ_Disc, Q_Est, P_Est);
          }
        }

        // Step 4.3: Populate the red and blue channels at green CFA positions
        for(int row = 4; row < tileRows - 4; row++)
        {
          for(int col = 4 + (FCRCD(row, 1) & 1), indx = row * RCD_TILESIZE + col; col < tileCols - 4; col += 2, indx +=2)
          {
            // Refined vertical and horizontal local discrimination
            const float VH_Central_Value = VH_Dir[indx];
            const float VH_Neighbourhood_Value = 0.25f * (VH_Dir[indx - w1 - 1] + VH_Dir[indx - w1 + 1] + VH_Dir[indx + w1 - 1] + VH_Dir[indx + w1 + 1]);
            const float VH_Disc = (fabs(0.5f - VH_Central_Value) < fabs(0.5f - VH_Neighbourhood_Value) ) ? VH_Neighbourhood_Value : VH_Central_Value;
            const float rgb1 = rgb[1][indx];
            const float N1 = eps + fabs(rgb1 - rgb[1][indx - w2]);
            const float S1 = eps + fabs(rgb1 - rgb[1][indx + w2]);
            const float W1 = eps + fabs(rgb1 - rgb[1][indx -  2]);
            const float E1 = eps + fabs(rgb1 - rgb[1][indx +  2]);

            const float rgb1mw1 = rgb[1][indx - w1];
            const float rgb1pw1 = rgb[1][indx + w1];
            const float rgb1m1 =  rgb[1][indx - 1];
            const float rgb1p1 =  rgb[1][indx + 1];

            for(int c = 0; c <= 2; c += 2)
            {
              const float SNabs = fabs(rgb[c][indx - w1] - rgb[c][indx + w1]);
              const float EWabs = fabs(rgb[c][indx -  1] - rgb[c][indx +  1]);

              // Cardinal gradients
              const float N_Grad = N1 + SNabs + fabs(rgb[c][indx - w1] - rgb[c][indx - w3]);
              const float S_Grad = S1 + SNabs + fabs(rgb[c][indx + w1] - rgb[c][indx + w3]);
              const float W_Grad = W1 + EWabs + fabs(rgb[c][indx -  1] - rgb[c][indx -  3]);
              const float E_Grad = E1 + EWabs + fabs(rgb[c][indx +  1] - rgb[c][indx +  3]);

              // Cardinal colour differences
              const float N_Est = rgb[c][indx - w1] - rgb1mw1;
              const float S_Est = rgb[c][indx + w1] - rgb1pw1;
              const float W_Est = rgb[c][indx -  1] - rgb1m1;
              const float E_Est = rgb[c][indx +  1] - rgb1p1;

              // Vertical and horizontal estimations
              const float V_Est = (N_Grad * S_Est + S_Grad * N_Est) / (N_Grad + S_Grad);
              const float H_Est = (E_Grad * W_Est + W_Grad * E_Est) / (E_Grad + W_Grad);

              // R@G and B@G interpolation
              rgb[c][indx] = rgb1 + intp(VH_Disc, H_Est, V_Est);
            }
          }
        }

        // For the outermost tiles in all directions we can use a smaller border margin
        const int first_vertical =   rowStart + ((tile_vertical == 0) ? RCD_MARGIN : RCD_BORDER);
        const int last_vertical =    rowEnd   - ((tile_vertical == num_vertical - 1)     ? RCD_MARGIN : RCD_BORDER);
        const int first_horizontal = colStart + ((tile_horizontal == 0) ? RCD_MARGIN : RCD_BORDER);
        const int last_horizontal =  colEnd   - ((tile_horizontal == num_horizontal - 1) ? RCD_MARGIN : RCD_BORDER);
        for(int row = first_vertical; row < last_vertical; row++)
        {
          for(int col = first_horizontal, idx = (row - rowStart) * RCD_TILESIZE + col - colStart, o_idx = (row * width + col) * 4; col < last_horizontal; col++, o_idx += 4, idx++)
          {
            out[o_idx]   = scaler * fmaxf(0.0f, rgb[0][idx]);
            out[o_idx+1] = scaler * fmaxf(0.0f, rgb[1][idx]);
            out[o_idx+2] = scaler * fmaxf(0.0f, rgb[2][idx]);
            out[o_idx+3] = 0.0f;
          }
        }
      }
    }
    dt_free_align(cfa);
    dt_free_align(rgb);
    dt_free_align(VH_Dir);
    dt_free_align(PQ_Dir);
    dt_free_align(P_CDiff_Hpf);
    dt_free_align(Q_CDiff_Hpf);
  }
  rcd_border_interpolate(piece, out, in, roi_out, roi_in, filters, RCD_MARGIN);
}

// revert rcd specific aggressive optimizing
#ifdef __GNUC__
  #pragma GCC pop_options
#endif

#undef FCRCD
#undef RCD_BORDER
#undef RCD_MARGIN
#undef RCD_TILEVALID
#undef w1
#undef w2
#undef w3
#undef w4
#undef eps
#undef epssq
