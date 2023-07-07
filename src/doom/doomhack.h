
#define CBOR_STRING(x)	{x, sizeof(x)-1}

enum
{
	DHVT_INT8,
	DHVT_INT16,
	DHVT_INT32,
	DHVT_ENUM8,
	DHVT_STATENUM,
	DHVT_SPRITE,
	DHVT_STRING,
	DHVT_FLAG_LIST,
	DHVT_FLAG_BIT,
	DHVT_ACTION,
	DHVT_CBOR_OBJ,
	DHVT_SOUNDNUM,
	DHVT_MOBJTYPE,
	DHVT_WEAPTYPE,
	DHVT_AMMOTYPE,
	DHVT_MOBJFLAG,
};

typedef struct
{
	void *ptr;
	uint32_t len;
} ptr_with_len_t;

typedef struct
{
	ptr_with_len_t name;
	uint32_t type;
	uint32_t offset;
	const void *extra;
} dh_value_t;

typedef struct
{
	ptr_with_len_t name;
	uint32_t value;
} dh_extra_t;

typedef struct
{
	uint32_t tick;
	mobj_t *mobj;
	mobj_t *target;
	angle_t angle;
	angle_t pitch;
} dh_aim_cache_t;

struct kgcbor_ctx_s;
union kgcbor_value_u;

//

extern int doomhack_active;
extern dh_aim_cache_t dh_aim_cache;

//

void dh_init();
void dh_reset_level();

void *dh_get_storage(uint32_t len);

uint32_t dh_parse_value(const dh_value_t *val, void *base, char *key, struct kgcbor_ctx_s *ctx, uint8_t type, union kgcbor_value_u *value);

void *dh_find_action(void *name, uint32_t nlen);
void dh_parse_action(state_t*);

