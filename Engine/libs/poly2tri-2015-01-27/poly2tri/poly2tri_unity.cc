/*
 * Poly2Tri Copyright (c) 2009-2010, Poly2Tri Contributors
 * http://code.google.com/p/poly2tri/
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the conditions in the included
 * implementation files are met.
 */

// SIF compiles poly2tri as one translation unit.  Keep the upstream source
// files separate and readable while reproducing that original build boundary.
#include "common/shapes.cc"
#include "sweep/advancing_front.cc"
#include "sweep/sweep_context.cc"
#include "sweep/sweep.cc"
#include "sweep/cdt.cc"
