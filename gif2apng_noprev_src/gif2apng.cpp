/* gif2apng version 1.9
 *
 * This program converts GIF animations into APNG format.
 *
 * http://gif2apng.sourceforge.net/
 *
 * Copyright (c) 2009-2013 Max Stepin
 * maxst at users.sourceforge.net
 *
 * zlib license
 * ------------
 *
 * This software is provided 'as-is', without any express or implied
 * warranty.  In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would be
 *    appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "zlib.h"
#include "7z.h"
extern "C" {
#include "zopfli.h"
}

#if defined(_MSC_VER) && _MSC_VER >= 1300
#define swap16(data) _byteswap_ushort(data)
#define swap32(data) _byteswap_ulong(data)
#elif defined(__linux__)
#include <byteswap.h>
#define swap16(data) bswap_16(data)
#define swap32(data) bswap_32(data)
#elif defined(__FreeBSD__)
#include <sys/endian.h>
#define swap16(data) bswap16(data)
#define swap32(data) bswap32(data)
#elif defined(__APPLE__)
#include <libkern/OSByteOrder.h>
#define swap16(data) OSSwapInt16(data)
#define swap32(data) OSSwapInt32(data)
#else
unsigned short swap16(unsigned short data) {return((data & 0xFF) << 8) | ((data >> 8) & 0xFF);}
unsigned int swap32(unsigned int data) {return((data & 0xFF) << 24) | ((data & 0xFF00) << 8) | ((data >> 8) & 0xFF00) | ((data >> 24) & 0xFF);}
#endif

typedef struct { unsigned char *p; unsigned int size; int x, y, w, h, valid; } OP;
typedef struct { unsigned int num; unsigned char r, g, b, a; } COLORS;

OP      op[6];
COLORS  col[256];

unsigned char * op_zbuf;
z_stream        op_zstream;
z_stream        fin_zstream;

unsigned int    next_seq_num;
unsigned char   png_sign[8]      = {137,  80,  78,  71,  13,  10,  26,  10};
unsigned char   png_Software[24] = { 83, 111, 102, 116, 119,  97, 114, 101, '\0',
                                    103, 105, 102,  50,  97, 112, 110, 103,
                                     46, 115, 102,  46, 110, 101, 116};

int cmp_colors( const void *arg1, const void *arg2 )
{
  if ( ((COLORS*)arg1)->a != ((COLORS*)arg2)->a )
    return (int)(((COLORS*)arg1)->a) - (int)(((COLORS*)arg2)->a);

  if ( ((COLORS*)arg1)->num != ((COLORS*)arg2)->num )
    return (int)(((COLORS*)arg2)->num) - (int)(((COLORS*)arg1)->num);

  if ( ((COLORS*)arg1)->r != ((COLORS*)arg2)->r )
    return (int)(((COLORS*)arg1)->r) - (int)(((COLORS*)arg2)->r);

  if ( ((COLORS*)arg1)->g != ((COLORS*)arg2)->g )
    return (int)(((COLORS*)arg1)->g) - (int)(((COLORS*)arg2)->g);

  return (int)(((COLORS*)arg1)->b) - (int)(((COLORS*)arg2)->b);
}

void DecodeLZW(unsigned char * img, FILE * f1)
{
  int i, bits, codesize, codemask, clearcode, nextcode, lastcode;
  unsigned int   j;
  unsigned int   size = 0;
  unsigned int   accum = 0;
  unsigned short prefix[4097];
  unsigned char  suffix[4097];
  unsigned char  str[4097];
  unsigned char  data[1024];
  unsigned char  firstchar = 0;
  unsigned char *pstr = str;
  unsigned char *pout = img;
  unsigned char  mincodesize;

  if (fread(&mincodesize, 1, 1, f1) != 1) return;

  bits = 0;
  codesize = mincodesize + 1;
  codemask = (1 << codesize) - 1;
  clearcode = 1 << mincodesize;
  nextcode = clearcode + 2;
  lastcode = -1;

  for (i=0; i<clearcode; i++)
    suffix[i] = i;

  if (fread(&size, 1, 1, f1) != 1) return;
  while (size != 0)
  {
    if (fread(&data[0], 1, size, f1) != size) return;

    for (j=0; j<size; j++)
    {
      accum += data[j] << bits;
      bits += 8;
      while (bits >= codesize)
      {
        int incode;
        int code = accum & codemask;
        accum >>= codesize;
        bits -= codesize;

        if (code == clearcode + 1)
          break;

        if (code == clearcode)
        {
          codesize = mincodesize + 1;
          codemask = (1 << codesize) - 1;
          nextcode = clearcode + 2;
          lastcode = -1;
          continue;
        }

        if (lastcode == -1)
        {
          *pout++ = suffix[code];
          firstchar = lastcode = code;
          continue;
        }

        incode = code;
        if (code >= nextcode)
        {
          *pstr++ = firstchar;
          code = lastcode;
        }

        while (code >= clearcode)
        {
          *pstr++ = suffix[code];
          code = prefix[code];
        }

        *pstr++ = firstchar = suffix[code];

        if (nextcode < 4096)
        {
          prefix[nextcode] = lastcode;
          suffix[nextcode] = firstchar;
          nextcode++;
          if (((nextcode & codemask) == 0) && (nextcode < 4096))
          {
            codesize++;
            codemask += nextcode;
          }
        }
        lastcode = incode;

        do
        {
          *pout++ = *--pstr;
        }
        while (pstr > str);
      }
    }
    if (fread(&size, 1, 1, f1) != 1) return;
  }
}

void write_chunk(FILE * f, const char * name, unsigned char * data, unsigned int length)
{
  unsigned int crc = crc32(0, Z_NULL, 0);
  unsigned int len = swap32(length);

  fwrite(&len, 1, 4, f);
  fwrite(name, 1, 4, f);
  crc = crc32(crc, (const Bytef *)name, 4);

  if (memcmp(name, "fdAT", 4) == 0)
  {
    unsigned int seq = swap32(next_seq_num++);
    fwrite(&seq, 1, 4, f);
    crc = crc32(crc, (const Bytef *)(&seq), 4);
    length -= 4;
  }

  if (data != NULL && length > 0)
  {
    fwrite(data, 1, length, f);
    crc = crc32(crc, data, length);
  }

  crc = swap32(crc);
  fwrite(&crc, 1, 4, f);
}

void write_IDATs(FILE * f, int frame, unsigned char * data, unsigned int length, unsigned int idat_size)
{
  unsigned int z_cmf = data[0];
  if ((z_cmf & 0x0f) == 8 && (z_cmf & 0xf0) <= 0x70)
  {
    if (length >= 2)
    {
      unsigned int z_cinfo = z_cmf >> 4;
      unsigned int half_z_window_size = 1 << (z_cinfo + 7);
      while (idat_size <= half_z_window_size && half_z_window_size >= 256)
      {
        z_cinfo--;
        half_z_window_size >>= 1;
      }
      z_cmf = (z_cmf & 0x0f) | (z_cinfo << 4);
      if (data[0] != (unsigned char)z_cmf)
      {
        data[0] = (unsigned char)z_cmf;
        data[1] &= 0xe0;
        data[1] += (unsigned char)(0x1f - ((z_cmf << 8) + data[1]) % 0x1f);
      }
    }
  }

  while (length > 0)
  {
    unsigned int ds = length;
    if (ds > 32768)
      ds = 32768;

    if (frame == 0)
      write_chunk(f, "IDAT", data, ds);
    else
      write_chunk(f, "fdAT", data, ds+4);

    data += ds;
    length -= ds;
  }
}

void deflate_rect_fin(int deflate_method, int iter, unsigned char * zbuf, int * zsize, int bpp, int stride, unsigned char * rows, int zbuf_size, int n)
{
  int j;
  int rowbytes = op[n].w*bpp;
  unsigned char * row  = op[n].p + op[n].y*stride + op[n].x*bpp;
  unsigned char * p  = rows;
  for (j=0; j<op[n].h; j++)
  {
    *p++ = 0;
    memcpy(p, row, rowbytes);
    p += rowbytes;
    row += stride;
  }
  if (deflate_method == 2)
  {
    ZopfliOptions opt_zopfli;
    unsigned char* data = 0;
    size_t size = 0;
    ZopfliInitOptions(&opt_zopfli);
    opt_zopfli.numiterations = iter;
    ZopfliCompress(&opt_zopfli, ZOPFLI_FORMAT_ZLIB, rows, op[n].h*(rowbytes + 1), &data, &size);
    if (size < (size_t)zbuf_size)
    {
      memcpy(zbuf, data, size);
      *zsize = size;
    }
    free(data);
  }
  else
  if (deflate_method == 1)
  {
    unsigned size = zbuf_size;
    compress_rfc1950_7z(rows, op[n].h*(rowbytes + 1), zbuf, size, iter<100 ? iter : 100, 255);
    *zsize = size;
  }
  else
  {
    fin_zstream.next_out = zbuf;
    fin_zstream.avail_out = zbuf_size;
    fin_zstream.next_in = rows;
    fin_zstream.avail_in = op[n].h*(rowbytes + 1);
    deflate(&fin_zstream, Z_FINISH);
    *zsize = fin_zstream.total_out;
    deflateReset(&fin_zstream);
  }
}

void deflate_rect_op(unsigned char * p, int x, int y, int w, int h, int bpp, int stride, unsigned char * row_buf, int zbuf_size, int n)
{
  int j;
  unsigned char * row  = p + y*stride + x*bpp;
  op_zstream.data_type = Z_BINARY;
  op_zstream.next_out = op_zbuf;
  op_zstream.avail_out = zbuf_size;
  for (j=0; j<h; j++)
  {
    memcpy(row_buf+1, row, w*bpp);
    op_zstream.next_in = row_buf;
    op_zstream.avail_in = w*bpp + 1;
    deflate(&op_zstream, Z_NO_FLUSH);
    row += stride;
  }
  deflate(&op_zstream, Z_FINISH);
  op[n].p = p;
  op[n].size = op_zstream.total_out;
  op[n].x = x;
  op[n].y = y;
  op[n].w = w;
  op[n].h = h;
  op[n].valid = 1;
  deflateReset(&op_zstream);
}

void get_rect(int w, int h, unsigned char *pimage1, unsigned char *pimage2, unsigned char *ptemp, int bpp, int stride, unsigned char * row_buf, int zbuf_size, int has_tcolor, int tcolor, int n)
{
  int   i, j, x0, y0, w0, h0;
  int   x_min = w-1;
  int   y_min = h-1;
  int   x_max = 0;
  int   y_max = 0;
  int   diffnum = 0;
  int   over_is_possible = 1;

  if (!has_tcolor)
    over_is_possible = 0;

  if (bpp == 1)
  {
    unsigned char *pa = pimage1;
    unsigned char *pb = pimage2;
    unsigned char *pc = ptemp;

    for (j=0; j<h; j++)
    for (i=0; i<w; i++)
    {
      unsigned char c = *pb++;
      if (*pa++ != c)
      {
        diffnum++;
        if (has_tcolor && c == tcolor) over_is_possible = 0;
        if (i<x_min) x_min = i;
        if (i>x_max) x_max = i;
        if (j<y_min) y_min = j;
        if (j>y_max) y_max = j;
      }
      else
        c = tcolor;

      *pc++ = c;
    }
  }
  else
  if (bpp == 3)
  {
    unsigned char *pa = pimage1;
    unsigned char *pb = pimage2;
    unsigned char *pc = ptemp;

    for (j=0; j<h; j++)
    for (i=0; i<w; i++)
    {
      int c1 = (pa[2]<<16) + (pa[1]<<8) + pa[0];
      int c2 = (pb[2]<<16) + (pb[1]<<8) + pb[0];
      if (c1 != c2)
      {
        diffnum++;
        if (has_tcolor && c2 == tcolor) over_is_possible = 0;
        if (i<x_min) x_min = i;
        if (i>x_max) x_max = i;
        if (j<y_min) y_min = j;
        if (j>y_max) y_max = j;
      }
      else
        c2 = tcolor;

      memcpy(pc, &c2, 3);
      pa += 3;
      pb += 3;
      pc += 3;
    }
  }

  if (diffnum == 0)
  {
    x0 = y0 = 0;
    w0 = h0 = 1;
  }
  else
  {
    x0 = x_min;
    y0 = y_min;
    w0 = x_max-x_min+1;
    h0 = y_max-y_min+1;
  }

  deflate_rect_op(pimage2, x0, y0, w0, h0, bpp, stride, row_buf, zbuf_size, n*2);

  if (over_is_possible)
    deflate_rect_op(ptemp, x0, y0, w0, h0, bpp, stride, row_buf, zbuf_size, n*2+1);
}

int main(int argc, char** argv)
{
  int              i, j, k, n, start;
  int              w, h, x0, y0, w0, h0, x1, y1, w1, h1, h2;
  int              has_tcolor, tcolor, unused, tr, tg, tb, grayscale;
  unsigned char    flags, bcolor, aspect, id, val, size, end, dop, bop;
  unsigned char    dispose_op, interlaced, has_t, t;
  unsigned int     c;
  unsigned short   delay;
  char             szIn[256];
  char             szOut[256];
  char           * szOpt;
  char           * szExt;
  FILE           * f1;
  FILE           * f2;
  unsigned char    data[1024];
  unsigned char    cube[4096];
  unsigned char    gray[256];
  unsigned char    pal_g[256][3];
  unsigned char    pal_l[256][3];
  unsigned char    plte[256][3];
  unsigned char    trns[256];
  unsigned char    coltype;
  int              frames, loops, bpp;
  int              rowbytes, imagesize, idat_size, zbuf_size;
  int              zsize = 0;
  unsigned int     palsize_g = 0;
  unsigned int     palsize_l = 0;
  int              plte_size = 0;
  int              trns_size = 0;
  int              ssize = 0;
  int              keep_palette = 0;
  int              deflate_method = 1;
  int              iter = 15;

  unsigned char  * buffer;
  unsigned char  * zbuf;
  unsigned char  * rows;
  unsigned char  * row_buf;
  unsigned char  * frame0;
  unsigned char  * frame1;
  unsigned char  * frame2;
  unsigned char  * rest;
  unsigned char  * temp;
  unsigned char  * over1;
  unsigned char  * over2;
  unsigned char  * over3;
  unsigned short * delays;

  printf("\ngif2apng 1.9");

  if (argc <= 1)
  {
    printf("\n\nUsage: gif2apng [options] anim.gif [anim.png]\n\n"
           "-z0  : zlib compression\n"
           "-z1  : 7zip compression (default)\n"
           "-z2  : zopfli compression\n"
           "-i## : number of iterations, default -i%d\n"
           "-kp  : keep the palette\n", iter);
    return 1;
  }

  szIn[0] = 0;
  szOut[0] = 0;

  for (i=1; i<argc; i++)
  {
    szOpt = argv[i];

    if (szOpt[0] == '/' || szOpt[0] == '-')
    {
      if (szOpt[1] == 'k' || szOpt[1] == 'K')
      {
        if (szOpt[2] == 'p' || szOpt[2] == 'P')
          keep_palette = 1;
      }
      if (szOpt[1] == 'z' || szOpt[1] == 'Z')
      {
        if (szOpt[2] == '0')
          deflate_method = 0;
        if (szOpt[2] == '1')
          deflate_method = 1;
        if (szOpt[2] == '2')
          deflate_method = 2;
      }
      if (szOpt[1] == 'i' || szOpt[1] == 'I')
      {
        iter = atoi(szOpt+2);
        if (iter < 1) iter = 1;
      }
    }
    else
    if (szIn[0] == 0)
      strcpy(szIn, szOpt);
    else
    if (szOut[0] == 0)
      strcpy(szOut, szOpt);
  }

  if (deflate_method == 0)
    printf(" using ZLIB\n\n");
  else if (deflate_method == 1)
    printf(" using 7ZIP with %d iterations\n\n", iter);
  else if (deflate_method == 2)
    printf(" using ZOPFLI with %d iterations\n\n", iter);

  if (szOut[0] == 0)
  {
    strcpy(szOut, szIn);
    if ((szExt = strrchr(szOut, '.')) != NULL) *szExt = 0;
    strcat(szOut, ".png");
  }

  memset(&cube, 0, sizeof(cube));
  memset(&gray, 0, sizeof(gray));

  for (i=0; i<256; i++)
  {
    pal_g[i][0] = pal_g[i][1] = pal_g[i][2] = i;
    pal_l[i][0] = pal_l[i][1] = pal_l[i][2] = i;
    col[i].num = 0;
    col[i].r = col[i].g = col[i].b = i;
    col[i].a = trns[i] = 255;
  }

  if ((f1 = fopen(szIn, "rb")) != 0)
  {
    unsigned char sig[6];
    printf("Reading '%s'...\n", szIn);

    if (fread(sig, 1, 6, f1) == 6)
    {
      if (sig[0] == 'G' && sig[1] == 'I' && sig[2] == 'F')
      {
        w = h = 0;
        if (fread(&w, 2, 1, f1) != 1) return 1;
        if (fread(&h, 2, 1, f1) != 1) return 1;
        if (fread(&flags, 1, 1, f1) != 1) return 1;
        if (fread(&bcolor, 1, 1, f1) != 1) return 1;
        if (fread(&aspect, 1, 1, f1) != 1) return 1;
        if (flags & 0x80)
        {
          palsize_g = 1 << ((flags & 7) + 1);
          if (fread(&pal_g, 3, palsize_g, f1) != palsize_g) return 1;
        }
      }
      else
      {
        printf("Error: wrong GIF sig\n");
        return 1;
      }
    }
    else
    {
      printf("Error: can't read the sig\n");
      return 1;
    }

    start = ftell(f1);

    frames = 0;
    loops = 1;
    coltype = 3;
    bpp = 1;
    rowbytes = w;
    imagesize = w*h;
    grayscale = 1;

    buffer = (unsigned char *)malloc(imagesize*2);
    if (buffer == NULL)
    {
      printf("Error: not enough memory\n");
      return 1;
    }

    has_tcolor = has_t = tcolor = t = tr = tg = tb = 0;

    /* First GIF scan - count frames, stats, set coltype, bpp, tcolor */
    while ( !feof(f1) )
    {
      if (fread(&id, 1, 1, f1) != 1) return 1;
      if (id == 0x21)
      {
        if (fread(&val, 1, 1, f1) != 1) return 1;
        if (val == 0xF9)
        {
          if (fread(&size, 1, 1, f1) != 1) return 1;
          if (fread(&flags, 1, 1, f1) != 1) return 1;
          if (fread(&delay, 2, 1, f1) != 1) return 1;
          if (fread(&t, 1, 1, f1) != 1) return 1;
          if (fread(&end, 1, 1, f1) != 1) return 1;
          has_t = flags & 1;
        }
        else
        {
          int netscape = 0;
          if (fread(&size, 1, 1, f1) != 1) return 1;
          while (size != 0)
          {
            if (fread(&data[0], 1, size, f1) != size) return 1;
            if (netscape == 1)
            {
              if (size == 3) loops = (data[2]<<8) + data[1];
              netscape = 0;
            }
            if (val == 0xFF && size == 11 && memcmp(data, "NETSCAPE2.0", 11) == 0)
              netscape = 1;
            if (fread(&size, 1, 1, f1) != 1) return 1;
          }
        }
      }
      else
      if (id == 0x2C)
      {
        unsigned int num[256];
        x0 = y0 = w0 = h0 = 0;
        if (fread(&x0, 2, 1, f1) != 1) return 1;
        if (fread(&y0, 2, 1, f1) != 1) return 1;
        if (fread(&w0, 2, 1, f1) != 1) return 1;
        if (fread(&h0, 2, 1, f1) != 1) return 1;
        if (fread(&flags, 1, 1, f1) != 1) return 1;
        memcpy(&pal_l, &pal_g, 256*3);
        if (flags & 0x80)
        {
          palsize_l = 1 << ((flags & 7) + 1);
          if (fread(&pal_l, 3, palsize_l, f1) != palsize_l) return 1;
        }
        imagesize = w0*h0;

        DecodeLZW(buffer, f1);

        for (i=0; i<256; i++)
          num[i] = 0;

        for (i=0; i<imagesize; i++)
          num[buffer[i]]++;

        if (has_t)
        {
          if (frames == 0)
          {
            has_tcolor = 1;
            tcolor = t;
            tr = pal_l[tcolor][0];
            tg = pal_l[tcolor][1];
            tb = pal_l[tcolor][2];
            col[tcolor].a = 0;
          }
          num[t] = 0;
        }

        for (i=0; i<256; i++)
        if (num[i] != 0)
        {
          if (((pal_l[i][0] | pal_l[i][1] | pal_l[i][2]) & 15) == 0)
            cube[(pal_l[i][0]<<4) + pal_l[i][1] + (pal_l[i][2]>>4)] = 1;
          if (pal_l[i][0] == pal_l[i][1] && pal_l[i][1] == pal_l[i][2])
            gray[pal_l[i][0]] = 1;
          else
            grayscale = 0;
        }

        if (frames == 0)
        {
          for (i=0; i<256; i++)
          {
            col[i].r = pal_l[i][0];
            col[i].g = pal_l[i][1];
            col[i].b = pal_l[i][2];
            col[i].num = num[i];
          }
          ssize = (palsize_g > palsize_l) ? palsize_g : palsize_l;
        }
        else
        {
          for (i=0; i<256; i++)
          if (num[i] > 0)
          {
            int found = 0;
            for (j=0; j<ssize; j++)
            if (pal_l[i][0] == col[j].r && pal_l[i][1] == col[j].g && pal_l[i][2] == col[j].b && col[j].a == 255)
            {
              col[j].num += num[i];
              found = 1;
              break;
            }

            if (found == 0)
            {
              if (ssize < 256)
              {
                col[ssize].r = pal_l[i][0];
                col[ssize].g = pal_l[i][1];
                col[ssize].b = pal_l[i][2];
                col[ssize].num = num[i];
                ssize++;
              }
              else
              {
                keep_palette = 0;
                found = 0;
                for (j=0; j<256; j++)
                {
                  if (col[j].num == 0 && col[j].a == 255)
                  {
                    col[j].r = pal_l[i][0];
                    col[j].g = pal_l[i][1];
                    col[j].b = pal_l[i][2];
                    col[j].num = num[i];
                    found = 1;
                    break;
                  }
                }
                if (found == 0)
                {
                  coltype = 2;
                  bpp = 3;
                }
              }
            }
          }
        }
        frames++;
      }
      else
        break;
    }
    /* First GIF scan - end */

    printf("%d frames.\n", frames);

    next_seq_num = 0;
    rowbytes *= bpp;
    imagesize = rowbytes*h;

    /* Optimizations and making plte[] and trns[] - start */
    unused = 0;
    if (!has_tcolor)
    {
      for (i=0; i<4096; i++)
      if (cube[i] == 0)
      {
        tr = (i>>4)&0xF0;
        tg = i&0xF0;
        tb = (i<<4)&0xF0;
        unused = 1;
        break;
      }
    }

    if (coltype == 3)
    {
      if (keep_palette == 1)
      {
        for (i=0; i<ssize; i++)
        {
          plte[i][0] = col[i].r;
          plte[i][1] = col[i].g;
          plte[i][2] = col[i].b;
        }
        plte_size = ssize;
        if (!has_tcolor)
        for (i=0; i<256; i++)
        if (col[i].num == 0)
        {
          has_tcolor = 1;
          tcolor = i;
          break;
        }
        if (has_tcolor)
        {
          trns[tcolor] = 0;
          trns_size = tcolor+1;
          if (trns_size > plte_size)
            plte_size = trns_size;
        }
      }
      else
      {
        if (grayscale)
        {
          int gray_count = 0;
          for (i=0; i<256; i++)
            gray_count += gray[i];
          if (gray_count <= 192)
            grayscale = 0;
        }

        if (grayscale)
        {
          coltype = 0;
          plte_size = 0;

          if (has_tcolor)
          {
            tcolor = col[tcolor].r;
            if (gray[tcolor] == 1)
              has_tcolor = 0;
          }

          if (!has_tcolor)
          for (i=0; i<256; i++)
          if (gray[i] == 0)
          {
            has_tcolor = 1;
            tcolor = i;
            break;
          }

          if (has_tcolor)
          {
            trns[0] = 0; trns[1] = tcolor;
            trns_size = 2;
          }
        }
        else
        {
          if (!has_tcolor)
          for (i=0; i<256; i++)
          if (col[i].num == 0)
          {
            has_tcolor = 1;
            tcolor = i;
            break;
          }
          if (has_tcolor)
            col[tcolor].a = 0;

          qsort(&col[0], 256, sizeof(COLORS), cmp_colors);

          for (i=0; i<256; i++)
          {
            plte[i][0] = col[i].r;
            plte[i][1] = col[i].g;
            plte[i][2] = col[i].b;
            trns[i] = col[i].a;
            if (col[i].num != 0 || col[i].a != 255)
              plte_size = i+1;
            if (trns[i] != 255)
              trns_size = i+1;
            if (trns[i] == 0)
              tcolor = i;
          }
        }
      }
    }
    else /* coltype == 2 */
    {
      if (has_tcolor || unused)
      {
        trns[0] = 0; trns[1] = tr;
        trns[2] = 0; trns[3] = tg;
        trns[4] = 0; trns[5] = tb;
        trns_size = 6;
        has_tcolor = 1;
        tcolor = (tb<<16) + (tg<<8) + tr;
      }
    }
    /* Optimizations and making plte[] and trns[] - end */

    delays = (unsigned short *)malloc(frames*2);
    if (delays == NULL)
      return 1;

    for (i=0; i<frames; i++)
      delays[i] = 10;

    frame0 = (unsigned char *)malloc(imagesize); /* current */
    frame1 = (unsigned char *)malloc(imagesize); /* previous */
    frame2 = (unsigned char *)malloc(imagesize); /* previous of previous */
    rest   = (unsigned char *)malloc(imagesize);
    temp   = (unsigned char *)malloc(imagesize);
    over1  = (unsigned char *)malloc(imagesize);
    over2  = (unsigned char *)malloc(imagesize);
    over3  = (unsigned char *)malloc(imagesize);

    if (!frame0 || !frame1 || !frame2 || !rest || !temp || !over1 || !over2 || !over3)
    {
      printf("Error: not enough memory\n");
      return 1;
    }

    if (coltype == 2)
    {
      unsigned char * dst = frame0;
      for (j=0; j<h; j++)
      for (i=0; i<w; i++)
      {
        *dst++ = tr;
        *dst++ = tg;
        *dst++ = tb;
      }
    }
    else
      memset(frame0, tcolor, imagesize);

    /* APNG encoding - start */
    if ((f2 = fopen(szOut, "wb")) != 0)
    {
      struct IHDR
      {
        unsigned int    mWidth;
        unsigned int    mHeight;
        unsigned char   mDepth;
        unsigned char   mColorType;
        unsigned char   mCompression;
        unsigned char   mFilterMethod;
        unsigned char   mInterlaceMethod;
      } ihdr = { swap32(w), swap32(h), 8, coltype, 0, 0, 0 };

      struct acTL
      {
        unsigned int    mFrameCount;
        unsigned int    mLoopCount;
      } actl = { swap32(frames), swap32(loops) };

      struct fcTL
      {
        unsigned int    mSeq;
        unsigned int    mWidth;
        unsigned int    mHeight;
        unsigned int    mXOffset;
        unsigned int    mYOffset;
        unsigned short  mDelayNum;
        unsigned short  mDelayDen;
        unsigned char   mDisposeOp;
        unsigned char   mBlendOp;
      } fctl;

      printf("Writing '%s'...\n", szOut);

      fwrite(png_sign, 1, 8, f2);

      write_chunk(f2, "IHDR", (unsigned char *)(&ihdr), 13);

      if (frames > 1)
        write_chunk(f2, "acTL", (unsigned char *)(&actl), 8);

      if (plte_size > 0 && coltype == 3)
        write_chunk(f2, "PLTE", (unsigned char *)(&plte), plte_size*3);

      if (trns_size > 0)
        write_chunk(f2, "tRNS", trns, trns_size);

      idat_size = (rowbytes + 1) * h;
      zbuf_size = idat_size + ((idat_size + 7) >> 3) + ((idat_size + 63) >> 6) + 11;

      op_zstream.data_type = Z_BINARY;
      op_zstream.zalloc = Z_NULL;
      op_zstream.zfree = Z_NULL;
      op_zstream.opaque = Z_NULL;
      deflateInit2(&op_zstream, Z_BEST_SPEED, 8, 15, 8, Z_DEFAULT_STRATEGY);

      fin_zstream.data_type = Z_BINARY;
      fin_zstream.zalloc = Z_NULL;
      fin_zstream.zfree = Z_NULL;
      fin_zstream.opaque = Z_NULL;
      deflateInit2(&fin_zstream, Z_BEST_COMPRESSION, 8, 15, 8, Z_DEFAULT_STRATEGY);

      op_zbuf = (unsigned char *)malloc(zbuf_size);
      if (op_zbuf == NULL)
        return 1;

      zbuf = (unsigned char *)malloc(zbuf_size);
      if (zbuf == NULL)
        return 1;

      rows = (unsigned char *)malloc((rowbytes + 1) * h);
      if (rows == NULL)
        return 1;

      row_buf = (unsigned char *)malloc(rowbytes + 1);
      if (row_buf == NULL)
        return 1;

      row_buf[0] = 0;

      x1 = 0;
      y1 = 0;
      w1 = w;
      h1 = h;
      bop = 0;

      fseek( f1, start, SEEK_SET );
      dispose_op = 0;
      has_t = 0;
      n = 0;

      while ( !feof(f1) )
      {
        if (fread(&id, 1, 1, f1) != 1) return 1;
        if (id == 0x21)
        {
          if (fread(&val, 1, 1, f1) != 1) return 1;
          if (val == 0xF9)
          {
            if (fread(&size, 1, 1, f1) != 1) return 1;
            if (fread(&flags, 1, 1, f1) != 1) return 1;
            if (fread(&delay, 2, 1, f1) != 1) return 1;
            if (fread(&t, 1, 1, f1) != 1) return 1;
            if (fread(&end, 1, 1, f1) != 1) return 1;
            has_t = flags & 1;
            dispose_op = (flags >> 2) & 7;
            if (dispose_op > 3) dispose_op = 3;
            if (dispose_op == 3 && n == 0) dispose_op = 2;
            if (delay > 1) delays[n] = delay;
          }
          else
          {
            if (fread(&size, 1, 1, f1) != 1) return 1;
            while (size != 0)
            {
              if (fread(&data[0], 1, size, f1) != size) return 1;
              if (fread(&size, 1, 1, f1) != 1) return 1;
            }
          }
        }
        else
        if (id == 0x2C)
        {
          /* decode GIF frame - start */
          unsigned char * src;
          unsigned char * dst;
          unsigned int    sh[256];
          unsigned int    shuffle = 0;

          x0 = y0 = w0 = h0 = 0;
          if (fread(&x0, 2, 1, f1) != 1) return 1;
          if (fread(&y0, 2, 1, f1) != 1) return 1;
          if (fread(&w0, 2, 1, f1) != 1) return 1;
          if (fread(&h0, 2, 1, f1) != 1) return 1;
          if (fread(&flags, 1, 1, f1) != 1) return 1;
          interlaced = flags & 0x40;
          memcpy(&pal_l, &pal_g, 256*3);
          if (flags & 0x80)
          {
            palsize_l = 1 << ((flags & 7) + 1);
            if (fread(&pal_l, 3, palsize_l, f1) != palsize_l) return 1;
          }

          if (coltype == 3)
          {
            for (c=0; c<256; c++)
            {
              sh[c] = c;

              if (has_t && c==t)
                sh[c] = tcolor;
              else
              {
                for (i=0; i<plte_size; i++)
                if (pal_l[c][0] == plte[i][0] && pal_l[c][1] == plte[i][1] && pal_l[c][2] == plte[i][2] && trns[i] == 255)
                {
                  sh[c] = i;
                  break;
                }
              }

              if (sh[c] != c)
                shuffle = 1;
            }
          }
          else
          if (coltype == 0)
          {
            for (c=0; c<256; c++)
            {
              if (has_t && c==t)
                sh[c] = tcolor;
              else
                sh[c] = pal_l[c][0];

              if (sh[c] != c)
                shuffle = 1;
            }
          }

          memcpy(rest, frame0, imagesize);

          DecodeLZW(buffer, f1);

          h2 = (h0-1)/2;

          if (coltype == 2)
          {
            for (j=0; j<h0; j++)
            {
              k = j; if (interlaced) k = (j>h2) ? (j-h2)*2-1 : (j>h2/2) ? (j-h2/2)*4-2 : (j>h2/4) ? (j-h2/4)*8-4 : j*8;
              src = buffer + j*w0;
              dst = frame0 + ((k+y0)*w + x0)*3;
              for (i=0; i<w0; i++, src++, dst+=3)
                if (!has_t || *src != t)
                  memcpy(dst, &pal_l[*src][0], 3);
            }
          }
          else
          {
            for (j=0; j<h0; j++)
            {
              k = j; if (interlaced) k = (j>h2) ? (j-h2)*2-1 : (j>h2/2) ? (j-h2/2)*4-2 : (j>h2/4) ? (j-h2/4)*8-4 : j*8;
              src = buffer + j*w0;
              dst = frame0 + (k+y0)*w + x0;
              if (shuffle)
              {
                for (i=0; i<w0; i++, src++, dst++)
                  if (!has_t || *src != t)
                    *dst = sh[*src];
              }
              else
              {
                for (i=0; i<w0; i++, src++, dst++)
                  if (!has_t || *src != t)
                    *dst = *src;
              }
            }
          }
          /* decode GIF frame - end */

          /* encode PNG frame - start */
          if (n == 0)
          {
            op[0].p = frame0;
            op[0].x = x1;
            op[0].y = y1;
            op[0].w = w1;
            op[0].h = h1;
            deflate_rect_fin(deflate_method, iter, zbuf, &zsize, bpp, rowbytes, rows, zbuf_size, 0);
          }
          else
          {
            unsigned int op_min;
            unsigned int op_best;

            for (j=0; j<6; j++)
              op[j].valid = 0;

            /* dispose = none */
            get_rect(w, h, frame1, frame0, over1, bpp, rowbytes, row_buf, zbuf_size, has_tcolor, tcolor, 0);

            /* dispose = background */
            if (has_tcolor)
            {
              memcpy(temp, frame1, imagesize);
              if (coltype == 2)
              {
                for (j=0; j<h1; j++)
                {
                  dst = temp + ((j+y1)*w + x1)*3;
                  for (k=0; k<w1; k++)
                  {
                    *dst++ = tr;
                    *dst++ = tg;
                    *dst++ = tb;
                  }
                }
              }
              else
                for (j=0; j<h1; j++)
                  memset(temp + (j+y1)*w + x1, tcolor, w1);

              get_rect(w, h, temp, frame0, over2, bpp, rowbytes, row_buf, zbuf_size, has_tcolor, tcolor, 1);
            }

            // HACK : Removed to save memory on Pebble by not requiring saving previous frame contents
            /* dispose = previous */
            //if (n > 1)
            //  get_rect(w, h, frame2, frame0, over3, bpp, rowbytes, row_buf, zbuf_size, has_tcolor, tcolor, 2);

            op_min = op[0].size;
            op_best = 0;
            for (j=1; j<6; j++)
            {
              if (op[j].size < op_min && op[j].valid)
              {
                op_min = op[j].size;
                op_best = j;
              }
            }

            dop = op_best >> 1;

            fctl.mSeq       = swap32(next_seq_num++);
            fctl.mWidth     = swap32(w1);
            fctl.mHeight    = swap32(h1);
            fctl.mXOffset   = swap32(x1);
            fctl.mYOffset   = swap32(y1);
            fctl.mDelayNum  = swap16(delays[n-1]);
            fctl.mDelayDen  = swap16((unsigned short)100);
            fctl.mDisposeOp = dop;
            fctl.mBlendOp   = bop;
            write_chunk(f2, "fcTL", (unsigned char *)(&fctl), 26);

            write_IDATs(f2, n-1, zbuf, zsize, idat_size);

            /* process apng dispose - begin */
            if (dop == 1)
            {
              if (coltype == 2)
              {
                for (j=0; j<h1; j++)
                {
                  dst = frame1 + ((j+y1)*w + x1)*3;
                  for (k=0; k<w1; k++)
                  {
                    *dst++ = tr;
                    *dst++ = tg;
                    *dst++ = tb;
                  }
                }
              }
              else
                for (j=0; j<h1; j++)
                  memset(frame1 + (j+y1)*w + x1, tcolor, w1);
            }
            else
            if (dop == 2)
            {
              for (j=0; j<h1; j++)
                memcpy(frame1 + ((j+y1)*w + x1)*bpp, frame2 + ((j+y1)*w + x1)*bpp, w1*bpp);
            }
            /* process apng dispose - end */

            x1 = op[op_best].x;
            y1 = op[op_best].y;
            w1 = op[op_best].w;
            h1 = op[op_best].h;
            bop = op_best & 1;

            deflate_rect_fin(deflate_method, iter, zbuf, &zsize, bpp, rowbytes, rows, zbuf_size, op_best);

            memcpy(frame2, frame1, imagesize);
          }
          memcpy(frame1, frame0, imagesize);
          /* encode PNG frame - end */

          /* process gif dispose - begin */
          if (n < frames-1)
          {
            if (dispose_op == 3)
              memcpy(frame0, rest, imagesize);
            else
            if (dispose_op == 2)
            {
              if (coltype == 2)
              {
                for (j=0; j<h0; j++)
                {
                  dst = frame0 + ((j+y0)*w + x0)*3;
                  for (k=0; k<w0; k++)
                  {
                    *dst++ = tr;
                    *dst++ = tg;
                    *dst++ = tb;
                  }
                }
              }
              else
                for (j=0; j<h0; j++)
                  memset(frame0 + (j+y0)*w + x0, tcolor, w0);
            }
          }
          /* process gif dispose - end */

          n++;
          dispose_op = 0;
        }
        else
          break;
      }

      if (frames > 1)
      {
        fctl.mSeq       = swap32(next_seq_num++);
        fctl.mWidth     = swap32(w1);
        fctl.mHeight    = swap32(h1);
        fctl.mXOffset   = swap32(x1);
        fctl.mYOffset   = swap32(y1);
        fctl.mDelayNum  = swap16(delays[frames-1]);
        fctl.mDelayDen  = swap16((unsigned short)100);
        fctl.mDisposeOp = 0;
        fctl.mBlendOp   = bop;
        write_chunk(f2, "fcTL", (unsigned char *)(&fctl), 26);
      }

      write_IDATs(f2, frames-1, zbuf, zsize, idat_size);

      free(row_buf);
      free(rows);
      free(zbuf);

      deflateEnd(&fin_zstream);
      deflateEnd(&op_zstream);
      free(op_zbuf);

      write_chunk(f2, "tEXt", png_Software, 24);
      write_chunk(f2, "IEND", 0, 0);
      fclose(f2);
      printf("%d frames.\n", n);
    }
    else
      printf("Error: can't open the file '%s'\n", szOut);

    /* APNG encoding - end */

    free(frame0);
    free(frame1);
    free(frame2);
    free(rest);
    free(temp);
    free(over1);
    free(over2);
    free(over3);
    free(delays);
    free(buffer);

    fclose(f1);
  }
  else
  {
    printf("Error: can't open the file '%s'\n", szIn);
    return 1;
  }

  return 0;
}
