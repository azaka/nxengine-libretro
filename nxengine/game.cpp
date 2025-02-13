/* vim: set shiftwidth=3 tabstop=3 textwidth=80 expandtab: */
#include "nx.h"
#include "endgame/island.h"
#include "endgame/credits.h"
#include "intro/intro.h"
#include "intro/title.h"
#include "pause/pause.h"
#include "pause/options.h"
#include "inventory.h"
#include "map_system.h"
#include "game.h"
#include "profile.h"
#include "game.fdh"

static struct TickFunctions
{
	void (*OnTick)(void);
	bool (*OnEnter)(int param);
	void (*OnExit)(void);
}
tickfunctions[] =
{
	NULL,				NULL,			NULL,			// GM_NONE
	game_tick_normal,	NULL,			NULL,			// GM_NORMAL
	inventory_tick,		inventory_init,	NULL,			// GM_INVENTORY
	ms_tick,			ms_init,		ms_close,		// GM_MAP_SYSTEM
	island_tick,		island_init,	NULL,			// GM_ISLAND
	credit_tick,		credit_init,	credit_close,	// GM_CREDITS
	intro_tick,			intro_init,		NULL,			// GM_INTRO
	title_tick,			title_init,		NULL,			// GM_TITLE
	pause_tick,			pause_init,		NULL,			// GP_PAUSED
	options_tick,		options_init,	options_close	// GP_OPTIONS
	//old_options_tick,		old_options_init,	old_options_close	// GP_OPTIONS
};

Object *onscreen_objects[MAX_OBJECTS];
int nOnscreenObjects;

Game game;
TextBox textbox;
ObjProp objprop[OBJ_LAST];

// init Game object: only called once during startup
bool Game::init()
{
	int i;

	memset(static_cast<void*>(&game), 0, sizeof(game));

	// set default properties
	memset(objprop, 0, sizeof(objprop));
	for(i=0;i<OBJ_LAST;i++)
	{
		objprop[i].shaketime = 16;
		objprop[i].sprite = SPR_NULL;
	}

   // auto-generated function to assign sprites to objects
	AssignSprites();
   // assign rest of sprites (to be replaced at some point)
	AssignExtraSprites(); 

   // setup function pointers to AI routines
	if (ai_init())
      return 1;

	if (initslopetable())
      return 1;
	if (initmapfirsttime())
      return 1;

	// create the player object - note that the player 
   // is NOT destroyed on map change
	if (game.createplayer())
      return 1;

	return 0;
}


// reset things to prepare for entry to the next stage
bool Game::initlevel()
{
	Carets::DestroyAll();	// delete smoke clouds, ZZzz's etc...
	ScreenEffects::Stop();	// prevents white flash after island scene when ballos defeated
	
	game.frozen = false;
	game.bossbar.object = NULL;
	nOnscreenObjects = 0;
	
	if (statusbar_init()) return 1;					// reset his displayed health value
	InitPlayer();
	initmap();
	
	game.stageboss.SetType(stages[game.curmap].bossNo);
	game.stageboss.OnMapEntry();
	
	map_scroll_jump(player->CenterX(), player->CenterY());
	
	if (game.switchstage.eventonentry)
	{
		// this prevents a glitch otherwise caused by entry script to Last Cave.
		// i.e. the script immediately <PRI's then fades in while the game is still
		// frozen, thus the player code never has a chance to set the initial frame.
		PHandleAttributes();
		PSelectFrame();
		
		StartScript(game.switchstage.eventonentry);
		game.switchstage.eventonentry = 0;
	}
	
	return 0;
}

bool Game::createplayer()
{
	if (player)
	{
		NX_ERR("game.createplayer: player already exists!\n");
		return 1;
	}
	
	player = (Player *)CreateObject(0, 0, OBJ_PLAYER);
	PInitFirstTime();
	
	return 0;
}


void Game::close(void)
{
	// call any onexit/cleanup function for the current mode
	setmode(GM_NONE);
	
	Objects::DestroyAll(true);	// destroy all objects and player
	FloatText::DeleteAll();
}

/*
void c------------------------------() {}
*/

bool Game::setmode(int newmode, int param, bool force)
{
	if (newmode == 0)
		newmode = GM_NORMAL;
	
	if (game.mode == newmode && !force)
		return 0;
	
	if (tickfunctions[game.mode].OnExit)
		tickfunctions[game.mode].OnExit();
	
	game.mode = newmode;
	
	if (tickfunctions[game.mode].OnEnter)
	{
		if (tickfunctions[game.mode].OnEnter(param))
		{
			NX_ERR("game.setmode: initilization failed for mode %d\n", newmode);
			game.mode = GM_NONE;
			return 1;
		}
	}
	
	return 0;
}

bool Game::pause(int pausemode, int param)
{
   if (game.paused == pausemode)
      return 0;

   if (tickfunctions[game.paused].OnExit)
      tickfunctions[game.paused].OnExit();

   game.paused = pausemode;

   if (tickfunctions[game.paused].OnEnter)
   {
      if (tickfunctions[game.paused].OnEnter(param))
      {
         NX_ERR("game.pause: initilization failed for mode %d\n", pausemode);
         game.paused = 0;
         return 1;
      }
   }

   /*
    * This prevents options menu from leaking inputs to other modes and
    * game player.
    */
   if (!game.paused)
      memcpy(lastpinputs, inputs, sizeof(lastpinputs));
   /*
    * This leaks inputs to other modes.
    * How did nxengine-evo avoid leaking inputs to other modes with this?
    */
   /*
    *  if (!game.paused)
    *     memset(inputs, 0, sizeof(inputs));
    */

   return 0;
}

void Game::tick(void)
{
	if (game.paused)
		tickfunctions[game.paused].OnTick();
	else
	{
		// run scripts
		RunScripts();
		
		// call the tick function for the current game mode
		tickfunctions[game.mode].OnTick();
	}
}


void Game::switchmap(int mapno, int scriptno, int px, int py)
{
	game.switchstage.mapno = mapno;
	game.switchstage.playerx = px;
	game.switchstage.playery = py;
	game.switchstage.eventonentry = scriptno;
}


void Game::reset()
{
	memset(inputs, 0, sizeof(inputs));
	StopLoopSounds();
	StopScripts();
	
	game.pause(false);
	game.setmode(GM_INTRO, 0, true);
}

/*
void c------------------------------() {}
*/

// standard in-game tick (as opposed to title-screen, inventory etc)
void game_tick_normal(void)
{
Object *o;

	player->riding = NULL;
	player->bopped_object = NULL;
	Objects::UpdateBlockStates();

	if (!game.frozen)
	{
		// run AI for player and stageboss first
		HandlePlayer();
		game.stageboss.Run();
		
		// now objects AI and move all objects to their new positions
		Objects::RunAI();
		Objects::PhysicsSim();
		
		// run the "aftermove" AI routines
		HandlePlayer_am();
		game.stageboss.RunAftermove();
		
		FOREACH_OBJECT(o)
		{
			if (!o->deleted)
				o->OnAftermove();
		}
	}

	// important to put this before and not after DrawScene(), or non-existant objects
	// can wind up in the onscreen_objects[] array, and blow up the program on the next tick.
	Objects::CullDeleted();
	
	map_scroll_do();
	
	DrawScene();
	DrawStatusBar();
	fade.Draw();
	
	niku_run();
	if (player->equipmask & EQUIP_NIKUMARU)
		niku_draw(game.counter);
	
	textbox.Draw();
	
	ScreenEffects::Draw();
	map_draw_map_name();	// stage name overlay as on entry
}

// shake screen.
void quake(int quaketime, int snd)
{
	if (game.quaketime < quaketime)
		game.quaketime = quaketime;
	
	if (snd)
		sound((snd != -1) ? snd : SND_QUAKE);
}

// during Ballos fight, since there's already a perpetual quake,
// we need to be able to make an even BIGGER quake effect.
void megaquake(int quaketime, int snd)
{
	if (game.megaquaketime < quaketime)
	{
		game.megaquaketime = quaketime;
		if (game.quaketime < game.megaquaketime)
			game.quaketime = game.megaquaketime;
	}
	
	if (snd)
		sound((snd != -1) ? snd : SND_QUAKE);
}


void DrawScene(void)
{
   int scr_x, scr_y;
	
   Graphics::ClearScreen(BLACK);

   // sporidically-used animated tile feature,
   // e.g. water currents in Waterway
   if (map.nmotiontiles)
      AnimateMotionTiles();
	
   // draw background map tiles
		map_draw_backdrop();
		map_draw(false);
	
	// draw all objects following their z-order
	nOnscreenObjects = 0;
	
	for(Object *o = lowestobject;
		o != NULL;
		o = o->higher)
	{
		if (o == player) continue;	// player drawn specially in DrawPlayer
		
		// keep it's floattext linked with it's position
		o->DamageText->UpdatePos(o);
		
		// shake enemies that were just hit. when they stop shaking,
		// start rising up how many damage they took.
		if (o->shaketime)
		{
			o->display_xoff = (o->shaketime & 2) ? 1 : -1;
			if (!--o->shaketime) o->display_xoff = 0;
		}
		else if (o->DamageWaiting > 0)
		{
			o->DamageText->AddQty(o->DamageWaiting);
			o->DamageWaiting = 0;
		}
		
		// get object's onscreen position
		scr_x = (o->x >> CSF) - (map.displayed_xscroll >> CSF);
		scr_y = (o->y >> CSF) - (map.displayed_yscroll >> CSF);
		scr_x -= sprites[o->sprite].frame[o->frame].dir[o->dir].drawpoint.x;
		scr_y -= sprites[o->sprite].frame[o->frame].dir[o->dir].drawpoint.y;
		
		// don't draw objects that are completely offscreen
		// (+26 so floattext won't suddenly disappear on object near bottom of screen)
		if (scr_x <= SCREEN_WIDTH && scr_y <= SCREEN_HEIGHT+26 && \
			scr_x >= -sprites[o->sprite].w && scr_y >= -sprites[o->sprite].h)
		{
			if (nOnscreenObjects < MAX_OBJECTS-1)
			{
				onscreen_objects[nOnscreenObjects++] = o;
				o->onscreen = true;
			}
			else
			{
				NX_ERR("%s:%d: Max Objects Overflow\n", __FILE__, __LINE__);
				return;
			}
			
			if (!o->invisible && o->sprite != SPR_NULL)
			{
				scr_x += o->display_xoff;
				
				if (o->clip_enable)
				{
					Sprites::draw_sprite_clipped(scr_x, scr_y, o->sprite, o->frame, o->dir, o->clipx1, o->clipx2, o->clipy1, o->clipy2);
				}
				else
					Sprites::draw_sprite(scr_x, scr_y, o->sprite, o->frame, o->dir);
			}
		}
		else
			o->onscreen = false;
	}
	
	// draw the player
	DrawPlayer();
	
	// draw foreground map tiles
   map_draw(TA_FOREGROUND);
	
	// draw carets (always-on-top effects such as boomflash)
	Carets::DrawAll();
	
	// draw rising/falling water in maps like Almond
	map_drawwaterlevel();
	
	// draw all floattext (rising damage and XP amounts)
	FloatText::DrawAll();
	
	//if (game.debug.DrawBoundingBoxes) DrawBoundingBoxes();
	//if (game.debug.debugmode) DrawAttrPoints();
}

/*
void c------------------------------() {}
*/

bool game_load(int num)
{
   Profile p;

   NX_LOG("game_load: loading savefile %d\n", num);

   if (profile_load(GetProfileName(num), &p))
      return 1;

   return game_load(&p);
}

bool game_load(Profile *p)
{
   int i;

   player->hp              = p->hp;
   player->maxHealth       = p->maxhp;

   player->whimstar.nstars = p->num_whimstars;
   player->equipmask       = p->equipmask;

   // load weapons
   for(i=0;i<WPN_COUNT;i++)
   {
      player->weapons[i].hasWeapon = p->weapons[i].hasWeapon;
      player->weapons[i].level = p->weapons[i].level;
      player->weapons[i].xp = p->weapons[i].xp;
      player->weapons[i].ammo = p->weapons[i].ammo;
      player->weapons[i].maxammo = p->weapons[i].maxammo;
   }

   player->curWeapon = p->curWeapon;

   // load inventory
   memcpy(player->inventory, p->inventory, sizeof(player->inventory));
   player->ninventory = p->ninventory;

   // load flags
   memcpy(game.flags, p->flags, sizeof(game.flags));

   // load teleporter slots
   textbox.StageSelect.ClearSlots();
   for(i=0;i<p->num_teleslots;i++)
   {
      int slotno = p->teleslots[i].slotno;
      int scriptno = p->teleslots[i].scriptno;

      textbox.StageSelect.SetSlot(slotno, scriptno);
      NX_LOG(" - Read Teleporter Slot %d: slotno=%d scriptno=%d\n", i, slotno, scriptno);
   }

   // have to load the stage last AFTER the flags are loaded because
   // of the options to appear and disappear objects based on flags.
   if (load_stage(p->stage)) return 1;
   music(p->songno);

   player->x = p->px;
   player->y = p->py;
   player->dir = p->pdir;
   player->hide = false;
   game.showmapnametime = 0;

   return 0;
}


bool game_save(int num)
{
   Profile p;

   NX_LOG("game_save: writing savefile %d\n", num);

   if (game_save(&p))
      return 1;

   if (profile_save(GetProfileName(num), &p))
      return 1;

   return 0;
}

bool game_save(Profile *p)
{
   int i;

   memset(p, 0, sizeof(Profile));

   p->stage         = game.curmap;
   p->songno        = music_cursong();

   p->px            = player->x;
   p->py            = player->y;
   p->pdir          = player->dir;

   p->hp            = player->hp;
   p->maxhp         = player->maxHealth;

   p->num_whimstars = player->whimstar.nstars;
   p->equipmask     = player->equipmask;

   // save weapons
   p->curWeapon     = player->curWeapon;

   for(i=0;i<WPN_COUNT;i++)
   {
      p->weapons[i].hasWeapon = player->weapons[i].hasWeapon;
      p->weapons[i].level     = player->weapons[i].level;
      p->weapons[i].xp        = player->weapons[i].xp;
      p->weapons[i].ammo      = player->weapons[i].ammo;
      p->weapons[i].maxammo   = player->weapons[i].maxammo;
   }

   // save inventory
   p->ninventory = player->ninventory;
   memcpy(p->inventory, player->inventory, sizeof(p->inventory));

   // save flags
   memcpy(p->flags, game.flags, sizeof(p->flags));

   // save teleporter slots
   for(i=0;i<NUM_TELEPORTER_SLOTS;i++)
   {
      int slotno, scriptno;
      if (!textbox.StageSelect.GetSlotByIndex(i, &slotno, &scriptno))
      {
         p->teleslots[p->num_teleslots].slotno = slotno;
         p->teleslots[p->num_teleslots].scriptno = scriptno;
         p->num_teleslots++;
      }
   }

   return 0;
}

// assign sprites for the objects that didn't get covered by the
// auto-generated spritesetup->cpp, and set some properties on the objects.
// This is mostly for objects where the sprite is not named the same as
// the object it is assigned to.
void AssignExtraSprites(void)
{
	objprop[OBJ_PLAYER].sprite            = SPR_MYCHAR;
	objprop[OBJ_NPC_PLAYER].sprite        = SPR_MYCHAR;
	objprop[OBJ_PTELIN].sprite            = SPR_MYCHAR;
	objprop[OBJ_PTELOUT].sprite           = SPR_MYCHAR;
	
	objprop[OBJ_NULL].sprite              = SPR_NULL;
	objprop[OBJ_HVTRIGGER].sprite         = SPR_NULL;
	objprop[OBJ_BUBBLE_SPAWNER].sprite    = SPR_NULL;
	objprop[OBJ_DROPLET_SPAWNER].sprite   = SPR_NULL;
	objprop[OBJ_HEY_SPAWNER].sprite       = SPR_NULL;
	objprop[OBJ_WATERLEVEL].sprite        = SPR_NULL;
	objprop[OBJ_LAVA_DRIP_SPAWNER].sprite = SPR_NULL;
	objprop[OBJ_RED_BAT_SPAWNER].sprite   = SPR_NULL;
	objprop[OBJ_SCROLL_CONTROLLER].sprite = SPR_NULL;
	objprop[OBJ_DOCTOR_GHOST].sprite      = SPR_NULL;
	objprop[OBJ_FALLING_BLOCK].sprite     = SPR_NULL;	// set at runtime based on current map
	objprop[OBJ_FALLING_BLOCK_SPAWNER].sprite = SPR_NULL;
	objprop[OBJ_QUAKE].sprite                 = SPR_NULL;
	objprop[OBJ_BUTE_SPAWNER].sprite          = SPR_NULL;
	objprop[OBJ_SMOKE_DROPPER].sprite         = SPR_NULL;

   // so spawn point is applied
	objprop[OBJ_BUTE_ARROW].sprite            = SPR_BUTE_ARROW_LEFT;
	
	objprop[OBJ_POLISHBABY].defaultnxflags |= NXFLAG_SLOW_WHEN_HURT;
	
	objprop[OBJ_MIMIGAC1].sprite         = SPR_MIMIGAC;
	objprop[OBJ_MIMIGAC2].sprite         = SPR_MIMIGAC;
	objprop[OBJ_MIMIGAC_ENEMY].sprite    = SPR_MIMIGAC;
	objprop[OBJ_MIMIGAC_ENEMY].shaketime = 0;
	
	objprop[OBJ_MISERY_FLOAT].sprite     = SPR_MISERY;
	objprop[OBJ_MISERY_FLOAT].damage     = 1;
	objprop[OBJ_MISERY_STAND].sprite     = SPR_MISERY;
	
	objprop[OBJ_PUPPY_WAG].sprite        = SPR_PUPPY;
	objprop[OBJ_PUPPY_BARK].sprite       = SPR_PUPPY;
	objprop[OBJ_PUPPY_CARRY].sprite      = SPR_PUPPY;
	objprop[OBJ_PUPPY_SLEEP].sprite      = SPR_PUPPY_ASLEEP;
	objprop[OBJ_PUPPY_RUN].sprite        = SPR_PUPPY;
	objprop[OBJ_PUPPY_ITEMS].sprite      = SPR_PUPPY;
	
	objprop[OBJ_BALROG_DROP_IN].sprite   = SPR_BALROG;
	objprop[OBJ_BALROG_BUST_IN].sprite   = SPR_BALROG;
	
	objprop[OBJ_CROWWITHSKULL].sprite      = SPR_CROW;
	objprop[OBJ_ARMADILLO].defaultnxflags |= (NXFLAG_FOLLOW_SLOPE | NXFLAG_SLOW_WHEN_HURT);
	objprop[OBJ_SKULLHEAD_CARRIED].sprite  = SPR_SKULLHEAD;
	
	objprop[OBJ_TOROKO].defaultnxflags    |= NXFLAG_FOLLOW_SLOPE;
	objprop[OBJ_TOROKO_TELEPORT_IN].sprite = SPR_TOROKO;
	
	objprop[OBJ_KING].defaultnxflags      |= NXFLAG_FOLLOW_SLOPE;
	
	objprop[OBJ_FAN_DROPLET].sprite        = SPR_WATER_DROPLET;
	
	objprop[OBJ_MGUN_TRAIL].defaultflags  |= FLAG_IGNORE_SOLID;
	
	objprop[OBJ_BLOCK_MOVEH].sprite        = SPR_MOVING_BLOCK;
	objprop[OBJ_BLOCK_MOVEV].sprite        = SPR_MOVING_BLOCK;
	
	objprop[OBJ_IRONH].shaketime           = 8;

   // omega handles his own shaketime
	objprop[OBJ_OMEGA_BODY].shaketime      = 0;
	objprop[OBJ_OMEGA_BODY].hurt_sound     = SND_ENEMY_HURT_BIG;
	
	objprop[OBJ_OMEGA_LEG].sprite          = SPR_OMG_LEG_INAIR;
	objprop[OBJ_OMEGA_STRUT].sprite        = SPR_OMG_STRUT;
	
	objprop[OBJ_OMEGA_SHOT].death_smoke_amt= 4;
	objprop[OBJ_OMEGA_SHOT].death_sound    = SND_EXPL_SMALL;
	objprop[OBJ_OMEGA_SHOT].initial_hp     = 1;
	objprop[OBJ_OMEGA_SHOT].xponkill       = 1;
	
	objprop[OBJ_BAT_HANG].sprite           = SPR_BAT;
	objprop[OBJ_BAT_CIRCLE].sprite         = SPR_BAT;
	
	objprop[OBJ_FIREBALL1].defaultnxflags  |= NXFLAG_FOLLOW_SLOPE;
	objprop[OBJ_FIREBALL23].defaultnxflags |= NXFLAG_FOLLOW_SLOPE;
	
	objprop[OBJ_CURLY_AI].sprite            = SPR_CURLY;
	objprop[OBJ_CURLY_AI].defaultnxflags   |= NXFLAG_FOLLOW_SLOPE;
	
	objprop[OBJ_CURLY].defaultnxflags      |= NXFLAG_FOLLOW_SLOPE;
	
	objprop[OBJ_MINICORE].hurt_sound        = SND_ENEMY_HURT_COOL;
	objprop[OBJ_CORE_CONTROLLER].hurt_sound = SND_CORE_HURT;
	
	objprop[OBJ_CURLY_CARRIED].sprite       = SPR_CURLY;
	
	objprop[OBJ_BALROG_BOSS_RUNNING].sprite  = SPR_BALROG;
	objprop[OBJ_BALROG_BOSS_FLYING].sprite   = SPR_BALROG;
	objprop[OBJ_BALROG_BOSS_MISSILES].sprite = SPR_BALROG;
	
	objprop[OBJ_XP].sprite                   = SPR_XP_SMALL;
	
	objprop[OBJ_NPC_IGOR].sprite             = SPR_IGOR;
	objprop[OBJ_BOSS_IGOR].sprite            = SPR_IGOR;
	objprop[OBJ_BOSS_IGOR_DEFEATED].sprite   = SPR_IGOR;
	objprop[OBJ_IGOR_BALCONY].sprite         = SPR_IGOR;
	
	objprop[OBJ_X_TARGET].hurt_sound         = SND_ENEMY_HURT_COOL;
	objprop[OBJ_X_INTERNALS].shaketime       = 9;
	objprop[OBJ_X_MAINOBJECT].xponkill       = 1;
	
	objprop[OBJ_POOH_BLACK_BUBBLE].xponkill  = 0;
	objprop[OBJ_POOH_BLACK_DYING].sprite     = SPR_POOH_BLACK;
	
	objprop[OBJ_BOOSTER_FALLING].sprite      = SPR_PROFESSOR_BOOSTER;
	
	objprop[OBJ_MIMIGA_FARMER_STANDING].sprite = SPR_MIMIGA_FARMER;
	objprop[OBJ_MIMIGA_FARMER_WALKING].sprite  = SPR_MIMIGA_FARMER;
	objprop[OBJ_DROLL_GUARD].sprite            = SPR_DROLL;
	
	objprop[OBJ_MA_PIGNON_CLONE].sprite        = SPR_MA_PIGNON;
	
	objprop[OBJ_DOCTOR_SHOT_TRAIL].sprite      = SPR_DOCTOR_SHOT;
	
	// they're still able to detect when they touch floor; etc,
	// but we don't want say a falling one to get blocked by the ceiling.
	objprop[OBJ_RED_ENERGY].defaultflags      |= FLAG_IGNORE_SOLID;
	
	objprop[OBJ_SUE_TELEPORT_IN].sprite        = SPR_SUE;
	
	objprop[OBJ_MISERY_BAT].sprite             = SPR_ORANGE_BAT_FINAL;
	objprop[OBJ_UD_MINICORE_IDLE].sprite       = SPR_UD_MINICORE;

   // for bbox only, object is invisible
	objprop[OBJ_WHIMSICAL_STAR].sprite         = SPR_WHIMSICAL_STAR;
}

