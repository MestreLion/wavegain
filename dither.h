/* This program is licensed under the GNU Library General Public License, version 2,
 * a copy of which is included with this program (with filename LICENSE.LGPL).
 *
 * (c) 2002 John Edwards
 *
 * rand_t header.
 *
 * last modified: $ID:$
 */

#ifndef __RAND_T_H
#define __RAND_T_H

#include "misc.h"

#ifdef __cplusplus
extern "C" {
#endif 

typedef struct {
	const float*  FilterCoeff;
	Uint64_t      Mask;
	double        Add;
	float         Dither;
	float         ErrorHistory     [2] [16];       // max. 2 channels, 16th order Noise shaping
	float         DitherHistory    [2] [16];
	int           LastRandomNumber [2];
} dither_t;

extern dither_t            Dither;
extern double              doubletmp;
static const unsigned char Parity [256];
unsigned int               random_int ( void );
extern double              scalar16 ( const float* x, const float* y );
extern double              Random_Equi ( double mult );
extern double              Random_Triangular ( double mult );
void                       Init_Dither ( int bits, int shapingtype );

#ifdef __cplusplus
}
#endif 

#endif /* __RAND_T_H */

