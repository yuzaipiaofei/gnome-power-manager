/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "config.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <glib.h>

#include "gpm-debug.h"
#include "gpm-array.h"
#include "gpm-array-float.h"

/**
 * gpm_array_float_guassian_value:
 *
 * @x: input value
 * @sigma: sigma value
 * Return value: the gaussian, in floating point precision
 **/
gfloat
gpm_array_float_guassian_value (gfloat x, gfloat sigma)
{
	return (1.0 / (sqrtf(2.0*3.1415927) * sigma)) * (expf((-(powf(x,2.0)))/(2.0 * powf(sigma, 2.0))));
}

/**
 * gpm_array_float_convert:
 *
 * @array: input array
 * Return value: Same length array as input array
 *
 * Converts a GpmArray->y to GpmArrayFloat
 **/
GArray *
gpm_array_float_convert (GpmArray *array)
{
	GpmArrayPoint *point;
	GArray *arrayfloat;
	gfloat *dataptr;
	guint i;
	guint length;

	length = gpm_array_get_size (array);

	/* create a new array */
	arrayfloat = g_array_sized_new (TRUE, FALSE, sizeof(gfloat), length);
	arrayfloat->len = length;

	/* copy from one structure to a quick 1D array */
	for (i=0; i<length; i++) {
		point = gpm_array_get (array, i);
		dataptr = &g_array_index (arrayfloat, gfloat, i);
		*dataptr = point->y;
	}
	return arrayfloat;
}

/**
 * gpm_array_float_compute_gaussian:
 *
 * @length: length of output array
 * @sigma: sigma value
 * Return value: Gaussian array
 *
 * Create a set of Gaussian array of a specified size
 **/
GArray *
gpm_array_float_compute_gaussian (guint length, gfloat sigma)
{
	GArray *array;
	gint half_length;
	guint i;
	gfloat div;
//	const float gaussian_width = 4.0;

	array = g_array_sized_new (TRUE, FALSE, sizeof(gfloat), length);
	array->len = length;

	/* array positions 0..10, has to be an odd number */
	half_length = (length / 2) + 1;
	for (i=0; i<half_length; i++) {
//		div = (gaussian_width / (gfloat) (half_length-1)) * (gfloat) (half_length-(i+1));
//		div = (half_length - (i + 1)) * ((gfloat) length / (gaussian_width + 1.0));
		div = half_length - (i + 1);
		gpm_debug ("div=%f", div);
		g_array_index (array, gfloat, i) = gpm_array_float_guassian_value (div, sigma);
	}

	/* no point working these out, we can just reflect the gaussian */
	for (i=half_length; i<length; i++) {
		div = g_array_index (array, gfloat, length-(i+1));
		g_array_index (array, gfloat, i) = div;
	}
	return array;
}

/**
 * gpm_array_float_print:
 *
 * @array: input array
 *
 * Print the array
 **/
gboolean
gpm_array_float_print (GArray *array)
{
	guint length;
	guint i;

	length = array->len;
	/* debug out */
	for (i=0; i<length; i++) {
		g_print ("[%i]\tval=%f\n", i, g_array_index (array, gfloat, i));
	}
	return TRUE;
}

/**
 * gpm_array_float_convolve:
 *
 * @data: input array
 * @kernel: kernel array
 * Return value: Colvolved array, same length as data
 *
 * Convolves an array with a kernel, and returns an array the same size.
 * THIS FUNCTION IS REALLY SLOW...
 **/
GArray *
gpm_array_float_convolve (GArray *data, GArray *kernel)
{
	guint length_data;
	guint length_kernel;
	GArray *result;
	gfloat value;
	gint i;
	gint j;
	gint index;

	length_data = data->len;
	length_kernel = kernel->len;

	result = g_array_sized_new (TRUE, FALSE, sizeof(gfloat), length_data);
	result->len = length_data;

	/* convolve */
	for (i=0;i<length_data;i++) {
		value = 0;
		for (j=0;j<length_kernel;j++) {
			index = i+j-(length_kernel/2);
			if (index > 0 && index < length_data + 1) {
				value += g_array_index (data, gfloat, index) * g_array_index (kernel, gfloat, j);
			}
		}

		g_array_index (result, gfloat, i) = value;
	}
	return result;
}
