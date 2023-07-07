//
// Copyright(C) 1993-1996 Id Software, Inc.
// Copyright(C) 2005-2014 Simon Howard
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// DESCRIPTION:
//


// We are referring to sprite numbers.
#include "info.h"

#include "d_items.h"


//
// PSPRITE ACTIONS for waepons.
// This struct controls the weapon animations.
//
// Each entry is:
//   ammo/amunition type
//  upstate
//  downstate
// readystate
// atkstate, i.e. attack/fire/hit frame
// flashstate, muzzle flash
//
int numweapons = NUMWEAPONS;

weaponinfo_t weaponinfo[MAX_WEAPON_COUNT] =
{
    {
	// fist
	.ammo = am_noammo,
	.upstate = S_PUNCHUP,
	.downstate = S_PUNCHDOWN,
	.readystate = S_PUNCH,
	.atkstate = S_PUNCH1,
	.flashstate = S_NULL,
	.sel_order = 3700,
	.slot = 1,
    },	
    {
	// pistol
	.ammo = am_clip,
	.upstate = S_PISTOLUP,
	.downstate = S_PISTOLDOWN,
	.readystate = S_PISTOL,
	.atkstate = S_PISTOL1,
	.flashstate = S_PISTOLFLASH,
	.ammo_use = 1,
	.sel_order = 1900,
	.slot = 2,
    },	
    {
	// shotgun
	.ammo = am_shell,
	.upstate = S_SGUNUP,
	.downstate = S_SGUNDOWN,
	.readystate = S_SGUN,
	.atkstate = S_SGUN1,
	.flashstate = S_SGUNFLASH1,
	.ammo_use = 1,
	.sel_order = 1300,
	.slot = 3,
    },
    {
	// chaingun
	.ammo = am_clip,
	.upstate = S_CHAINUP,
	.downstate = S_CHAINDOWN,
	.readystate = S_CHAIN,
	.atkstate = S_CHAIN1,
	.flashstate = S_CHAINFLASH1,
	.ammo_use = 1,
	.sel_order = 700,
	.slot = 4,
    },
    {
	// missile launcher
	.ammo = am_misl,
	.upstate = S_MISSILEUP,
	.downstate = S_MISSILEDOWN,
	.readystate = S_MISSILE,
	.atkstate = S_MISSILE1,
	.flashstate = S_MISSILEFLASH1,
	.ammo_use = 1,
	.sel_order = 2500,
	.slot = 5,
    },
    {
	// plasma rifle
	.ammo = am_cell,
	.upstate = S_PLASMAUP,
	.downstate = S_PLASMADOWN,
	.readystate = S_PLASMA,
	.atkstate = S_PLASMA1,
	.flashstate = S_PLASMAFLASH1,
	.ammo_use = 1,
	.sel_order = 100,
	.slot = 6,
    },
    {
	// bfg 9000
	.ammo = am_cell,
	.upstate = S_BFGUP,
	.downstate = S_BFGDOWN,
	.readystate = S_BFG,
	.atkstate = S_BFG1,
	.flashstate = S_BFGFLASH1,
	.ammo_use = 40, // this is modified by DEHACKED
	.sel_order = 2800,
	.slot = 7,
    },
    {
	// chainsaw
	.ammo = am_noammo,
	.upstate = S_SAWUP,
	.downstate = S_SAWDOWN,
	.readystate = S_SAW,
	.atkstate = S_SAW1,
	.flashstate = S_NULL,
	.sel_order = 2200,
	.slot = 1,
    },
    {
	// super shotgun
	.ammo = am_shell,
	.upstate = S_DSGUNUP,
	.downstate = S_DSGUNDOWN,
	.readystate = S_DSGUN,
	.atkstate = S_DSGUN1,
	.flashstate = S_DSGUNFLASH1,
	.ammo_use = 2,
	.sel_order = 400,
	.slot = 3,
    },	
};








