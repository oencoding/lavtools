/*
 *  generic.c
 *    Mark Heath <mjpeg0 at silicontrip.org>
 *  converts interlaced source material into progressive by halving 
 *  the vertical resolution and doubling the frame rate.
 *
 *  based on code:
 *  Copyright (C) 2002 Alfonso Garcia-Pati�o Barbolani <barbolani at jazzfree.com>
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
gcc yuvdeinterlace.c -I/sw/include/mjpegtools -lmjpegutils  -o yuvdeinterlace
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


#include "yuv4mpeg.h"
#include "mpegconsts.h"

#define YUVDE_VERSION "0.1"

static void print_usage() 
{
	fprintf (stderr,
			 "usage: yuvdeinterlace [-v] [-It|b] [-h]\n"
			 "yuvdeinterlace  deinterlaces source material by\n"
			 "doubling the frame rate and doubling the scanlines.\n"
			 "\n"
			 "\t -I force interlace mode\n"
			);
}

// Allocate a uint8_t frame
int chromalloc(uint8_t *m[3],y4m_stream_info_t *sinfo)
{
	
	int fs,cfs;
	
	fs = y4m_si_get_plane_length(sinfo,0);
	cfs = y4m_si_get_plane_length(sinfo,1);
	
	m[0] = (uint8_t *)malloc( fs );
	m[1] = (uint8_t *)malloc( cfs);
	m[2] = (uint8_t *)malloc( cfs);
	
	if( !m[0] || !m[1] || !m[2]) {
		return -1;
	} else {
		return 0;
	}
	
}

//Copy a uint8_t frame
int chromacpy(uint8_t *m[3],uint8_t *n[3],y4m_stream_info_t *sinfo)
{
	
	int fs,cfs;
	
	fs = y4m_si_get_plane_length(sinfo,0);
	cfs = y4m_si_get_plane_length(sinfo,1);
	
	memcpy (m[0],n[0],fs);
	memcpy (m[1],n[1],cfs);
	memcpy (m[2],n[2],cfs);
	
}

// set a solid colour for a uint8_t frame
set_colour(uint8_t *m[], y4m_stream_info_t  *sinfo, int y, int u, int v )
{

        int fs,cfs;
        
        fs = y4m_si_get_plane_length(sinfo,0);
        cfs = y4m_si_get_plane_length(sinfo,1);

		memset (m[0],y,fs);
		memset (m[1],u,cfs);
		memset (m[2],v,cfs);

}


// returns the opposite field ordering
int invert_order(int f)
{
	switch (f) {
			
		case Y4M_ILACE_TOP_FIRST:
			return Y4M_ILACE_BOTTOM_FIRST;
		case Y4M_ILACE_BOTTOM_FIRST:
			return Y4M_ILACE_TOP_FIRST;
		case Y4M_ILACE_NONE:
			return Y4M_ILACE_NONE;
		default:
			return Y4M_UNKNOWN;
	}
}
static void detect(  int fdIn  , y4m_stream_info_t  *inStrInfo )
{
	y4m_frame_info_t   in_frame ;
	uint8_t            *yuv_data[3] ;
	int                read_error_code ;
	int                write_error_code ;
	
	// Allocate memory for the YUV channels
	
	if (chromalloc(yuv_data,inStrInfo))		
		mjpeg_error_exit1 ("Could'nt allocate memory for the YUV4MPEG data!");
	
	/* Initialize counters */
	
	write_error_code = Y4M_OK ;
	
	y4m_init_frame_info( &in_frame );
	read_error_code = y4m_read_frame(fdIn, inStrInfo,&in_frame,yuv_data );
	
	while( Y4M_ERR_EOF != read_error_code && write_error_code == Y4M_OK ) {
		
		// do work
		if (read_error_code == Y4M_OK) {
			
		}
		
		y4m_fini_frame_info( &in_frame );
		y4m_init_frame_info( &in_frame );
		read_error_code = y4m_read_frame(fdIn, inStrInfo,&in_frame,yuv_data );
	}
	// Clean-up regardless an error happened or not
	y4m_fini_frame_info( &in_frame );
	
	free( yuv_data[0] );
	free( yuv_data[1] );
	free( yuv_data[2] );
	
	if( read_error_code != Y4M_ERR_EOF )
		mjpeg_error_exit1 ("Error reading from input stream!");
	
}

// *************************************************************************************
// MAIN
// *************************************************************************************
int main (int argc, char *argv[])
{
	
	int verbose = 4; // LOG_ERROR ;
	int top_field =0, bottom_field = 0,double_height=1;
	int fdIn = 0 ;
	int fdOut = 1 ;
	y4m_stream_info_t in_streaminfo, out_streaminfo ;
	y4m_ratio_t frame_rate;
	int interlaced,ilace=0,pro_chroma=0,yuv_interlacing= Y4M_UNKNOWN;
	int height;
	int c ;
	const static char *legal_flags = "I:vnich";
	
	while ((c = getopt (argc, argv, legal_flags)) != -1) {
		switch (c) {
			case 'v':
				verbose = atoi (optarg);
				if (verbose < 0 || verbose > 2)
					mjpeg_error_exit1 ("Verbose level must be [0..2]");
				break;
				
				case 'h':
				case '?':
				print_usage (argv);
				return 0 ;
				break;
				case 'I':
				switch (optarg[0]) {
					case 't':  yuv_interlacing = Y4M_ILACE_TOP_FIRST;  break;
					case 'b':  yuv_interlacing = Y4M_ILACE_BOTTOM_FIRST;  break;
					default:
						mjpeg_error("Unknown value for interlace: '%c'", optarg[0]);
						return -1;
						break;
				}
				break;
				case 't':
				top_field = 1 ;
				break;
				case 'b':
				bottom_field = 1;
				break;
				case 'c':
				pro_chroma = 1;
				break;
				case 'n':
				double_height = 0;
				break;
				case 'i':
				ilace = 1;
				break;
		}
	}
	
	// mjpeg tools global initialisations
	mjpeg_default_handler_verbosity (verbose);
	
	// Initialize input streams
	y4m_init_stream_info (&in_streaminfo);
	
	// ***************************************************************
	// Get video stream informations (size, framerate, interlacing, aspect ratio).
	// The streaminfo structure is filled in
	// ***************************************************************
	// INPUT comes from stdin, we check for a correct file header
	if (y4m_read_stream_header (fdIn, &in_streaminfo) != Y4M_OK)
		mjpeg_error_exit1 ("Could'nt read YUV4MPEG header!");
	
	// Information output
	mjpeg_info ("yuvcropdetect (version " YUVDE_VERSION ") is a general deinterlace/interlace utility for yuv streams");
	mjpeg_info ("(C) 2005 Mark Heath <mjpeg0 at silicontrip.org>");
	mjpeg_info ("yuvcropdetect -h for help");
	
    
	/* in that function we do all the important work */
	detect(fdIn, &in_streaminfo);
	
	y4m_fini_stream_info (&in_streaminfo);
	
	return 0;
}
/*
 * Local variables:
 *  tab-width: 8
 *  indent-tabs-mode: nil
 * End:
 */