//
// actions for DOOMHACK
//
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "doomdef.h"
#include "doomtype.h"
#include "doomstat.h"
#include "i_system.h"
#include "m_random.h"
#include "s_sound.h"
#include "p_local.h"
#include "p_mobj.h"
#include "d_player.h"
#include "p_pspr.h"
#include "d_items.h"
#include "info.h"
#include "sounds.h"
#include "kgcbor.h"
#include "doomhack.h"

#define MAX_CBOR_DEPTH	2

typedef struct def_action_s
{
	ptr_with_len_t name;
	void *codeptr;
	uint32_t args_sizeof; // optional
	const dh_value_t *args_list; // optional
	const void *args_default; // optional; BEWARE: action must check for NULL argumets
} def_action_t;

//

static state_t *parse_state;
static const def_action_t *parse_action;

dh_aim_cache_t dh_aim_cache;

// common
static const dh_extra_t enum_pointer[] =
{
	// 0 = use whatewer action wants
	{CBOR_STRING("self"), 1},
	{CBOR_STRING("target"), 2},
	{CBOR_STRING("tracer"), 3},
	// terminator
	{}
};

//
void A_FaceTarget(mobj_t*);

//
// functions

static void *get_state_arg(mobj_t *mo, pspdef_t *psp)
{
	if(psp)
		return psp->state->args;
	return mo->state->args;
}

static mobj_t *get_pointer(mobj_t *mo, uint32_t ptr)
{
	switch(ptr)
	{
		case 0:
		case 1:
			return mo;
		case 2:
			return mo->target;
		case 3:
			return mo->tracer;
		default:
			return NULL;
	}
}

static angle_t slope_to_angle(fixed_t slope)
{
	if(slope > 0)
	{
		if(slope < FRACUNIT)
			return tantoangle[SlopeDiv(slope, FRACUNIT)];
		else
			return ANG90 - 1 - tantoangle[SlopeDiv(FRACUNIT, slope)];
	} else
	{
		slope = -slope;
		if(slope < FRACUNIT)
			return -tantoangle[SlopeDiv(slope, FRACUNIT)];
		else
			return ANG270 + tantoangle[SlopeDiv(FRACUNIT, slope)];
	}
}

static mobj_t *player_aim(mobj_t *mo, angle_t *angle, angle_t *pitch)
{
	angle_t an;
	fixed_t sl;

	if(dh_aim_cache.tick == leveltime && dh_aim_cache.mobj == mo)
	{
		// use cached values
		*angle = dh_aim_cache.angle;
		*pitch = dh_aim_cache.pitch;
		return dh_aim_cache.target;
	}

	an = mo->angle;

	if(critical->freeaim == FREEAIM_DIRECT)
	{
		sl = PLAYER_SLOPE(mo->player);
		// TODO: aim for seeker missile
	} else
	{
		sl = P_AimLineAttack(mo, an, 16*64*FRACUNIT);

		if(!linetarget)
		{
			an += 1<<26;
			sl = P_AimLineAttack(mo, an, 16*64*FRACUNIT);

			if(!linetarget)
			{
				an -= 2 << 26;
				sl = P_AimLineAttack(mo, an, 16*64*FRACUNIT);
			}

			if(!linetarget)
			{
				an = mo->angle;
				if(critical->freeaim == FREEAIM_BOTH)
					sl = PLAYER_SLOPE(mo->player);
				else
					sl = 0;
			}
		}
	}

	*angle = an;
	*pitch = slope_to_angle(sl);

	dh_aim_cache.tick = leveltime;
	dh_aim_cache.mobj = mo;
	dh_aim_cache.target = linetarget;
	dh_aim_cache.angle = an;
	dh_aim_cache.pitch = *pitch;

	return linetarget;
}

static void pickup_item(mobj_t *item, player_t *player, void *message, uint32_t sound)
{
	player->bonuscount += 6;
	if(message)
		player->message = message;

	if(item->flags & MF_COUNTITEM)
		player->itemcount++;

	if(sound && player == &players[displayplayer])
		S_StartSoundOptional(NULL, sound, sound);
}

//
// callbacks: argument parser

static int32_t cb_parse_arg(kgcbor_ctx_t *ctx, char *key, uint8_t type, kgcbor_value_t *value)
{
	if(type == KGCBOR_TYPE_TERMINATOR)
		return 1;
	if(type == KGCBOR_TYPE_TERMINATOR_CB)
		return 1;
	return dh_parse_value(parse_action->args_list, parse_state->args, key, ctx, type, value);
}

//
// A_Sound

typedef struct
{
	uint16_t sound;
	uint8_t channel;
	uint8_t ptr;
} arg_Sound_t;

static const dh_extra_t enum_snd_channel[] =
{
	{CBOR_STRING("body"), 0},
	{CBOR_STRING("weapon"), 1},
	// terminator
	{}
};

static const dh_value_t args_Sound[] =
{
	{CBOR_STRING("Sound"), DHVT_SOUNDNUM, offsetof(arg_Sound_t, sound)},
	{CBOR_STRING("Channel"), DHVT_ENUM8, offsetof(arg_Sound_t, channel), enum_snd_channel},
	{CBOR_STRING("ptr"), DHVT_ENUM8, offsetof(arg_Sound_t, ptr), enum_pointer},
	// terminator
	{}
};

static void A_Sound(mobj_t *mo, player_t *pl, pspdef_t *psp)
{
	arg_Sound_t *arg = get_state_arg(mo, psp);

	if(!arg)
		return;

	if(!arg->sound)
		return;

	mo = get_pointer(mo, arg->ptr);
	if(!mo)
		return;

	if(mo->player && arg->channel == 1)
	{
		// crispy supports weapon sounds
		S_StartSound(mo->player->so, arg->sound);
		return;
	}

	S_StartSound(mo, arg->sound);
}

//
// A_DropItem

typedef struct
{
	uint16_t type;
	uint16_t chance;
} arg_DropItem_t;

static const arg_DropItem_t dflt_DropItem =
{
	.type = MT_SPAWNFIRE,
	.chance = 256,
};

static const dh_value_t args_DropItem[] =
{
	{CBOR_STRING("Type"), DHVT_MOBJTYPE, offsetof(arg_DropItem_t, type)},
	{CBOR_STRING("Chance"), DHVT_INT16, offsetof(arg_DropItem_t, chance)},
	// terminator
	{}
};

static void A_DropItem(mobj_t *mo, player_t *pl, pspdef_t *psp)
{
	mobj_t *item;
	arg_DropItem_t *arg = get_state_arg(mo, psp);

	if(arg->chance < 256 && P_Random() >= arg->chance)
		return;

	item = P_SpawnMobj(mo->x, mo->y, mo->z + (8 << FRACBITS), arg->type);

	item->flags |= MF_DROPPED;

	item->angle = P_Random() << 24;
	item->momx = FRACUNIT - (P_Random() << 9);
	item->momy = FRACUNIT - (P_Random() << 9);
	item->momz = (4 << 16) + (P_Random() << 10);

	item->target = mo;
	item->tracer = mo->target;
}

//
// A_SpawnThing

#define ACTF_ST_SET_TARGET	1
#define ACTF_ST_SET_TRACER	2
#define ACTF_ST_IS_TARGET	4
#define ACTF_ST_IS_TRACER	8
#define ACTF_ST_COPY_TARGET	16
#define ACTF_ST_COPY_TRACER	32

typedef struct
{
	uint16_t type;
	uint16_t chance;
	uint32_t flags;
	fixed_t offs_x;
	fixed_t offs_y;
	fixed_t offs_z;
	angle_t angle;
	uint8_t ptr;
} arg_SpawnThing_t;

static const arg_SpawnThing_t dflt_SpawnThing =
{
	.type = MT_SPAWNFIRE,
	.chance = 256,
};

static const dh_value_t args_SpawnThing[] =
{
	{CBOR_STRING("Type"), DHVT_MOBJTYPE, offsetof(arg_SpawnThing_t, type)},
	{CBOR_STRING("Chance"), DHVT_INT16, offsetof(arg_SpawnThing_t, chance)},
	{CBOR_STRING("x"), DHVT_INT32, offsetof(arg_SpawnThing_t, offs_x)},
	{CBOR_STRING("y"), DHVT_INT32, offsetof(arg_SpawnThing_t, offs_y)},
	{CBOR_STRING("z"), DHVT_INT32, offsetof(arg_SpawnThing_t, offs_z)},
	{CBOR_STRING("Angle"), DHVT_INT32, offsetof(arg_SpawnThing_t, angle)},
	{CBOR_STRING("SetTarget"), DHVT_FLAG_BIT, offsetof(arg_SpawnThing_t, flags), (void*)ACTF_ST_SET_TARGET},
	{CBOR_STRING("SetTracer"), DHVT_FLAG_BIT, offsetof(arg_SpawnThing_t, flags), (void*)ACTF_ST_SET_TRACER},
	{CBOR_STRING("IsTarget"), DHVT_FLAG_BIT, offsetof(arg_SpawnThing_t, flags), (void*)ACTF_ST_IS_TARGET},
	{CBOR_STRING("IsTracer"), DHVT_FLAG_BIT, offsetof(arg_SpawnThing_t, flags), (void*)ACTF_ST_IS_TRACER},
	{CBOR_STRING("CopyTarget"), DHVT_FLAG_BIT, offsetof(arg_SpawnThing_t, flags), (void*)ACTF_ST_COPY_TARGET},
	{CBOR_STRING("CopyTracer"), DHVT_FLAG_BIT, offsetof(arg_SpawnThing_t, flags), (void*)ACTF_ST_COPY_TRACER},
	{CBOR_STRING("ptr"), DHVT_ENUM8, offsetof(arg_SpawnThing_t, ptr), enum_pointer},
	// terminator
	{}
};

static void A_SpawnThing(mobj_t *mo, player_t *pl, pspdef_t *psp)
{
	mobj_t *item;
	arg_SpawnThing_t *arg = get_state_arg(mo, psp);
	fixed_t x, y, z;
	angle_t a;

	mo = get_pointer(mo, arg->ptr);
	if(!mo)
		return;

	a = mo->angle + arg->angle;
	z = mo->z + arg->offs_z;

	if(arg->offs_x || arg->offs_y)
	{
		fixed_t ss, cc;

		ss = finesine[a >> ANGLETOFINESHIFT];
		cc = finecosine[a >> ANGLETOFINESHIFT];

		x = FixedMul(arg->offs_y, cc);
		x += FixedMul(arg->offs_x, ss);
		y = FixedMul(arg->offs_y, ss);
		y -= FixedMul(arg->offs_x, cc);
		x += mo->x;
		y += mo->y;
	} else
	{
		x = mo->x;
		y = mo->y;
	}

	item = P_SpawnMobj(x, y, z, arg->type);
	item->angle = a;

	// spawned item
	if(arg->flags & ACTF_ST_SET_TARGET)
		item->target = mo;
	if(arg->flags & ACTF_ST_SET_TRACER)
		item->tracer = mo;
	if(arg->flags & ACTF_ST_COPY_TARGET)
		item->target = mo->target;
	if(arg->flags & ACTF_ST_COPY_TRACER)
		item->tracer = mo->tracer;

	// originator
	if(arg->flags & ACTF_ST_IS_TARGET)
		mo->target = item;
	if(arg->flags & ACTF_ST_IS_TRACER)
		mo->tracer = item;
}

//
// A_Projectile

typedef struct
{
	uint16_t type;
	int16_t ammo_use;
	fixed_t offset;
	fixed_t height;
	angle_t angle;
	angle_t pitch;
} arg_Projectile_t;

static const arg_Projectile_t dflt_Projectile =
{
	.type = MT_SPAWNFIRE,
	.ammo_use = -1, // specified in weapon info
};

static const dh_value_t args_Projectile[] =
{
	{CBOR_STRING("Type"), DHVT_MOBJTYPE, offsetof(arg_Projectile_t, type)},
	{CBOR_STRING("AmmoUse"), DHVT_INT16, offsetof(arg_Projectile_t, ammo_use)},
	{CBOR_STRING("Offset"), DHVT_INT32, offsetof(arg_Projectile_t, offset)},
	{CBOR_STRING("Height"), DHVT_INT32, offsetof(arg_Projectile_t, height)},
	{CBOR_STRING("Angle"), DHVT_INT32, offsetof(arg_Projectile_t, angle)},
	{CBOR_STRING("Pitch"), DHVT_INT32, offsetof(arg_Projectile_t, pitch)},
	// terminator
	{}
};

static void A_Projectile(mobj_t *mo, player_t *pl, pspdef_t *psp)
{
	mobj_t *th;
	fixed_t x, y, z, speed;
	angle_t angle, pitch;
	arg_Projectile_t *arg = get_state_arg(mo, psp);

	if(psp)
	{
		int32_t use = arg->ammo_use;
		weaponinfo_t *info = weaponinfo + pl->readyweapon;

		if(use < 0)
			use = info->ammo_use;

		if(use && info->ammo < numammo)
		{
			int count = P_GetAmmoCount(pl, info->ammo);
			if(count < use)
				return;
			count -= use;
			P_SetAmmoCount(pl, info->ammo, count);
		}
	}

	pitch = 0;
	angle = mo->angle;

	x = mo->x;
	y = mo->y;
	z = mo->z;

	z += 4*8*FRACUNIT; // this constant can be thing property
	z += arg->height;

	if(arg->offset)
	{
		angle_t a = (angle - ANG90) >> ANGLETOFINESHIFT;
		x += FixedMul(arg->offset, finecosine[a]);
		y += FixedMul(arg->offset, finesine[a]);
	}

	th = P_SpawnMobj(x, y, z, arg->type);

	speed = th->info->speed;

	if(mo->player)
	{
		la_zoffs = arg->height;
		th->tracer = player_aim(mo, &angle, &pitch);
		la_zoffs = 0;
	} else
	if(mo->target)
	{
		// enemy aim
		mobj_t *target = mo->target;

		th->tracer = target;

		angle = R_PointToAngle2(mo->x, mo->y, target->x, target->y);

		if(target->flags & MF_SHADOW)
			angle += P_SubRandom() << 20;

		if(speed)
		{
			fixed_t dist;

			dist = P_AproxDistance(target->x - mo->x, target->y - mo->y);

			dist /= speed;
			if(dist <= 0)
				dist = 1;

			dist = ((target->z + (target->height / 2)) - z) / dist;
			dist = FixedDiv(dist, speed);

			pitch = slope_to_angle(dist);
		}
	}

	angle += arg->angle;
	pitch += arg->pitch;

	th->angle = angle;
	th->target = mo;

	if(pitch)
	{
		pitch >>= ANGLETOFINESHIFT;
		th->momz = FixedMul(speed, finesine[pitch]);
		speed = FixedMul(speed, finecosine[pitch]);
	}

	angle >>= ANGLETOFINESHIFT;
	th->momx = FixedMul(speed, finecosine[angle]);
	th->momy = FixedMul(speed, finesine[angle]);

	if(th->info->seesound)
		S_StartSound(th, th->info->seesound);
}

//
// A_Bullets

typedef struct
{
	uint16_t type;
	int16_t ammo_use;
	int16_t count;
	int16_t damage;
	fixed_t height;
	fixed_t range;
	angle_t angle;
	angle_t pitch;
	fixed_t s_hor;
	fixed_t s_ver;
} arg_Bullets_t;

static const arg_Bullets_t dflt_Bullets =
{
	.type = MT_PUFF,
	.ammo_use = -1, // specified in weapon info
	.count = 1,
	.damage = 0,
	.range = MISSILERANGE,
};

static const dh_value_t args_Bullets[] =
{
	{CBOR_STRING("PuffType"), DHVT_MOBJTYPE, offsetof(arg_Bullets_t, type)},
	{CBOR_STRING("AmmoUse"), DHVT_INT16, offsetof(arg_Bullets_t, ammo_use)},
	{CBOR_STRING("Count"), DHVT_INT16, offsetof(arg_Bullets_t, count)},
	{CBOR_STRING("Damage"), DHVT_INT16, offsetof(arg_Bullets_t, damage)},
	{CBOR_STRING("Height"), DHVT_INT32, offsetof(arg_Bullets_t, height)},
	{CBOR_STRING("Angle"), DHVT_INT32, offsetof(arg_Bullets_t, angle)},
	{CBOR_STRING("Pitch"), DHVT_INT32, offsetof(arg_Bullets_t, pitch)},
	{CBOR_STRING("SpreadXY"), DHVT_INT32, offsetof(arg_Bullets_t, s_hor)},
	{CBOR_STRING("SpreadZ"), DHVT_INT32, offsetof(arg_Bullets_t, s_ver)},
	// terminator
	{}
};

static void A_Bullets(mobj_t *mo, player_t *pl, pspdef_t *psp)
{
	angle_t angle, pitch;
	uint32_t spread, count, damage;
	arg_Bullets_t *arg = get_state_arg(mo, psp);

	if(psp)
	{
		int32_t use = arg->ammo_use;
		weaponinfo_t *info = weaponinfo + pl->readyweapon;

		if(use < 0)
			use = info->ammo_use;

		if(use && info->ammo < numammo)
		{
			int count = P_GetAmmoCount(pl, info->ammo);
			if(count < use)
				return;
			count -= use;
			P_SetAmmoCount(pl, info->ammo, count);
		}
	}

	if(!pl)
	{
		fixed_t slope;

		A_FaceTarget(mo);
		angle = mo->angle;

		la_zoffs = arg->height;
		slope = P_AimLineAttack(mo, angle, arg->range);

		pitch = slope_to_angle(slope);

		if(mo->info->attacksound)
			S_StartSound(mo, mo->info->attacksound);
	} else
	{
		la_zoffs = arg->height;
		player_aim(mo, &angle, &pitch);
	}

	angle += arg->angle;
	pitch += arg->pitch;

	damage = arg->damage;
	if(!damage)
		damage = mobjinfo[arg->type].damage;

	count = arg->count;

	if(count < 0)
	{
		count = -count;
		spread = 1;
	} else
	{
		if(!count)
		{
			spread = 0;
			count = 1;
		} else
		if(count > 1 || !pl)
			spread = 1;
		else
			spread = pl->refire;
	}

	for(uint32_t i = 0; i < count; i++)
	{
		angle_t aaa;
		angle_t ppp;
		uint32_t ddd;
		fixed_t slope;

		aaa = angle;
		ppp = pitch;

		if(spread)
		{
			if(arg->s_hor)
			{
				aaa -= arg->s_hor;
				aaa += (arg->s_hor >> 7) * P_Random();
			}
			if(arg->s_ver)
			{
				ppp -= arg->s_ver;
				ppp += (arg->s_ver >> 7) * P_Random();
			}
		}

		la_pufftype = arg->type;
		la_zoffs = arg->height;
		ddd = damage * (1 + (P_Random() % 3));
		slope = finetangent[(ppp + ANG90) >> ANGLETOFINESHIFT];
		P_LineAttack(mo, aaa, arg->range, slope, ddd);
	}

	// restore stuff
	la_pufftype = MT_PUFF;
	la_zoffs = 0;
}

//
// A_ReFire (new)

typedef struct
{
	int32_t state;
} arg_ReFire_t;

static const dh_value_t args_ReFire[] =
{
	{CBOR_STRING("State"), DHVT_STATENUM, offsetof(arg_ReFire_t, state)},
	// terminator
	{}
};

static void A_ReFire_new(mobj_t *mo, player_t *pl, pspdef_t *psp)
{
	arg_ReFire_t *arg;
	int32_t state = -1;

	if(!pl)
		return;

	arg = psp->state->args;
	if(arg)
		state = arg->state;

	if(	pl->cmd.buttons & BT_ATTACK &&
		pl->pendingweapon == wp_nochange &&
		pl->health
	){
		pl->refire++;
		P_FireWeapon(pl, state);
	} else
	{
		pl->refire = 0;
		P_CheckAmmo(pl);
	}
}

//
// A_GunFlash (new)

typedef struct
{
	int32_t state;
	uint32_t player_flash;
} arg_GunFlash_t;

static const arg_GunFlash_t dflt_GunFlash =
{
	.state = -1,
	.player_flash = 1,
};

static const dh_value_t args_GunFlash[] =
{
	{CBOR_STRING("State"), DHVT_STATENUM, offsetof(arg_GunFlash_t, state)},
	{CBOR_STRING("Player"), DHVT_FLAG_BIT, offsetof(arg_GunFlash_t, player_flash), (void*)1},
	// terminator
	{}
};

static void A_GunFlash_new(mobj_t *mo, player_t *pl, pspdef_t *psp)
{
	arg_GunFlash_t *arg;
	int32_t state;

	if(!pl)
		return;

	arg = psp->state->args;

	if(arg->player_flash)
	{
		// check for custom attack state
		state = S_PLAY_ATK2;
		if(mo->info->meleestate)
			state = mo->info->meleestate;
		P_SetMobjState(pl->mo, state);
	}

	// check gun flash state
	state = arg->state;
	if(state < 0)
		// use weapon info
		state = weaponinfo[pl->readyweapon].flashstate;

	P_SetPsprite(pl, ps_flash, state);
}

//
// A_StateJump

typedef struct
{
	int32_t state;
	uint16_t chance;
	uint8_t ptr;
} arg_StateJump_t;

static const arg_StateJump_t dflt_StateJump =
{
	.state = -1,
	.chance = 256,
};

static const dh_value_t args_StateJump[] =
{
	{CBOR_STRING("State"), DHVT_STATENUM, offsetof(arg_StateJump_t, state)},
	{CBOR_STRING("Chance"), DHVT_INT16, offsetof(arg_StateJump_t, chance)},
	{CBOR_STRING("ptr"), DHVT_ENUM8, offsetof(arg_StateJump_t, ptr), enum_pointer},
	// terminator
	{}
};

static void A_StateJump(mobj_t *mo, player_t *pl, pspdef_t *psp)
{
	arg_StateJump_t *arg = get_state_arg(mo, psp);
	int32_t state = arg->state;

	if(psp && arg->ptr)
		// we are changing player thing
		// this allows different attack animations!
		psp = NULL;

	mo = get_pointer(mo, arg->ptr);
	if(!mo)
		return;

	if(arg->chance < 256 && P_Random() >= arg->chance)
		return;

	if(state >= 0)
	{
		if(psp)
			// change weapon sprite
			P_SetPsprite(pl, psp - pl->psprites, state);
		else
			// change requested thing
			P_SetMobjState(mo, state);
	}
}

//
// A_CheckAmmo

typedef struct
{
	int32_t state_pass;
	int32_t state_fail;
	uint16_t type;
	uint16_t count;
	uint8_t ptr;
} arg_CheckAmmo_t;

static const arg_CheckAmmo_t dflt_CheckAmmo =
{
	.state_pass = -1,
	.state_fail = -1,
	.type = 0xFFFF,
	.count = 1,
};

static const dh_value_t args_CheckAmmo[] =
{
	{CBOR_STRING("StatePass"), DHVT_STATENUM, offsetof(arg_CheckAmmo_t, state_pass)},
	{CBOR_STRING("StateFail"), DHVT_STATENUM, offsetof(arg_CheckAmmo_t, state_fail)},
	{CBOR_STRING("Type"), DHVT_AMMOTYPE, offsetof(arg_CheckAmmo_t, type)},
	{CBOR_STRING("Count"), DHVT_INT16, offsetof(arg_CheckAmmo_t, count)},
	{CBOR_STRING("ptr"), DHVT_ENUM8, offsetof(arg_CheckAmmo_t, ptr), enum_pointer},
	// terminator
	{}
};

static void A_CheckAmmo(mobj_t *mo, player_t *pl, pspdef_t *psp)
{
	arg_CheckAmmo_t *arg = get_state_arg(mo, psp);
	mobj_t *dest;
	uint32_t type;
	uint32_t check;
	int32_t state;

	if(psp)
	{
		if(arg->ptr)
			// no redirection for weapons
			return;
		dest = mo;
	} else
	{
		dest = get_pointer(mo, arg->ptr);
		if(!dest)
			return;
	}

	if(!dest->player)
		// only check players
		return;

	pl = dest->player;

	if(arg->type >= numammo)
	{
		// check current weapon
		weaponinfo_t *info = weaponinfo + pl->readyweapon;
		type = info->ammo;
		check = info->ammo_use;
	} else
	{
		type = arg->type;
		check = arg->count;
	}

	if(	type >= numammo ||
		P_GetAmmoCount(pl, type) >= check
	)
		state = arg->state_pass;
	else
		state = arg->state_fail;

	if(state >= 0)
	{
		if(psp)
			// change weapon sprite
			P_SetPsprite(pl, psp - pl->psprites, state);
		else
			// change original thing
			P_SetMobjState(mo, state);
	}
}

//
// A_CheckWeapon

typedef struct
{
	int32_t state_pass;
	int32_t state_fail;
	uint16_t type;
	uint16_t count;
	uint8_t ptr;
} arg_CheckWeapon_t;

static const arg_CheckWeapon_t dflt_CheckWeapon =
{
	.state_pass = -1,
	.state_fail = -1,
	.type = 0xFFFF,
};

static const dh_value_t args_CheckWeapon[] =
{
	{CBOR_STRING("StatePass"), DHVT_STATENUM, offsetof(arg_CheckWeapon_t, state_pass)},
	{CBOR_STRING("StateFail"), DHVT_STATENUM, offsetof(arg_CheckWeapon_t, state_fail)},
	{CBOR_STRING("Type"), DHVT_WEAPTYPE, offsetof(arg_CheckWeapon_t, type)},
	{CBOR_STRING("ptr"), DHVT_ENUM8, offsetof(arg_CheckWeapon_t, ptr), enum_pointer},
	// terminator
	{}
};

static void A_CheckWeapon(mobj_t *mo, player_t *pl, pspdef_t *psp)
{
	arg_CheckWeapon_t *arg = get_state_arg(mo, psp);
	mobj_t *dest;
	int32_t state;

	if(arg->type >= numweapons)
		return;

	if(psp)
	{
		if(arg->ptr)
			// no redirection for weapons
			return;
		dest = mo;
	} else
	{
		dest = get_pointer(mo, arg->ptr);
		if(!dest)
			return;
	}

	if(!dest->player)
		// only check players
		return;

	pl = dest->player;

	if(P_CheckWeaponOwned(pl, arg->type))
		state = arg->state_pass;
	else
		state = arg->state_fail;

	if(state >= 0)
	{
		if(psp)
			// change weapon sprite
			P_SetPsprite(pl, psp - pl->psprites, state);
		else
			// change original thing
			P_SetMobjState(mo, state);
	}
}

//
// A_CheckFlag

typedef struct
{
	int32_t state_pass;
	int32_t state_fail;
	uint32_t bits;
	uint8_t ptr;
} arg_CheckFlag_t;

static const arg_CheckFlag_t dflt_CheckFlag =
{
	.state_pass = -1,
	.state_fail = -1,
};

static const dh_value_t args_CheckFlag[] =
{
	{CBOR_STRING("StatePass"), DHVT_STATENUM, offsetof(arg_CheckFlag_t, state_pass)},
	{CBOR_STRING("StateFail"), DHVT_STATENUM, offsetof(arg_CheckFlag_t, state_fail)},
	{CBOR_STRING("Flag"), DHVT_MOBJFLAG, offsetof(arg_CheckFlag_t, bits)},
	{CBOR_STRING("ptr"), DHVT_ENUM8, offsetof(arg_CheckFlag_t, ptr), enum_pointer},
	// terminator
	{}
};

static void A_CheckFlag(mobj_t *mo, player_t *pl, pspdef_t *psp)
{
	arg_CheckFlag_t *arg = get_state_arg(mo, psp);
	mobj_t *dest;
	int32_t state;

	if(psp)
	{
		if(arg->ptr)
			// no redirection for weapons
			return;
		dest = mo;
	} else
	{
		dest = get_pointer(mo, arg->ptr);
		if(!dest)
			return;
	}

	if(dest->flags & arg->bits)
		state = arg->state_pass;
	else
		state = arg->state_fail;

	if(state >= 0)
	{
		if(psp)
			// change weapon sprite
			P_SetPsprite(pl, psp - pl->psprites, state);
		else
			// change original thing
			P_SetMobjState(mo, state);
	}
}

//
// A_CheckEnemy

typedef struct
{
	int32_t state_pass;
	int32_t state_fail;
	uint8_t ptr;
} arg_CheckEnemy_t;

static const arg_CheckEnemy_t dflt_CheckEnemy =
{
	.state_pass = -1,
	.state_fail = -1,
};

static const dh_value_t args_CheckEnemy[] =
{
	{CBOR_STRING("StatePass"), DHVT_STATENUM, offsetof(arg_CheckEnemy_t, state_pass)},
	{CBOR_STRING("StateFail"), DHVT_STATENUM, offsetof(arg_CheckEnemy_t, state_fail)},
	{CBOR_STRING("ptr"), DHVT_ENUM8, offsetof(arg_CheckEnemy_t, ptr), enum_pointer},
	// terminator
	{}
};

static void A_CheckEnemy(mobj_t *mo, player_t *pl, pspdef_t *psp)
{
	arg_CheckEnemy_t *arg = get_state_arg(mo, psp);
	mobj_t *dest;
	int32_t state;

	if(pl)
		// no weapons
		return;

	dest = get_pointer(mo, arg->ptr);
	if(!dest)
		return;

	state = arg->state_pass;

	for(thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next)
	{
		mobj_t *check;

		if(th->function.acp1 != (actionf_p1)P_MobjThinker)
			continue;

		check = (mobj_t *)th;

		if(check == dest)
			continue;

		if(check->health <= 0)
			continue;

		if(check->type != dest->type)
			continue;

		state = arg->state_fail;
		break;
	}

	if(state >= 0)
		// change original thing
		P_SetMobjState(mo, state);
}

//
// A_ChangeFlag

typedef struct
{
	uint32_t bits;
	uint32_t set;
	uint8_t ptr;
} arg_ChangeFlag_t;

static const dh_value_t args_ChangeFlag[] =
{
	{CBOR_STRING("Flag"), DHVT_MOBJFLAG, offsetof(arg_ChangeFlag_t, bits)},
	{CBOR_STRING("Set"), DHVT_FLAG_BIT, offsetof(arg_ChangeFlag_t, set), (void*)1},
	{CBOR_STRING("ptr"), DHVT_ENUM8, offsetof(arg_ChangeFlag_t, ptr), enum_pointer},
	// terminator
	{}
};

static void A_ChangeFlag(mobj_t *mo, player_t *pl, pspdef_t *psp)
{
	arg_ChangeFlag_t *arg = get_state_arg(mo, psp);
	uint32_t flags;

	if(!arg)
		return;

	mo = get_pointer(mo, arg->ptr);
	if(!mo)
		return;

	flags = mo->flags;

	if(arg->set)
		flags |= arg->bits;
	else
		flags &= ~arg->bits;

	if((flags ^ mo->flags) & (MF_NOSECTOR | MF_NOBLOCKMAP))
	{
		P_UnsetThingPosition(mo);
		mo->flags = flags;
		P_SetThingPosition(mo);
		return;
	}

	mo->flags = flags;
}

//
// A_Message

typedef struct
{
	ptr_with_len_t message;
	uint8_t ptr;
} arg_Message_t;

static const dh_value_t args_Message[] =
{
	{CBOR_STRING("Message"), DHVT_STRING, offsetof(arg_Message_t, message)},
	{CBOR_STRING("ptr"), DHVT_ENUM8, offsetof(arg_Message_t, ptr), enum_pointer},
	// terminator
	{}
};

static void A_Message(mobj_t *mo, player_t *pl, pspdef_t *psp)
{
	arg_Message_t *arg = get_state_arg(mo, psp);

	if(!arg)
		return;

	if(!arg->message.ptr)
		return;

	mo = get_pointer(mo, arg->ptr);
	if(!mo)
		return;

	if(!mo->player)
		return;

	mo->player->message = arg->message.ptr;
}

//
// A_PickupWeapon

typedef struct
{
	uint16_t type;
	uint8_t clips_normal;
	uint8_t clips_dmatch;
	ptr_with_len_t message;
	uint32_t sound;
} arg_PickupWeapon_t;

static const arg_PickupWeapon_t dflt_PickupWeapon =
{
	.type = 0xFFFF,
	.clips_normal = 2,
	.clips_dmatch = 5,
	.sound = sfx_wpnup,
};

static const dh_value_t args_PickupWeapon[] =
{
	{CBOR_STRING("Type"), DHVT_WEAPTYPE, offsetof(arg_PickupWeapon_t, type)},
	{CBOR_STRING("Clips"), DHVT_INT8, offsetof(arg_PickupWeapon_t, clips_normal)},
	{CBOR_STRING("ClipsDM"), DHVT_INT8, offsetof(arg_PickupWeapon_t, clips_dmatch)},
	{CBOR_STRING("Message"), DHVT_STRING, offsetof(arg_PickupWeapon_t, message)},
	{CBOR_STRING("Sound"), DHVT_SOUNDNUM, offsetof(arg_PickupWeapon_t, sound)},
	// terminator
	{}
};

static void A_PickupWeapon(mobj_t *mo, player_t *pl, pspdef_t *psp)
{
	arg_PickupWeapon_t *arg = get_state_arg(mo, psp);
	mobj_t *dest = mo->target;
	uint32_t dropped = !!(mo->flags & MF_DROPPED);
	uint32_t gw, ga;

	if(arg->type >= numweapons)
		return;

	if(!dest)
		return;

	if(!dest->player)
		return;

	if(psp)
		return;

	if(mo->player)
		return;

	pl = dest->player;

	if(	netgame &&
		deathmatch != 2 &&
		!dropped
	){
		uint32_t count;

		if(P_CheckWeaponOwned(pl, arg->type))
			return;

		P_SetWeaponOwned(pl, arg->type, 1);

		count = deathmatch ? arg->clips_dmatch : arg->clips_normal;
		if(count)
			P_GiveAmmo(pl, weaponinfo[arg->type].ammo, count, dropped);

		pl->pendingweapon = arg->type;

		pickup_item(mo, pl, arg->message.ptr, arg->sound);

		return;
	}

	gw = !P_CheckWeaponOwned(pl, arg->type);
	if(gw)
	{
		P_SetWeaponOwned(pl, arg->type, 1);
		pl->pendingweapon = arg->type;
	}

	if(arg->clips_normal)
		ga = P_GiveAmmo(pl, weaponinfo[arg->type].ammo, arg->clips_normal, dropped);
	else
		ga = 0;

	if(!gw && !ga)
		return;

	pickup_item(mo, pl, arg->message.ptr, arg->sound);

	P_RemoveMobj(mo);
}

//
// A_PickupAmmo

typedef struct
{
	uint16_t type;
	uint8_t clips;
	ptr_with_len_t message;
	uint32_t sound;
} arg_PickupAmmo_t;

static const arg_PickupAmmo_t dflt_PickupAmmo =
{
	.type = 0xFFFF,
	.clips = 1,
	.sound = sfx_itemup,
};

static const dh_value_t args_PickupAmmo[] =
{
	{CBOR_STRING("Type"), DHVT_AMMOTYPE, offsetof(arg_PickupAmmo_t, type)},
	{CBOR_STRING("Clips"), DHVT_INT8, offsetof(arg_PickupAmmo_t, clips)},
	{CBOR_STRING("Message"), DHVT_STRING, offsetof(arg_PickupAmmo_t, message)},
	{CBOR_STRING("Sound"), DHVT_SOUNDNUM, offsetof(arg_PickupAmmo_t, sound)},
	// terminator
	{}
};

static void A_PickupAmmo(mobj_t *mo, player_t *pl, pspdef_t *psp)
{
	arg_PickupAmmo_t *arg = get_state_arg(mo, psp);
	mobj_t *dest = mo->target;
	uint32_t dropped = !!(mo->flags & MF_DROPPED);
	uint32_t ga;

	if(arg->type >= numammo)
		return;

	if(!dest)
		return;

	if(!dest->player)
		return;

	if(psp)
		return;

	if(mo->player)
		return;

	pl = dest->player;

	if(arg->clips)
		ga = P_GiveAmmo(pl, arg->type, arg->clips, dropped);
	else
		ga = 0;

	if(!ga)
		return;

	pickup_item(mo, pl, arg->message.ptr, arg->sound);

	P_RemoveMobj(mo);
}

//
// A_DoomDoor

typedef struct
{
	uint16_t tag;
	uint8_t type;
} arg_DoomDoor_t;

static const dh_extra_t enum_door_type[] =
{
	{CBOR_STRING("normal"), vld_normal},
	{CBOR_STRING("close"), vld_close},
	{CBOR_STRING("open"), vld_open},
	{CBOR_STRING("blazeRaise"), vld_blazeRaise},
	{CBOR_STRING("blazeOpen"), vld_blazeOpen},
	{CBOR_STRING("blazeClose"), vld_blazeClose},
	// terminator
	{}
};

static const dh_value_t args_DoomDoor[] =
{
	{CBOR_STRING("Tag"), DHVT_INT16, offsetof(arg_DoomDoor_t, tag)},
	{CBOR_STRING("Type"), DHVT_ENUM8, offsetof(arg_DoomDoor_t, type), enum_door_type},
	// terminator
	{}
};

static void A_DoomDoor(mobj_t *mo, player_t *pl, pspdef_t *psp)
{
	arg_DoomDoor_t *arg = get_state_arg(mo, psp);
	line_t fakeline;

	if(!arg)
		return;

	if(!arg->tag)
		return;

	fakeline.tag = arg->tag;
	EV_DoDoor(&fakeline, arg->type);
}

//
// A_DoomFloor

typedef struct
{
	uint16_t tag;
	uint8_t type;
} arg_DoomFloor_t;

static const dh_extra_t enum_floor_type[] =
{
	{CBOR_STRING("lowerFloor"), lowerFloor},
	{CBOR_STRING("lowerFloorToLowest"), lowerFloorToLowest},
	{CBOR_STRING("turboLower"), turboLower},
	{CBOR_STRING("raiseFloor"), raiseFloor},
	{CBOR_STRING("raiseFloorToNearest"), raiseFloorToNearest},
	{CBOR_STRING("raiseToTexture"), raiseToTexture},
	{CBOR_STRING("lowerAndChange"), lowerAndChange},
	{CBOR_STRING("raiseFloor24"), raiseFloor24},
//	{CBOR_STRING("raiseFloor24AndChange"), raiseFloor24AndChange},
	{CBOR_STRING("raiseFloorCrush"), raiseFloorCrush},
	{CBOR_STRING("raiseFloorTurbo"), raiseFloorTurbo},
//	{CBOR_STRING("donutRaise"), donutRaise},
	{CBOR_STRING("raiseFloor512"), raiseFloor512},
	// terminator
	{}
};

static const dh_value_t args_DoomFloor[] =
{
	{CBOR_STRING("Tag"), DHVT_INT16, offsetof(arg_DoomFloor_t, tag)},
	{CBOR_STRING("Type"), DHVT_ENUM8, offsetof(arg_DoomFloor_t, type), enum_floor_type},
	// terminator
	{}
};

static void A_DoomFloor(mobj_t *mo, player_t *pl, pspdef_t *psp)
{
	arg_DoomFloor_t *arg = get_state_arg(mo, psp);
	line_t fakeline;

	if(!arg)
		return;

	if(!arg->tag)
		return;

	fakeline.tag = arg->tag;
	EV_DoFloor(&fakeline, arg->type);
}

//
// doom actions

void A_Look();
void A_Chase();
void A_BspiAttack();
void A_PosAttack();
void A_SpidRefire();
void A_Pain();
void A_Scream();
void A_Fall();
void A_XScream();

void A_WeaponReady();
void A_Lower();
void A_Raise();
void A_CheckReload();
void A_Light0();
void A_Light1();
void A_Light2();

//
// action list

static const def_action_t def_action[] =
{
	// (some) doom actions
	{CBOR_STRING("A_Look"), A_Look},
	{CBOR_STRING("A_Chase"), A_Chase},
	{CBOR_STRING("A_FaceTarget"), A_FaceTarget},
	{CBOR_STRING("A_BspiAttack"), A_BspiAttack},
	{CBOR_STRING("A_PosAttack"), A_PosAttack},
	{CBOR_STRING("A_SpidRefire"), A_SpidRefire},
	{CBOR_STRING("A_Pain"), A_Pain},
	{CBOR_STRING("A_Scream"), A_Scream},
	{CBOR_STRING("A_Fall"), A_Fall},
	{CBOR_STRING("A_XScream"), A_XScream},
	// doom weapon actions
	{CBOR_STRING("A_WeaponReady"), A_WeaponReady},
	{CBOR_STRING("A_Lower"), A_Lower},
	{CBOR_STRING("A_Raise"), A_Raise},
	{CBOR_STRING("A_CheckReload"), A_CheckReload},
	{CBOR_STRING("A_Light0"), A_Light0},
	{CBOR_STRING("A_Light1"), A_Light1},
	{CBOR_STRING("A_Light2"), A_Light2},
	// doomhack: sound
	{CBOR_STRING("A_Sound"), A_Sound, sizeof(arg_Sound_t), args_Sound}, // no default!
	// doomhack: spawn
	{CBOR_STRING("A_DropItem"), A_DropItem, sizeof(arg_DropItem_t), args_DropItem, &dflt_DropItem},
	{CBOR_STRING("A_SpawnThing"), A_SpawnThing, sizeof(arg_SpawnThing_t), args_SpawnThing, &dflt_SpawnThing},
	// doomhack: attack
	{CBOR_STRING("A_Projectile"), A_Projectile, sizeof(arg_Projectile_t), args_Projectile, &dflt_Projectile},
	{CBOR_STRING("A_Bullets"), A_Bullets, sizeof(arg_Bullets_t), args_Bullets, &dflt_Bullets},
	// doomhack: weapon
	{CBOR_STRING("A_ReFire"), A_ReFire_new, sizeof(arg_ReFire_t), args_ReFire}, // no default!
	{CBOR_STRING("A_GunFlash"), A_GunFlash_new, sizeof(arg_GunFlash_t), args_GunFlash, &dflt_GunFlash},
	// doomhack: state jumps
	{CBOR_STRING("A_StateJump"), A_StateJump, sizeof(arg_StateJump_t), args_StateJump, &dflt_StateJump},
	{CBOR_STRING("A_CheckAmmo"), A_CheckAmmo, sizeof(arg_CheckAmmo_t), args_CheckAmmo, &dflt_CheckAmmo},
	{CBOR_STRING("A_CheckWeapon"), A_CheckWeapon, sizeof(arg_CheckWeapon_t), args_CheckWeapon, &dflt_CheckWeapon},
	{CBOR_STRING("A_CheckFlag"), A_CheckFlag, sizeof(arg_CheckFlag_t), args_CheckFlag, &dflt_CheckFlag},
	{CBOR_STRING("A_CheckEnemy"), A_CheckEnemy, sizeof(arg_CheckEnemy_t), args_CheckEnemy, &dflt_CheckEnemy},
	// doomhack: stuff
	{CBOR_STRING("A_ChangeFlag"), A_ChangeFlag, sizeof(arg_ChangeFlag_t), args_ChangeFlag}, // no default!
	{CBOR_STRING("A_Message"), A_Message, sizeof(arg_Message_t), args_Message}, // no default!
	// doomhack: inventory
	{CBOR_STRING("A_PickupWeapon"), A_PickupWeapon, sizeof(arg_PickupWeapon_t), args_PickupWeapon, &dflt_PickupWeapon},
	{CBOR_STRING("A_PickupAmmo"), A_PickupAmmo, sizeof(arg_PickupAmmo_t), args_PickupAmmo, &dflt_PickupAmmo},
	// doomhack: sectors
	{CBOR_STRING("A_DoomDoor"), A_DoomDoor, sizeof(arg_DoomDoor_t), args_DoomDoor}, // no default!
	{CBOR_STRING("A_DoomFloor"), A_DoomFloor, sizeof(arg_DoomFloor_t), args_DoomFloor}, // no default!
	// terminator
	{}
};

//
// API

void *dh_find_action(void *name, uint32_t nlen)
{
	const def_action_t *act = def_action;

	while(act->name.ptr)
	{
		if(	nlen == act->name.len &&
			!memcmp(name, act->name.ptr, act->name.len)
		)
			return (void*)act;
		act++;
	}

	fprintf(stderr, "[DOOMHACK] Unknown action '%.*s'!\n", nlen, (char*)name);

	return NULL;
}

void dh_parse_action(state_t *st)
{
	kgcbor_ctx_t ctx = {.entry_cb = cb_parse_arg, .max_recursion = MAX_CBOR_DEPTH};
	const def_action_t *act;
	void *ptr;

	if(!st->action.acv)
	{
		st->args = NULL;
		return;
	}

	act = (def_action_t*)st->action.acv;

	st->action.acv = act->codeptr;

	if(!st->args)
	{
		st->args = (void*)act->args_default;
		return;
	}

	if(!act->args_sizeof)
	{
		st->args = NULL;
		return;
	}

	ptr = st->args;

	st->args = dh_get_storage(act->args_sizeof);
	if(act->args_default)
		memcpy(st->args, act->args_default, act->args_sizeof);
	else
		memset(st->args, 0, act->args_sizeof);

	parse_state = st;
	parse_action = act;

	kgcbor_parse_object(&ctx, ptr, -1);
}

