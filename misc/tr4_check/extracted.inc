// Store stage: keeps list state and fires the list-end interrupts, and decodes
// nothing. All vertex work happens later, on the replay (TArender.MD TR1).
struct TaStoreDecoder
{
#define TA_STORE_NOP1(name, type) static void name(type*) {}
	static void StartList(u32) {}
	static void EndList(u32) {}
	static void StartPolyStrip() {}
	static void EndPolyStrip() {}
	static void ModVolStripEnd() {}
	static void SetTileClip(u32, u32, u32, u32) {}
	static void TileClipMode(u32) {}
	TA_STORE_NOP1(AppendPolyParam0, TA_PolyParam0)
	TA_STORE_NOP1(AppendPolyParam1, TA_PolyParam1)
	TA_STORE_NOP1(AppendPolyParam2A, TA_PolyParam2A)
	TA_STORE_NOP1(AppendPolyParam2B, TA_PolyParam2B)
	TA_STORE_NOP1(AppendPolyParam3, TA_PolyParam3)
	TA_STORE_NOP1(AppendPolyParam4A, TA_PolyParam4A)
	TA_STORE_NOP1(AppendPolyParam4B, TA_PolyParam4B)
	TA_STORE_NOP1(AppendPolyVertex0, TA_Vertex0)
	TA_STORE_NOP1(AppendPolyVertex1, TA_Vertex1)
	TA_STORE_NOP1(AppendPolyVertex2, TA_Vertex2)
	TA_STORE_NOP1(AppendPolyVertex3, TA_Vertex3)
	TA_STORE_NOP1(AppendPolyVertex4, TA_Vertex4)
	TA_STORE_NOP1(AppendPolyVertex5A, TA_Vertex5A)
	TA_STORE_NOP1(AppendPolyVertex5B, TA_Vertex5B)
	TA_STORE_NOP1(AppendPolyVertex6A, TA_Vertex6A)
	TA_STORE_NOP1(AppendPolyVertex6B, TA_Vertex6B)
	TA_STORE_NOP1(AppendPolyVertex7, TA_Vertex7)
	TA_STORE_NOP1(AppendPolyVertex8, TA_Vertex8)
	TA_STORE_NOP1(AppendPolyVertex9, TA_Vertex9)
	TA_STORE_NOP1(AppendPolyVertex10, TA_Vertex10)
	TA_STORE_NOP1(AppendPolyVertex11A, TA_Vertex11A)
	TA_STORE_NOP1(AppendPolyVertex11B, TA_Vertex11B)
	TA_STORE_NOP1(AppendPolyVertex12A, TA_Vertex12A)
	TA_STORE_NOP1(AppendPolyVertex12B, TA_Vertex12B)
	TA_STORE_NOP1(AppendPolyVertex13A, TA_Vertex13A)
	TA_STORE_NOP1(AppendPolyVertex13B, TA_Vertex13B)
	TA_STORE_NOP1(AppendPolyVertex14A, TA_Vertex14A)
	TA_STORE_NOP1(AppendPolyVertex14B, TA_Vertex14B)
	TA_STORE_NOP1(AppendSpriteParam, TA_SpriteParam)
	TA_STORE_NOP1(AppendSpriteVertexA, TA_Sprite1A)
	TA_STORE_NOP1(AppendSpriteVertexB, TA_Sprite1B)
	TA_STORE_NOP1(StartModVol, TA_ModVolParam)
	TA_STORE_NOP1(AppendModVolVertexA, TA_ModVolA)
	TA_STORE_NOP1(AppendModVolVertexB, TA_ModVolB)
#undef TA_STORE_NOP1
	static void ListInit() {}
	static void ListCont() {}
	static void SoftReset() {}
};


// -- TR4: table-driven store stage. The store only needs record size and list state, so one table
// lookup per 32-byte record replaces the splitter (libswirl ta_fsm); a state says what the next word is.
enum
{
	TAS_NOLIST = 0,  // no list open; a header must open one
	TAS_P32,         // list open, not a modifier volume, 32-byte vertices
	TAS_P64,         // same, 64-byte vertices
	TAS_SKIP32,      // second half of a 64-byte record, then TAS_P32
	TAS_SKIP64,      // second half of a 64-byte record, then TAS_P64
	TAS_MV,          // modifier volume list open; its vertices are 64 bytes
	TAS_SKIPMV,      // second half of a modvol vertex, then TAS_MV
	TAS_COUNT = 8
};
#define TAS_SLOW 0x80

// Indexed (state << 8) | (ParaType << 5) | (obj_ctrl >> 2). UV_16bit/Gouraud/Shadow never change a
// record's size, so the key drops them; without the mask a sprite aliases a 16-bit-UV polygon.
static u8 s_fsm[TAS_COUNT * 256];
static u8 s_fsm_state = TAS_NOLIST;
static u32 s_store_list = ListType_None;
// Latched once at InitRenderer, so a run never changes store path mid-stream.
static bool s_ta_fast;

// P66 TA Data Gate (Flycast ta_ctx == NULL, Vigilante 8): no list init is current before the first one and after a
// STARTRENDER of the TA's context. With the row ON the TA records of that time are dropped.
static bool s_ta_nolist = true;
static bool s_ta_closed;
// TA Fast Store with the gate open: the flat SQ path's test, so a closed gate costs it nothing.
static bool s_flat_ok;
static u32 s_stat_gate;

static void ta_gate_set(bool nolist)
{
	s_ta_nolist = nolist;
	s_ta_closed = nolist && g_ta_data_gate_preset != 0;
	s_flat_ok = s_ta_fast && !s_ta_closed;
}

static inline u32 fsm_key(u32 para, u32 objc)
{
	return (para << 5) | ((objc >> 2) & 31);
}

// The 64-byte vertex types, from the table above poly_data_type_id() in ta.h.
static inline bool ta_vtx_is_64(u32 pdid)
{
	return pdid == 5 || pdid == 6 || pdid == 11 || pdid == 12 || pdid == 13 || pdid == 14;
}

// Built from drkPvr's ta_type_lut, so the two store paths size every record the
// same way. TileAccel.Init() must have run first.
static void ta_fsm_build()
{
	for (u32 st = 0; st < TAS_COUNT; st++)
	for (u32 pt = 0; pt < 8; pt++)
	for (u32 k = 0; k < 32; k++)
	{
		u8 e;
		if (st == TAS_SKIP32)
			e = TAS_P32;
		else if (st == TAS_SKIP64)
			e = TAS_P64;
		else if (st == TAS_SKIPMV)
			e = TAS_MV;
		else if (st >= TAS_COUNT - 1)
			e = TAS_NOLIST;  // state 7 is unused
		else switch (pt)
		{
		case ParamType_User_Tile_Clip:
			e = (u8)st;      // 32 bytes, ignored by the store stage
			break;

		case ParamType_Polygon_or_Modifier_Volume:
			if (st == TAS_NOLIST)
				e = TAS_SLOW | (u8)st;   // opens the list, so it needs ListType
			else if (st == TAS_MV)
				e = TAS_MV;              // 32-byte header, 64-byte vertices
			else
			{
				u32 uid = TileAccel.ta_type_lut[k << 2];
				u8 vtx = ta_vtx_is_64((u8)uid) ? TAS_P64 : TAS_P32;
				if ((uid >> 30) == SZ64)
					e = (vtx == TAS_P64) ? TAS_SKIP64 : TAS_SKIP32;
				else
					e = vtx;
			}
			break;

		case ParamType_Sprite:
			// Sprite vertices are 64 bytes, like modvol ones, so a modvol list
			// keeps its state either way.
			if (st == TAS_NOLIST)
				e = TAS_SLOW | (u8)st;
			else
				e = (st == TAS_MV) ? TAS_MV : TAS_P64;
			break;

		case ParamType_Vertex_Parameter:
			if (st == TAS_P64)
				e = TAS_SKIP64;
			else if (st == TAS_MV)
				e = TAS_SKIPMV;
			else
				e = (u8)st;  // 32-byte vertex, or a stray one with no list open
			break;

		default:             // End_Of_List and the reserved types
			e = TAS_SLOW | (u8)st;
			break;
		}
		s_fsm[(st << 8) | (pt << 5) | k] = e;
	}
}

static void ta_store_reset()
{
	s_fsm_state = TAS_NOLIST;
	s_store_list = ListType_None;
}

// The records the table cannot answer from obj_ctrl alone: they open or close a
// list, so they need the live list state.
static u8 ta_store_slow(Ta_Dma* d, u8 st)
{
	PCW pcw = d->pcw;
	switch (pcw.ParaType)
	{
	case ParamType_End_Of_List:
		// As the splitter: an end with no list open adopts the parameter's own
		// ListType, and spg_SchedListEndIrq drops anything above 4.
		if (s_store_list == ListType_None)
			s_store_list = pcw.ListType;
		spg_SchedListEndIrq(s_store_list);
		s_store_list = ListType_None;
		spe_flush(s_ta);
		return TAS_NOLIST;

	case ParamType_Polygon_or_Modifier_Volume:
	case ParamType_Sprite:
		if (s_store_list == ListType_None)
			s_store_list = pcw.ListType;
		if (IsModVolList(s_store_list))
			return TAS_MV;
		// The list is open and ordinary now, so the table row answers the rest.
		return s_fsm[(TAS_P32 << 8) | fsm_key(pcw.ParaType, pcw.obj_ctrl)];

	default:
		// Object_List_Set and the two reserved types. The splitter calls die()
		// here, which on PS3 is a silent freeze; log once and skip instead.
		{
			static bool warned;
			if (!warned)
			{
				warned = true;
				printf("[ps3rend] TA parameter type %u ignored\n", pcw.ParaType);
			}
		}
		return st;
	}
}

// The table entry for state st and a record whose first word is pcw. ParaType is bits 31..29 and
// obj_ctrl the low byte, in both endians.
static inline u32 ta_fsm_entry(u32 st, u32 pcw)
{
	return s_fsm[(st << 8) | ((pcw >> 24) & 0xE0) | ((pcw >> 2) & 0x1F)];
}

// One record at p: the next state. A list open or close takes ta_store_slow.
static inline u32 ta_fsm_step(u32 st, u32 pcw, Ta_Dma* p)
{
	const u32 e = ta_fsm_entry(st, pcw);
	return (e & TAS_SLOW) ? ta_store_slow(p, (u8)(e & 7)) : e;
}

// The state carries across calls, so a 64-byte record split over two SQ or DMA blocks resumes by itself.
static void ta_store_fsm_run(Ta_Dma* p, u32 words)
{
	u32 st = s_fsm_state;
	for (u32 i = 0; i < words; i++, p++)
		st = ta_fsm_step(st, p->pcw.full, p);
	s_fsm_state = (u8)st;
}
