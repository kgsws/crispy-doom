//
// kgsws's DOOMHACK
//
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "doomdef.h"
#include "doomtype.h"
#include "i_system.h"
#include "z_zone.h"
#include "w_wad.h"
#include "p_local.h"
#include "p_mobj.h"
#include "d_items.h"
#include "info.h"
#include "sounds.h"
#include "deh_main.h"
#include "deh_misc.h"
#include "kgcbor.h"
#include "doomhack.h"

#include "dh_names.h"

//#define DEBUG_TEXT

#define STORAGE_ALLOC_CHUNK	(4 * 1024)
#define MAX_CBOR_DEPTH	8

enum
{
	RI_SOUNDS,
	RI_AMMOS,
	RI_WEAPONS,
	RI_THINGS,
	RI_STATES,
	//
	NUM_ROOT_ITEMS
};

typedef struct
{
	ptr_with_len_t name;
	uint32_t type; // CBOR type!
	ptr_with_len_t *dest;
} def_root_t;

typedef struct
{
	ptr_with_len_t name;
	uint32_t bits;
} def_flag_t;

//

int doomhack_active;

static void *data_storage;
static uint32_t data_free;

static int first_state;
static int last_state;

static void (*obj_create)(kgcbor_ctx_t*,char*);
static void (*obj_finish)();
static void *obj_ptr;

static const dh_value_t *prop_val;
static const def_flag_t *prop_flags;

// root contents
static ptr_with_len_t root_item[NUM_ROOT_ITEMS];

static const def_root_t def_root[] =
{
	{CBOR_STRING("sounds"), KGCBOR_TYPE_ARRAY, root_item + RI_SOUNDS},
	{CBOR_STRING("ammos"), KGCBOR_TYPE_OBJECT, root_item + RI_AMMOS},
	{CBOR_STRING("weapons"), KGCBOR_TYPE_OBJECT, root_item + RI_WEAPONS},
	{CBOR_STRING("things"), KGCBOR_TYPE_OBJECT, root_item + RI_THINGS},
	{CBOR_STRING("states"), KGCBOR_TYPE_ARRAY, root_item + RI_STATES},
	// terminator
	{}
};

// thing flags
static const def_flag_t flags_thing[] =
{
	{CBOR_STRING("special"), MF_SPECIAL},
	{CBOR_STRING("solid"), MF_SOLID},
	{CBOR_STRING("shootable"), MF_SHOOTABLE},
	{CBOR_STRING("nosector"), MF_NOSECTOR},
	{CBOR_STRING("noblockmap"), MF_NOBLOCKMAP},
	{CBOR_STRING("ambush"), MF_AMBUSH},
	{CBOR_STRING("justhit"), MF_JUSTHIT},
	{CBOR_STRING("justattacked"), MF_JUSTATTACKED},
	{CBOR_STRING("spawnceiling"), MF_SPAWNCEILING},
	{CBOR_STRING("nogravity"), MF_NOGRAVITY},
	{CBOR_STRING("dropoff"), MF_DROPOFF},
	{CBOR_STRING("pickup"), MF_PICKUP},
	{CBOR_STRING("noclip"), MF_NOCLIP},
	{CBOR_STRING("slidesonwalls"), MF_SLIDE},
	{CBOR_STRING("float"), MF_FLOAT},
	{CBOR_STRING("teleport"), MF_TELEPORT},
	{CBOR_STRING("missile"), MF_MISSILE},
	{CBOR_STRING("dropped"), MF_DROPPED},
	{CBOR_STRING("shadow"), MF_SHADOW},
	{CBOR_STRING("noblood"), MF_NOBLOOD},
	{CBOR_STRING("corpse"), MF_CORPSE},
	{CBOR_STRING("countkill"), MF_COUNTKILL},
	{CBOR_STRING("countitem"), MF_COUNTITEM},
	{CBOR_STRING("skullfly"), MF_SKULLFLY},
	{CBOR_STRING("notdmatch"), MF_NOTDMATCH},
	// terminator
	{}
};

// states
static const dh_value_t val_state[] =
{
	{CBOR_STRING("Sprite"), DHVT_SPRITE, offsetof(state_t, sprite)},
	{CBOR_STRING("Frame"), DHVT_INT32, offsetof(state_t, frame)},
	{CBOR_STRING("Tics"), DHVT_INT32, offsetof(state_t, tics)},
	{CBOR_STRING("Next"), DHVT_STATENUM, offsetof(state_t, nextstate)},
	{CBOR_STRING("Action"), DHVT_ACTION, offsetof(state_t, action)},
	{CBOR_STRING("Args"), DHVT_CBOR_OBJ, offsetof(state_t, args)},
	// terminator
	{}
};

// thing
static const dh_value_t val_thing[] =
{
	// basic
	{CBOR_STRING("ID"), DHVT_INT32, offsetof(mobjinfo_t, doomednum)},
	{CBOR_STRING("Health"), DHVT_INT32, offsetof(mobjinfo_t, spawnhealth)},
	{CBOR_STRING("ReactionTime"), DHVT_INT32, offsetof(mobjinfo_t, reactiontime)},
	{CBOR_STRING("PainChance"), DHVT_INT32, offsetof(mobjinfo_t, painchance)},
	{CBOR_STRING("Speed"), DHVT_INT32, offsetof(mobjinfo_t, speed)},
	{CBOR_STRING("Radius"), DHVT_INT32, offsetof(mobjinfo_t, radius)},
	{CBOR_STRING("Height"), DHVT_INT32, offsetof(mobjinfo_t, height)},
	{CBOR_STRING("Mass"), DHVT_INT32, offsetof(mobjinfo_t, mass)},
	{CBOR_STRING("Damage"), DHVT_INT32, offsetof(mobjinfo_t, damage)},
	// sounds
	{CBOR_STRING("SoundSee"), DHVT_SOUNDNUM, offsetof(mobjinfo_t, seesound)},
	{CBOR_STRING("SoundAttack"), DHVT_SOUNDNUM, offsetof(mobjinfo_t, attacksound)},
	{CBOR_STRING("SoundPain"), DHVT_SOUNDNUM, offsetof(mobjinfo_t, painsound)},
	{CBOR_STRING("SoundDeath"), DHVT_SOUNDNUM, offsetof(mobjinfo_t, deathsound)},
	{CBOR_STRING("SoundActive"), DHVT_SOUNDNUM, offsetof(mobjinfo_t, activesound)},
	// states
	{CBOR_STRING("StateSpawn"), DHVT_STATENUM, offsetof(mobjinfo_t, spawnstate)},
	{CBOR_STRING("StateSee"), DHVT_STATENUM, offsetof(mobjinfo_t, seestate)},
	{CBOR_STRING("StatePain"), DHVT_STATENUM, offsetof(mobjinfo_t, painstate)},
	{CBOR_STRING("StateMelee"), DHVT_STATENUM, offsetof(mobjinfo_t, meleestate)},
	{CBOR_STRING("StateMissile"), DHVT_STATENUM, offsetof(mobjinfo_t, missilestate)},
	{CBOR_STRING("StateDeath"), DHVT_STATENUM, offsetof(mobjinfo_t, deathstate)},
	{CBOR_STRING("StateXDeath"), DHVT_STATENUM, offsetof(mobjinfo_t, xdeathstate)},
	{CBOR_STRING("StateRaise"), DHVT_STATENUM, offsetof(mobjinfo_t, raisestate)},
	{CBOR_STRING("StateTouch"), DHVT_STATENUM, offsetof(mobjinfo_t, touchstate)},
	// special
	{CBOR_STRING("Flags"), DHVT_FLAG_LIST, 0, flags_thing},
	// terminator
	{}
};

// weapon
static const dh_value_t val_weapon[] =
{
	// basic
	{CBOR_STRING("AmmoType"), DHVT_AMMOTYPE, offsetof(weaponinfo_t, ammo)},
	{CBOR_STRING("AmmoUse"), DHVT_INT32, offsetof(weaponinfo_t, ammo_use)},
	{CBOR_STRING("SelectionOrder"), DHVT_INT32, offsetof(weaponinfo_t, sel_order)},
	{CBOR_STRING("Slot"), DHVT_INT32, offsetof(weaponinfo_t, slot)},
	// states
	{CBOR_STRING("StateSelect"), DHVT_STATENUM, offsetof(weaponinfo_t, upstate)},
	{CBOR_STRING("StateDeselect"), DHVT_STATENUM, offsetof(weaponinfo_t, downstate)},
	{CBOR_STRING("StateReady"), DHVT_STATENUM, offsetof(weaponinfo_t, readystate)},
	{CBOR_STRING("StateFire"), DHVT_STATENUM, offsetof(weaponinfo_t, atkstate)},
	{CBOR_STRING("StateFlash"), DHVT_STATENUM, offsetof(weaponinfo_t, flashstate)},
	// terminator
	{}
};

// ammo
static const dh_value_t val_ammo[] =
{
	{CBOR_STRING("ClipSize"), DHVT_INT16, offsetof(ammoinfo_t, clip)},
	{CBOR_STRING("BackpackClips"), DHVT_INT16, offsetof(ammoinfo_t, clip_bkpk)},
	{CBOR_STRING("Max"), DHVT_INT16, offsetof(ammoinfo_t, amax)},
	{CBOR_STRING("BackpackMax"), DHVT_INT16, offsetof(ammoinfo_t, amax_bkpk)},
	// terminator
	{}
};

// parser to CBOR types
static const uint_fast8_t parser_type[] =
{
	[DHVT_INT8] = KGCBOR_TYPE_VALUE,
	[DHVT_INT16] = KGCBOR_TYPE_VALUE,
	[DHVT_INT32] = KGCBOR_TYPE_VALUE,
	[DHVT_ENUM8] = KGCBOR_TYPE_STRING,
	[DHVT_STATENUM] = KGCBOR_TYPE_VALUE,
	[DHVT_SPRITE] = KGCBOR_TYPE_VALUE,
	[DHVT_STRING] = KGCBOR_TYPE_STRING,
	[DHVT_FLAG_LIST] = KGCBOR_TYPE_ARRAY,
	[DHVT_FLAG_BIT] = KGCBOR_TYPE_BOOLEAN,
	[DHVT_ACTION] = KGCBOR_TYPE_STRING,
	[DHVT_CBOR_OBJ] = KGCBOR_TYPE_OBJECT,
	[DHVT_SOUNDNUM] = KGCBOR_TYPE_STRING,
	[DHVT_MOBJTYPE] = KGCBOR_TYPE_STRING,
	[DHVT_WEAPTYPE] = KGCBOR_TYPE_STRING,
	[DHVT_AMMOTYPE] = KGCBOR_TYPE_STRING,
	[DHVT_MOBJFLAG] = KGCBOR_TYPE_STRING,
};

//
// functions

static int32_t handle_sprite_name(uint32_t name)
{
	char *ptr;

	for(uint32_t i = 0; i < numsprites; i++)
	{
		const char *sname = sprnames[i];

		if(!sname)
			return -1;

		// NOTE: this check could be faster on x86 using pointers
		if(!memcmp(sname, &name, sizeof(uint32_t)))
			return i;
	}

	// create new one

	if(numsprites >= MAX_SPRITE_COUNT)
	{
		I_Error("[DOOMHACK] Too many sprite names!\n");
		return 0;
	}

	ptr = dh_get_storage(5);
	sprnames[numsprites] = ptr;

	memcpy(ptr, &name, sizeof(uint32_t));
	ptr[4] = 0;

	numsprites++;

	return numsprites-1;
}

static int find_thing_by_name(char *name, uint32_t nlen)
{
	for(uint32_t i = 0; i < nummobjtypes; i++)
	{
		mobjinfo_t *info = mobjinfo + i;

		if(info->name.len != nlen)
			continue;

		if(memcmp(info->name.ptr, name, nlen))
			continue;

		return i;
	}

	return -1;
}

static int find_weapon_by_name(char *name, uint32_t nlen)
{
	for(uint32_t i = 0; i < numweapons; i++)
	{
		weaponinfo_t *info = weaponinfo + i;

		if(info->name.len != nlen)
			continue;

		if(memcmp(info->name.ptr, name, nlen))
			continue;

		return i;
	}

	return -1;
}

static int find_ammo_by_name(char *name, uint32_t nlen)
{
	for(uint32_t i = 0; i < numammo; i++)
	{
		ammoinfo_t *info = ammoinfo + i;

		if(info->name.len != nlen)
			continue;

		if(memcmp(info->name.ptr, name, nlen))
			continue;

		return i;
	}

	return -1;
}

static int find_sound_by_name(char *name, uint32_t nlen)
{
	char check[8];

	if(nlen > 8)
		return -1;

	if(nlen < 3)
		return -1;

	if(*name != 'd')
		return -1;
	name++;

	if(*name != 's')
		return -1;
	name++;

	nlen -= 2;

	memcpy(check, name, nlen);
	check[nlen] = 0;

	for(uint32_t i = 0; i < numsfx; i++)
	{
		sfxinfo_t *info = S_sfx + i;

		if(strcmp(info->name, check))
			continue;

		return i;
	}

	return -1;
}

//
// callbacks: CBOR

static int32_t cb_root_obj(kgcbor_ctx_t *ctx, char *key, uint8_t type, kgcbor_value_t *value)
{
	if(type != KGCBOR_TYPE_OBJECT)
		return 1;
	obj_create(ctx, key);
	return 0;
}

static int32_t cb_root(kgcbor_ctx_t *ctx, char *key, uint8_t type, kgcbor_value_t *value)
{
	const def_root_t *def = def_root;

	while(def->name.ptr)
	{
		if(	type == def->type &&
			ctx->key_len == def->name.len &&
			!memcmp(key, def->name.ptr, def->name.len)
		){
			def->dest->ptr = ctx->ptr_obj;
			def->dest->len = ctx->val_len;
			break;
		}
		def++;
	}

	// skip everything
	return 1;
}

static int32_t cb_parse_flags(kgcbor_ctx_t *ctx, char *key, uint8_t type, kgcbor_value_t *value)
{
	// this can be used only on mobjinfo_t
	const def_flag_t *flag = prop_flags;

	if(type != KGCBOR_TYPE_STRING)
		return 1;

	while(flag->name.ptr)
	{
		if(	ctx->val_len == flag->name.len &&
			!memcmp(value->ptr, flag->name.ptr, flag->name.len)
		){
			((mobjinfo_t*)obj_ptr)->flags |= flag->bits;
			return 0;
		}
		flag++;
	}

	fprintf(stderr, "[DOOMHACK] Unknown flag '%.*s'!\n", ctx->val_len, (char*)value->ptr);

	return 0;
}

static int32_t cb_parse_props(kgcbor_ctx_t *ctx, char *key, uint8_t type, kgcbor_value_t *value)
{
	if(type == KGCBOR_TYPE_TERMINATOR_CB)
	{
		if(obj_finish)
			obj_finish();
		return 1;
	}

	if(type == KGCBOR_TYPE_TERMINATOR)
		return 1;

	return dh_parse_value(prop_val, obj_ptr, key, ctx, type, value);
}

static int32_t cb_state_array(kgcbor_ctx_t *ctx, char *key, uint8_t type, kgcbor_value_t *value)
{
	if(type != KGCBOR_TYPE_OBJECT)
		return 1;

	prop_val = val_state;
	obj_ptr = states + first_state + ctx->index;
	ctx->entry_cb = cb_parse_props;

	return 0;
}

static int32_t cb_sound_array(kgcbor_ctx_t *ctx, char *key, uint8_t type, kgcbor_value_t *value)
{
	int32_t idx;
	sfxinfo_t *info;
	char *name;
	uint32_t nlen;

	if(type != KGCBOR_TYPE_STRING)
		return 1;

	name = value->ptr;
	nlen = ctx->val_len;

	idx = find_sound_by_name(name, nlen);
	if(idx >= 0)
		// sound already exists
		return 0;

	if(	nlen > 8 ||
		nlen < 3 ||
		name[0] != 'd' ||
		name[1] != 's'
	){
		fprintf(stderr, "[DOOMHACK] Invalid sound name '%.*s'!\n", nlen, name);
		return 0;
	}

	name += 2;
	nlen -= 2;

	numsfx++;
	S_sfx = realloc(S_sfx, sizeof(sfxinfo_t) * numsfx);
	if(!S_sfx)
	{
		I_Error("[DOOMHACK] Memory allocation failed!\n");
		return 0;
	}

	info = S_sfx + numsfx - 1;
	*info = S_sfx[sfx_pistol];

	memcpy(info->name, name, nlen);
	info->name[nlen] = 0;

	return 0;
}

//
// handler: ammo

static void create_ammo(kgcbor_ctx_t *ctx, char *name)
{
	uint32_t nlen = ctx->key_len;
	int idx;
	ammoinfo_t *info;

	idx = find_ammo_by_name(name, nlen);
	if(idx < 0)
	{
		if(numammo >= MAX_AMMO_COUNT)
		{
			I_Error("[DOOMHACK] Too many ammo types!\n");
			return;
		}

		info = ammoinfo + numammo;
		numammo++;

		obj_ptr = info;

		memset(info, 0, sizeof(ammoinfo_t));
		info->clip_bkpk = 1;

		// save name
		info->name.len = nlen;
		info->name.ptr = dh_get_storage(nlen);
		memcpy(info->name.ptr, name, nlen);
	} else
		obj_ptr = ammoinfo + idx;

	// parse properties now
	prop_val = val_ammo;
	ctx->entry_cb = cb_parse_props;
}

static void finish_ammo()
{
	ammoinfo_t *info = obj_ptr;

	if(info->amax_bkpk < info->amax)
		info->amax_bkpk = info->amax * 2;
}

//
// handler: weapon

static void create_weapon(kgcbor_ctx_t *ctx, char *name)
{
	uint32_t nlen = ctx->key_len;
	int idx;
	weaponinfo_t *info;

	idx = find_weapon_by_name(name, nlen);
	if(idx < 0)
	{
		if(numweapons >= MAX_WEAPON_COUNT)
		{
			I_Error("[DOOMHACK] Too many weapons!\n");
			return;
		}

		info = weaponinfo + numweapons;
		numweapons++;

		obj_ptr = info;

		memset(info, 0, sizeof(weaponinfo_t));
		info->ammo = am_noammo;

		// save name
		info->name.len = nlen;
		info->name.ptr = dh_get_storage(nlen);
		memcpy(info->name.ptr, name, nlen);
	} else
		obj_ptr = weaponinfo + idx;

	// parse properties now
	prop_val = val_weapon;
	ctx->entry_cb = cb_parse_props;
}

//
// handler: thing

static void create_thing(kgcbor_ctx_t *ctx, char *name)
{
	uint32_t nlen = ctx->key_len;
	int idx;
	mobjinfo_t *info;

	idx = find_thing_by_name(name, nlen);
	if(idx < 0)
	{
		nummobjtypes++;
		mobjinfo = realloc(mobjinfo, sizeof(mobjinfo_t) * nummobjtypes);
		if(!mobjinfo)
		{
			I_Error("[DOOMHACK] Memory allocation failed!\n");
			return;
		}

		info = mobjinfo + nummobjtypes - 1;
		obj_ptr = info;

		memset(info, 0, sizeof(mobjinfo_t));

		// save name
		info->name.len = nlen;
		info->name.ptr = dh_get_storage(nlen);
		memcpy(info->name.ptr, name, nlen);

		// crispy defaults
		info->droppeditem = MT_NULL;
	} else
		obj_ptr = mobjinfo + idx;

	// parse properties now
	prop_val = val_thing;
	ctx->entry_cb = cb_parse_props;
}

//
// handler: stat

static void finish_state()
{
	dh_parse_action(obj_ptr);
}

//
// callbacks: WAD

static int cb_doomhack(int lump)
{
	kgcbor_ctx_t ctx = {.entry_cb = cb_root, .max_recursion = MAX_CBOR_DEPTH};
	void *data = W_CacheLumpNum(lump, PU_STATIC);
	int ret;

#ifdef DEBUG_TEXT
	printf("[DOOMHACK] Lump %u\n", lump);
#endif

	if(!doomhack_active)
	{
		// data storage
		data_storage = malloc(STORAGE_ALLOC_CHUNK);
		data_free = STORAGE_ALLOC_CHUNK;
		if(!data_storage)
			goto alloc_error;
		// things
		mobjinfo = malloc(sizeof(mobjinfo_code));
		if(!mobjinfo)
			goto alloc_error;
		memcpy(mobjinfo, mobjinfo_code, sizeof(mobjinfo_code));
		// sounds
		S_sfx = malloc(sizeof(sfxinfo_t) * NUMSFX);
		if(!S_sfx)
			goto alloc_error;
		memcpy(S_sfx, S_sfx_code, sizeof(sfxinfo_t) * NUMSFX);
		// states
		states = malloc(sizeof(states_code));
		if(!states)
			goto alloc_error;
		memcpy(states, states_code, sizeof(states_code));
		// it's active now
		doomhack_active = 1;
	}

	// clear sections
	memset(root_item, 0, sizeof(root_item));

	// scan sections in CBOR data
	ret = kgcbor_parse_object(&ctx, data, W_LumpLength(lump));
	if(ret != KGCBOR_RET_OK)
	{
		Z_Free(data);
		I_Error("[DOOMHACK] Unable to parse CBOR lump %d!\n", lump);
		return 0;
	}

	first_state = numstates;
	numstates += root_item[RI_STATES].len;

	states = realloc(states, sizeof(state_t) * numstates);
	if(!states)
		goto alloc_error;

	last_state = numstates;

	// parse sounds
	if(root_item[RI_SOUNDS].ptr)
	{
		ctx.entry_cb = cb_sound_array;
		kgcbor_parse_object(&ctx, root_item[RI_SOUNDS].ptr, -1);
	}

	// parse ammo
	if(root_item[RI_AMMOS].ptr)
	{
		obj_finish = finish_ammo;
		obj_create = create_ammo;
		ctx.entry_cb = cb_root_obj;
		kgcbor_parse_object(&ctx, root_item[RI_AMMOS].ptr, -1);
	}

	// parse weapons
	if(root_item[RI_WEAPONS].ptr)
	{
		obj_finish = NULL;
		obj_create = create_weapon;
		ctx.entry_cb = cb_root_obj;
		kgcbor_parse_object(&ctx, root_item[RI_WEAPONS].ptr, -1);
	}

	// parse things
	if(root_item[RI_THINGS].ptr)
	{
		obj_finish = NULL;
		obj_create = create_thing;
		ctx.entry_cb = cb_root_obj;
		kgcbor_parse_object(&ctx, root_item[RI_THINGS].ptr, -1);
	}

	// parse states
	if(root_item[RI_STATES].ptr)
	{
		obj_finish = finish_state;
		ctx.entry_cb = cb_state_array;
		kgcbor_parse_object(&ctx, root_item[RI_STATES].ptr, -1);
	}

	// done
	Z_Free(data);
	return 0;

alloc_error:
	Z_Free(data);
	I_Error("[DOOMHACK] Memory allocation failed!\n");
	return 0;
}

//
// API

void dh_init()
{
	uint8_t *ptr;
	uint32_t i;

	printf("[DOOMHACK] Init\n");

	// setup built-in thing names
	i = 0;
	ptr = doom_actor_names;
	while(*ptr)
	{
		uint32_t len = *ptr++;

		mobjinfo[i].name.ptr = ptr;
		mobjinfo[i].name.len = len;

		i++;
		ptr += len;
	}

	// setup built-in weapon names
	i = 0;
	ptr = doom_weapon_names;
	while(*ptr)
	{
		uint32_t len = *ptr++;

		weaponinfo[i].name.ptr = ptr;
		weaponinfo[i].name.len = len;

		i++;
		ptr += len;
	}

	// setup built-in ammo names
	i = 0;
	ptr = doom_ammo_names;
	while(*ptr)
	{
		uint32_t len = *ptr++;

		ammoinfo[i].name.ptr = ptr;
		ammoinfo[i].name.len = len;

		i++;
		ptr += len;
	}

	// setup dehacked defaults
	weaponinfo[wp_bfg].ammo_use = deh_bfg_cells_per_shot;

	for(uint32_t i = 0; i < NUMAMMO; i++)
		ammoinfo[i].amax_bkpk = ammoinfo[i].amax * 2;

	// stuff
	numsprites = NUMSPRITES;

	// parse every lump
	W_ForEach("DOOMHACK", cb_doomhack);

	// sfx_chgun link
	S_sfx[sfx_chgun].link = S_sfx + sfx_pistol;
}

void dh_reset_level()
{
	dh_aim_cache.mobj = NULL;
}

void *dh_get_storage(uint32_t len)
{
	void *ret = data_storage;

	len += 3;
	len &= ~3;

	if(len > STORAGE_ALLOC_CHUNK)
		goto alloc_error;

	if(len > data_free)
	{
		// allocate new chunk
		// can't use realloc here, many pointers have been already used!
		data_storage = malloc(STORAGE_ALLOC_CHUNK);
		data_free = STORAGE_ALLOC_CHUNK;
		if(!data_storage)
			goto alloc_error;
	}

	data_free -= len;
	data_storage += len;

	return ret;

alloc_error:
	I_Error("[DOOMHACK] Memory allocation failed!\n");
	return NULL;
}

uint32_t dh_parse_value(const dh_value_t *val, void *base, char *key, kgcbor_ctx_t *ctx, uint8_t type, kgcbor_value_t *value)
{
	// Returns if property should be skipped not a success status!
	while(val->name.ptr)
	{
		if(	type == parser_type[val->type] &&
			ctx->key_len == val->name.len &&
			!memcmp(key, val->name.ptr, val->name.len)
		){
			kgcbor_value_t *dest = (kgcbor_value_t*)(base + val->offset);

			switch(val->type)
			{
				case DHVT_INT8:
					dest->u8 = value->u8;
				break;
				case DHVT_INT16:
					dest->u16 = value->u16;
				break;
				case DHVT_INT32:
					dest->u32 = value->u32;
				break;
				case DHVT_ENUM8:
				{
					const dh_extra_t *extra = val->extra;

					while(extra->name.ptr)
					{
						if(	ctx->val_len == extra->name.len &&
							!memcmp(value->ptr, extra->name.ptr, extra->name.len)
						){
							dest->u8 = extra->value;
							break;
						}
						extra++;
					}
					if(!extra->name.ptr)
						fprintf(stderr, "[DOOMHACK] Unknown value '%.*s' for '%.*s'!\n", ctx->val_len, (char*)value->ptr, ctx->key_len, key);
				}
				break;
				case DHVT_STATENUM:
					if(value->s32 < 0)
					{
						// internal state
						dest->s32 = -value->s32;
						if(dest->s32 >= NUMSTATES)
							dest->s32 = 0;
					} else
					{
						// external state; relative offset
						dest->s32 = first_state + value->s32;
						if(dest->s32 >= last_state)
							dest->s32 = 0;
					}
				break;
				case DHVT_SPRITE:
					dest->s32 = handle_sprite_name(value->s32);
				break;
				case DHVT_STRING:
					// allocate and copy NUL terminated string
					dest->ptr = dh_get_storage(ctx->val_len + 1);
					memcpy(dest->ptr, value->ptr, ctx->val_len);
					((uint8_t*)dest->ptr)[ctx->val_len] = 0;
					// save length
					dest = (kgcbor_value_t*)(base + val->offset + sizeof(void*));
					dest->u32 = ctx->val_len;
				break;
				case DHVT_FLAG_LIST:
				{
					// process flag field with custom offsets
					const def_flag_t *flag = val->extra;
					// this one is special
					prop_flags = flag;
					ctx->entry_cb = cb_parse_flags;
					// clear all flags
					((mobjinfo_t*)base)->flags = 0;
					// don't skip!
					return 0;
				}
				break;
				case DHVT_FLAG_BIT:
					// flip just one bit
					if(value->uint)
						dest->u32 |= (uintptr_t)val->extra;
					else
						dest->u32 &= ~(uintptr_t)val->extra;
				break;
				case DHVT_ACTION:
					dest->ptr = dh_find_action(value->ptr, ctx->val_len);
				break;
				case DHVT_CBOR_OBJ:
					// this one is special; only for temporary use
					dest->ptr = ctx->ptr_obj;
				break;
				case DHVT_SOUNDNUM:
				{
					int idx = find_sound_by_name(value->ptr, ctx->val_len);
					if(idx < 0)
						fprintf(stderr, "[DOOMHACK] Unknown sound '%.*s'!\n", ctx->val_len, (char*)value->ptr);
					else
						dest->u16 = idx;
				}
				break;
				case DHVT_MOBJTYPE:
				{
					int idx = find_thing_by_name(value->ptr, ctx->val_len);
					if(idx < 0)
						fprintf(stderr, "[DOOMHACK] Unknown thing '%.*s'!\n", ctx->val_len, (char*)value->ptr);
					else
						dest->u16 = idx;
				}
				break;
				case DHVT_WEAPTYPE:
				{
					int idx = find_weapon_by_name(value->ptr, ctx->val_len);
					if(idx < 0)
						fprintf(stderr, "[DOOMHACK] Unknown weapon '%.*s'!\n", ctx->val_len, (char*)value->ptr);
					else
						dest->u16 = idx;
				}
				break;
				case DHVT_AMMOTYPE:
				{
					int idx = find_ammo_by_name(value->ptr, ctx->val_len);
					if(idx < 0)
						fprintf(stderr, "[DOOMHACK] Unknown ammo '%.*s'!\n", ctx->val_len, (char*)value->ptr);
					else
						dest->u16 = idx;
				}
				break;
				case DHVT_MOBJFLAG:
				{
					const def_flag_t *flag = flags_thing;
					while(flag->name.ptr)
					{
						if(	ctx->val_len == flag->name.len &&
							!memcmp(value->ptr, flag->name.ptr, flag->name.len)
						){
							dest->u32 = flag->bits;
							break;
						}
						flag++;
					}
					if(!flag->name.ptr)
						fprintf(stderr, "[DOOMHACK] Unknown flag '%.*s'!\n", ctx->val_len, (char*)value->ptr);
				}
				break;
			}

			return 1;
		}
		val++;
	}

	fprintf(stderr, "[DOOMHACK] Unknown property '%.*s'!\n", ctx->key_len, key);

	return 1;
}

