/**********************************************************************
 * otpasswd -- One-time password manager and PAM module.
 * Copyright (C) 2009, 2010 by Tomasz bla Fortuna <bla@thera.be>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with otpasswd. If not, see <http://www.gnu.org/licenses/>.
 *
 * DESC: 
 *   Set of functions performing tasks specified on command line.
 *   All are called from otpasswd.c. One function can realize more
 *   then one command line option.    
 **********************************************************************/

#ifndef _ACTIONS_H_
#define _ACTIONS_H_

#include "ppp.h"


/** Generates/Regenerates new key */
extern int action_key_generate(state *s, const char *username);

/** Removes user state */
extern int action_key_remove();

#endif
