/****************************************************************
 *
 *  simann.c: Contains all routines used for simulated annealing.
 *
*****************************************************************/
/*
*   Copyright 2002-2008 Peter Brommer, Daniel Schopf
*             Institute for Theoretical and Applied Physics
*             University of Stuttgart, D-70550 Stuttgart, Germany
*             http://www.itap.physik.uni-stuttgart.de/~imd/potfit
*
*****************************************************************/
/*  
*   This file is part of potfit.
*
*   potfit is free software; you can redistribute it and/or modify
*   it under the terms of the GNU General Public License as published by
*   the Free Software Foundation; either version 2 of the License, or
*   (at your option) any later version.
*
*   potfit is distributed in the hope that it will be useful,
*   but WITHOUT ANY WARRANTY; without even the implied warranty of
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*   GNU General Public License for more details.
*
*   You should have received a copy of the GNU General Public License
*   along with potfit; if not, write to the Free Software
*   Foundation, Inc., 51 Franklin St, Fifth Floor, 
*   Boston, MA  02110-1301  USA
*/
/****************************************************************
* $Revision: 1.25 $
* $Date: 2008/09/26 06:42:34 $
*****************************************************************/



#include <math.h>
#include "potfit.h"
#include "utils.h"
#define RAND_MAX 2147483647
#define EPS 0.1
#define NEPS 4
#define NSTEP 20
#define STEPVAR 2.0
#define NTEMP (3*ndim)
#define TEMPVAR 0.85
#define KMAX 1000
#define GAUSS(a) (1.0/sqrt(2*pi)*(exp(-(SQRREAL(a))/2.)))

#ifdef APOT

void randomize_parameter(int n, real *xi2, real *v)
{
  real  temp, rand;
  int   done = 0;

  do {
    temp = xi2[idx[n]];
    rand = 2.0 * random() / (RAND_MAX + 1.) - 1;
    temp += (rand * v[n]);
    if (temp >= apot_table.pmin[apot_table.idxpot[n]][apot_table.idxparam[n]]
	&& temp <=
	apot_table.pmax[apot_table.idxpot[n]][apot_table.idxparam[n]])
      done = 1;
  } while (!done);
  xi2[idx[n]] = temp;
}

#else


/****************************************************************
 *
 *  real normdist(): Returns a normally distributed random variable
 *          Uses random() to generate a random number.
 * 
 *****************************************************************/


real normdist(void)
{
  static int have = 0;
  static real nd2;
  real  x1, x2, sqr, cnst;

  if (!(have)) {
    do {
      x1 = 2.0 * random() / (RAND_MAX + 1.0) - 1.0;
      x2 = 2.0 * random() / (RAND_MAX + 1.0) - 1.0;
      sqr = x1 * x1 + x2 * x2;
    } while (!(sqr <= 1.0 && sqr > 0));
    /* Box Muller Transformation */
    cnst = sqrt(-2.0 * log(sqr) / sqr);
    nd2 = x2 * cnst;
    have = 1;
    return x1 * cnst;
  } else {
    have = 0;
    return nd2;
  }
}

/****************************************************************
 *
 *  makebump(*x, width, height, center): Displaces equidistant 
 *        sampling points of a function. Displacement is given by
 *        gaussian of given width and height.
 * 
 *****************************************************************/


void makebump(real *x, real width, real height, int center)
{
  int   i, j = 0;

  /* find pot to which center belongs */
  while (opt_pot.last[j] < idx[center])
    j++;
  for (i = 0; i <= 4. * width; i++) {
/* using idx avoids moving fixed points */
    if ((center + i <= ndim) && (idx[center + i] <= opt_pot.last[j])) {
      x[idx[center + i]] += GAUSS((double)i / width) * height;
    }
  }
  for (i = 1; i <= 4. * width; i++) {
    if ((center - i >= 0) && (idx[center - i] >= opt_pot.first[j])) {
      x[idx[center - i]] += GAUSS((double)i / width) * height;
    }
  }
  return;
}
#endif

/****************************************************************
 *
 *  anneal(*xi): Anneals x a vector xi to minimize a function F(xi).
 *      Algorithm according to Cordona et al.
 * 
 *****************************************************************/

void anneal(real *xi)
{
  int   j = 0, m = 0, k = 0, n, h = 0;	/* counters */
  int   nstep = NSTEP, ntemp = NTEMP;
  int   loopagain;		/* loop flag */
  real  c = STEPVAR;
  real  T;			/* Temperature */
  real  F, Fopt, F2;		/* Fn value */
  real *Fvar;			/* backlog of Fn vals */
  real *v;			/* step vector */
  real *xopt, *xi2;		/* optimal value */
  real *fxi1;			/* two latest force vectors */
  real  temp;			/* temporary variable */
#ifndef APOT
  real  p;			/* Probability */
  real  width, height;		/* gaussian bump size */
#endif
  FILE *ff;			/* exit flagfile */
  int  *naccept;		/* number of accepted changes in dir */
  /* init starting temperature for annealing process */
  T = anneal_temp;
  if (T == 0.)
    return;			/* don't anneal if starttemp equal zero */
  Fvar = vect_real(KMAX + 5 + NEPS);	//-(NEPS+1); /* Backlog of old F values */
  v = vect_real(ndim);
  xopt = vect_real(ndimtot);
  xi2 = vect_real(ndimtot);
  fxi1 = vect_real(mdim);
  naccept = vect_int(ndim);
  /* init step vector and optimum vector */
  for (n = 0; n < ndim; n++) {
    v[n] = 1.;
    naccept[n] = 0;
  }
  for (n = 0; n < ndimtot; n++) {
    xi2[n] = xi[n];
    xopt[n] = xi[n];
  }
  F = (*calc_forces) (xi, fxi1, 0);
  Fopt = F;
  printf("k\tT        \tm\tF          \tFopt\n");
  printf("%d\t%f\t%d\t%f\t%f\n", 0, T, 0, F, Fopt);
  fflush(stdout);
  for (n = 0; n <= NEPS; n++)
    Fvar[n] = F;		//Fvar[-n]=F;

  do {
    for (m = 0; m < ntemp; m++) {
      for (j = 0; j < nstep; j++) {
	for (h = 0; h < ndim; h++) {
	  /* Step #1 */
	  /* Create a gaussian bump, 
	     width & hight distributed normally */
	  for (n = 0; n < ndimtot; n++)
	    xi2[n] = xi[n];
#ifdef APOT
	  randomize_parameter(h, xi2, v);
#else
	  width = fabs(normdist());
	  height = normdist() * v[h];
	  makebump(xi2, width, height, h);
#endif
	  F2 = (*calc_forces) (xi2, fxi1, 0);
	  if (F2 <= F) {	/* accept new point */
	    for (n = 0; n < ndimtot; n++)
	      xi[n] = xi2[n];
	    F = F2;
	    naccept[h]++;
	    if (F2 < Fopt) {
	      for (n = 0; n < ndimtot; n++)
		xopt[n] = xi2[n];
	      Fopt = F2;
	      if (tempfile != "\0")
#ifndef APOT
		write_pot_table(&opt_pot, tempfile);
#else
		write_pot_table(&apot_table, tempfile);
#endif
	    }
	  }

	  else if (random() / (RAND_MAX + 1.0) < exp((F - F2) / T)) {
	    for (n = 0; n < ndimtot; n++)
	      xi[n] = xi2[n];
	    F = F2;
	    naccept[h]++;
	  }
	}
      }
      /* Step adjustment */
      for (n = 0; n < ndim; n++) {
	if (naccept[n] > 0.6 * nstep)
	  v[n] *= (1 + c * ((double)naccept[n] / nstep - 0.6) / 0.4);
	else if (naccept[n] < 0.4 * nstep)
	  v[n] /= (1 + c * (0.4 - (double)naccept[n] / nstep) / 0.4);
	naccept[n] = 0;
      }
      printf("%d\t%f\t%d\t%f\t%f\n", k, T, m + 1, F, Fopt);
      fflush(stdout);
      /* End fit if break flagfile exists */
      ff = fopen(flagfile, "r");
      if (NULL != ff) {
	printf("Annealing terminated in presence of break flagfile %s!\n",
	       flagfile);
	printf("Temperature was %f, returning optimum configuration\n", T);
	for (n = 0; n < ndimtot; n++)
	  xi[n] = xopt[n];
	F = Fopt;
	k = KMAX + 1;
	break;
      }
#ifdef EAM
#ifndef NORESCALE
      /* Check for rescaling... every tenth step */
      if ((m % 10) == 0) {
	temp = rescale(&opt_pot, 1., 0);
	/* Was rescaling necessary ? */
	if (temp != 0.) {
#ifdef WZERO
//          embed_shift(&opt_pot);
#endif /* WZERO */
	  /* wake other threads and sync potentials */
	  F = (*calc_forces) (xi, fxi1, 2);
	}
      }
#endif /* NORESCALE */
#endif /* EAM */

    }
    /*Temp adjustment */
    T *= TEMPVAR;
    k++;
    Fvar[k + NEPS] = F;
    loopagain = 0;
    for (n = 1; n <= NEPS; n++) {
      if (fabs(F - Fvar[k - n + NEPS]) > EPS)
	loopagain = 1;
    }
    if (!loopagain && ((F - Fopt) > EPS)) {
      for (n = 0; n < ndimtot; n++)
	xi[n] = xopt[n];
      F = Fopt;
      loopagain = 1;
    }


  } while (k < KMAX && loopagain);
  for (n = 0; n < ndimtot; n++)
    xi[n] = xopt[n];

#ifdef APOT
  for (n = 0; n < ndim; n++)
    apot_table.values[apot_table.idxpot[n]][apot_table.idxparam[n]] =
      xopt[idx[n]];
#endif

  F = Fopt;
#ifndef APOT
  if (tempfile != "\0")
    write_pot_table(&opt_pot, tempfile);
#else
  if (tempfile != "\0")
    write_pot_table(&apot_table, tempfile);
#endif
  free_vect_real(Fvar);		//-NEPS+1);
  free_vect_real(v);
  free_vect_real(xopt);
  free_vect_int(naccept);
  free_vect_real(xi2);
  free_vect_real(fxi1);
  return;
}
