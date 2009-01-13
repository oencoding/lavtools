 /*
  *  Experiments in interlace detection
  *  Interlace is the increase of temporal information at the sacrifice of vertical spacial information.
  *  Interlace is totally adaptive.  Stationary objects increase their vertical resolution without any sort of detection.
  *  This system exploits the persistance of vision in the eye.
  *
  *  In a full height progressive display, if the frame is treated as occuring at the same temporal value, stationary images
  *  retain their full height resolution, however moving images suffer from "comb" effect
  *
  *  If the entire frame is treated as two separate temporal images, then the vertical resolution is halved.
  *  A full height frame can be produced by interpolation, however this is noticable and would needlessly degrade the 
  *  the image.  
  *  Stationary portions would lose information.  Moving portions have already lost information due to the 
  *  nature of interlace.
  *
  *  Neither case is true, as part of the image may be stationary and part may be moving.
  *  Detecting the moving portion and interpolating only the needed pixels is the goal of this code.
  *
  *  modified from yuvfps.c by
  *  Copyright (C) 2002 Alfonso Garcia-Pati�o Barbolani
  *
  *
  *  This program is free software; you can redistribute it and/or modify
  *  it under the terms of the GNU General Public License as published by
  *  the Free Software Foundation; either version 2 of the License, or
  *  (at your option) any later version.
  *
  *  This program is distributed in the hope that it will be useful,
  *  but WITHOUT ANY WARRANTY; without even the implied warranty of
  *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  *  GNU General Public License for more details.
  *
  *  You should have received a copy of the GNU General Public License
  *  along with this program; if not, write to the Free Software
  *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
  *
  *
  
gcc yuvilace.c -I/sw/include -I/sw/include/mjpegtools -L/sw/lib -lfftw3 -lmjpegutils -o yuvilace
  
  */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <fftw3.h>
#include <math.h>

#include "yuv4mpeg.h"
#include "mpegconsts.h"

#define YUVRFPS_VERSION "0.1"

static void print_usage() 
{
  fprintf (stderr,
	   "usage: yuvaddetect [-v -h -Ip|b|p]\n"
	   "yuvaddetect produces a 2d graph showing time vs frame difference\n"
           "\n"
	   "\t -v Verbosity degree : 0=quiet, 1=normal, 2=verbose/debug\n"
	   "\t -I<pbt> Force interlace mode\n"
	   "\t -h print this help\n"
         );
}

void copyfield(int which, int width, int height, unsigned char *input[], unsigned char *output[])
{
	// "which" is 0 for top field, 1 for bottom field
	int r = 0;
	int chromasize = width / 2;
	for (r = which; r < height; r += 2)
	{
		int chromapos = (r / 2) * (width / 2); 
		memcpy(&output[0][r * width], &input[0][r * width], width);
		memcpy(&output[1][chromapos], &input[1][chromapos], chromasize);
		memcpy(&output[2][chromapos], &input[2][chromapos], chromasize);
	}
}



// read X frames
// diff each frame (X-1)
// for 1 to X
// if frame != diff write frame 


static void detect(  int fdIn , y4m_stream_info_t  *inStrInfo,
int fdOut, y4m_stream_info_t  *outStrInfo)
{
	y4m_frame_info_t   in_frame ;
	uint8_t            *yuv_data[3],*yuv_odata[3];	

	int                frame_data_size ;
	int                read_error_code ;
	int                write_error_code ;
	int                src_frame_counter ;
	int l=0,f=0,x,y,w,h;
	int points = 512,window=128;
	double max,min,*rdata = NULL, *idata = NULL, *sdata = NULL;
	fftw_plan rplan, iplan;

  // Allocate memory for the YUV channels
	w = y4m_si_get_width(inStrInfo);
	h =  y4m_si_get_height(inStrInfo);
	frame_data_size = h *w;

	// should check for allocation errors here.

		yuv_data[0] = (uint8_t *)malloc( frame_data_size );
		yuv_data[1] = (uint8_t *)malloc( frame_data_size >> 2);
		yuv_data[2] = (uint8_t *)malloc( frame_data_size >> 2);
		if( !yuv_data[0] || !yuv_data[1] || !yuv_data[2]) 
		    mjpeg_error_exit1 ("Could'nt allocate memory for the YUV4MPEG data!");  

		yuv_odata[0] = (uint8_t *)malloc( frame_data_size );
		yuv_odata[1] = (uint8_t *)malloc( frame_data_size >> 2);
		yuv_odata[2] = (uint8_t *)malloc( frame_data_size >> 2);
		if( !yuv_odata[0] || !yuv_odata[1] || !yuv_odata[2]) 
		    mjpeg_error_exit1 ("Could'nt allocate memory for the YUV4MPEG data!");  

		rdata = (double *)fftw_malloc(h * sizeof(double));
		idata = (double *)fftw_malloc(h * sizeof(double));
		sdata = (double *)fftw_malloc(h * sizeof(double));
		
		// should check for malloc errors
		if( !rdata || !idata || !sdata) 
		    mjpeg_error_exit1 ("Could'nt allocate memory for the fft data!");  

	// Zero the sum data
	for (y=0; y<h; y++) sdata[y]=0;


  write_error_code = Y4M_OK ;

  src_frame_counter = 0 ;

	rplan = fftw_plan_r2r_1d(h, rdata, idata, FFTW_R2HC, FFTW_FORWARD); 
	memset((void *)idata, 0, points * sizeof(double));
	iplan = fftw_plan_r2r_1d(h, idata, rdata, FFTW_HC2R, FFTW_BACKWARD); 


	memset((void *)yuv_odata[1],127,frame_data_size >> 2);
	memset((void *)yuv_odata[2],127,frame_data_size >> 2);


// initialise and read the first number of frames
	y4m_init_frame_info( &in_frame );
	read_error_code = y4m_read_frame(fdIn,inStrInfo,&in_frame,yuv_data );
	
	while( Y4M_ERR_EOF != read_error_code && write_error_code == Y4M_OK ) {

// fft the frame
// only doing the luma channel
		
		memcpy(yuv_odata[1],yuv_data[1],frame_data_size >> 2);
		memcpy(yuv_odata[2],yuv_data[2],frame_data_size >> 2);

		// Make a sum of the common vertical frequencies
		
		for (x=0; x<w; x++) {
			for (y=0; y<h; y++) {
				rdata[y] = yuv_data[0][y*w+x];
			}
			fftw_execute(rplan);
			
			for (y=0; y<h; y++) {
				sdata[y] += abs(idata[y]);
			}
		}

// not really max and min but some available doubles
	max = abs(sdata[h/2] - sdata[h/2-1]);
	min = abs(sdata[h/2-1] - sdata[h/2+1]);

if (min > 0 )  printf ("Interlace Factor: %f        \r", max/min ); fflush(stdout);
// Normalise the sum data

//		max = sdata[window]; min = sdata[window];
//		for (y=window+1; y<h-window; y++) {
//				if (max < sdata[y]) max = sdata[y];
//				if (min > sdata[y]) min = sdata[y];
//		}
//		for (y=window; y<h-window; y++) {
//			sdata[y] = (sdata[y] - min ) / (max - min);
//		}


//		for (x=0; x<w; x++) {
//			for (y=0; y<h; y++) {
//				rdata[y] = yuv_data[0][y*w+x];
//			}
//			fftw_execute(rplan);

// Inverse filter the frequency with the sum

//			for ( y=window; y<h-window; y++ ) {
//				idata[y] = idata[y] * (1.0 - sdata[y]);
//			}

//			fftw_execute(iplan);
	/*		
		max = rdata[0]; min = rdata[0];
		for (y=1; y<h; y++) {
				if (max < rdata[y]) max = rdata[y];
				if (min > rdata[y]) min = rdata[y];
		}
	*/		
//			for (y=0; y<h; y++) {
//				yuv_odata[0][y*w+x] = (rdata[y]/points);
//			}
//		}

//	write_error_code = y4m_write_frame( fdOut, outStrInfo, &in_frame, yuv_odata );
		y4m_fini_frame_info( &in_frame );
		y4m_init_frame_info( &in_frame );
		read_error_code = y4m_read_frame(fdIn,inStrInfo,&in_frame,yuv_data );
		++src_frame_counter ;

	}

  // Clean-up regardless an error happened or not

y4m_fini_frame_info( &in_frame );

		free( yuv_data[0] );
		free( yuv_data[1] );
		free( yuv_data[2] );
		free( yuv_odata[0] );
		free( yuv_odata[1] );
		free( yuv_odata[2] );

// output sum of vertical frequency	
//		for (y=0;y<h;y++) printf ("%d %f\n",y,sdata[y]);

	
		fftw_destroy_plan(rplan);
		fftw_free(rdata);
		fftw_free(idata);
		fftw_free(sdata);

	
  if( read_error_code != Y4M_ERR_EOF )
    mjpeg_error_exit1 ("Error reading from input stream!");
  if( write_error_code != Y4M_OK )
    mjpeg_error_exit1 ("Error writing output stream!");

}

static int parse_interlacing(char *str)
{
  if (str[0] != '\0' && str[1] == '\0')
  {
    switch (str[0])
    {
      case 'p':
        return Y4M_ILACE_NONE;
      case 't':
        return Y4M_ILACE_TOP_FIRST;
      case 'b':
        return Y4M_ILACE_BOTTOM_FIRST;
    }
  }
  mjpeg_error_exit1("Valid interlacing modes are: p - progressive, t - top-field first, b - bottom-field first");
  return Y4M_UNKNOWN; /* to avoid compiler warnings */
}

//
// Returns the greatest common divisor (GCD) of the input parameters.
//
static int gcd(int a, int b)
{
  if (b == 0)
    return a;
  else
    return gcd(b, a % b);
}


// *************************************************************************************
// MAIN
// *************************************************************************************
int main (int argc, char *argv[])
{

	int verbose = 4; // LOG_ERROR ;
	int drop_frames = 0;
	int fdIn = 0 ;
	int fdOut = 1 ;
	y4m_stream_info_t in_streaminfo,out_streaminfo;
	int src_interlacing = Y4M_UNKNOWN;
	y4m_ratio_t src_frame_rate;
	const static char *legal_flags = "F:I:v:h";
	int c ;

  while ((c = getopt (argc, argv, legal_flags)) != -1) {
    switch (c) {
      case 'v':
        verbose = atoi (optarg);
        if (verbose < 0 || verbose > 2)
          mjpeg_error_exit1 ("Verbose level must be [0..2]");
        break;
    case 'I':
	    src_interlacing = parse_interlacing(optarg);
		break;
	case 'F':
		drop_frames = atoi(optarg);
		break;
	case 'h':
	case '?':
          print_usage (argv);
          return 0 ;
          break;
    }
  }
  
  
  // mjpeg tools global initialisations
  mjpeg_default_handler_verbosity (verbose);

  // Initialize input streams
  y4m_init_stream_info (&in_streaminfo);
  y4m_init_stream_info (&out_streaminfo);

  // ***************************************************************
  // Get video stream informations (size, framerate, interlacing, aspect ratio).
  // The streaminfo structure is filled in
  // ***************************************************************
  // INPUT comes from stdin, we check for a correct file header
	if (y4m_read_stream_header (fdIn, &in_streaminfo) != Y4M_OK)
		mjpeg_error_exit1 ("Could'nt read YUV4MPEG header!");

	src_frame_rate = y4m_si_get_framerate( &in_streaminfo );
	y4m_copy_stream_info( &out_streaminfo, &in_streaminfo );
	

  // Information output
  mjpeg_info ("yuvrfps (version " YUVRFPS_VERSION ") is a frame rate converter which drops doubled frames for yuv streams");
  mjpeg_info ("yuvrfps -h for help, or man yuvaddetect");

  /* in that function we do all the important work */
	y4m_write_stream_header(fdOut,&out_streaminfo);

	detect( fdIn,&in_streaminfo,fdOut,&out_streaminfo);

  y4m_fini_stream_info (&in_streaminfo);
  y4m_fini_stream_info (&out_streaminfo);

  return 0;
}
/*
 * Local variables:
 *  tab-width: 8
 *  indent-tabs-mode: nil
 * End:
 */
