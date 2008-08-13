/*
 * See Licensing and Copyright notice in naev.h
 */

/**
 * @file misn_lua.c
 *
 * @brief Handles the mission lua bindings.
 */


#include "misn_lua.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "lua.h"
#include "lauxlib.h"

#include "nlua.h"
#include "nlua_space.h"
#include "hook.h"
#include "mission.h"
#include "log.h"
#include "naev.h"
#include "rng.h"
#include "space.h"
#include "toolkit.h"
#include "land.h"
#include "pilot.h"
#include "player.h"
#include "ntime.h"
#include "xml.h"
#include "nluadef.h"



/* similar to lua vars, but with less variety */
#define MISN_VAR_NIL    0 /**< Nil type. */
#define MISN_VAR_NUM    1 /**< Number type. */
#define MISN_VAR_BOOL   2 /**< Boolean type. */
#define MISN_VAR_STR    3 /**< String type. */
/**
 * @struct misn_var
 *
 * @brief Contains a mission variable.
 */
typedef struct misn_var_ {
   char* name; /**< Name of the variable. */
   char type; /**< Type of the variable. */
   union {
      double num; /**< Used if type is number. */
      char* str; /**< Used if type is string. */
      int b; /**< Used if type is boolean. */
   } d; /**< Variable data. */
} misn_var;


/*
 * variable stack
 */
static misn_var* var_stack = NULL; /**< Stack of mission variables. */
static int var_nstack = 0; /**< Number of mission variables. */
static int var_mstack = 0; /**< Memory size of the mission variable stack. */


/*
 * current mission
 */
static Mission *cur_mission = NULL; /**< Contains the current mission for a running script. */
static int misn_delete = 0; /**< if 1 delete current mission */


/*
 * prototypes
 */
/* static */
static int var_add( misn_var *var );
static void var_free( misn_var* var );
static unsigned int hook_generic( lua_State *L, char* stack );
/* externed */
int misn_run( Mission *misn, char *func );
int var_save( xmlTextWriterPtr writer );
int var_load( xmlNodePtr parent );
/* external */
extern void mission_sysMark (void);


/*
 * libraries
 */
/* misn */
static int misn_setTitle( lua_State *L );
static int misn_setDesc( lua_State *L );
static int misn_setReward( lua_State *L );
static int misn_setMarker( lua_State *L );
static int misn_factions( lua_State *L );
static int misn_accept( lua_State *L );
static int misn_finish( lua_State *L );
static const luaL_reg misn_methods[] = {
   { "setTitle", misn_setTitle },
   { "setDesc", misn_setDesc },
   { "setReward", misn_setReward },
   { "setMarker", misn_setMarker },
   { "factions", misn_factions },
   { "accept", misn_accept },
   { "finish", misn_finish },
   {0,0}
}; /**< Mission lua methods. */
/* var */
static int var_peek( lua_State *L );
static int var_pop( lua_State *L );
static int var_push( lua_State *L );
static const luaL_reg var_methods[] = {
   { "peek", var_peek },
   { "pop", var_pop },
   { "push", var_push },
   {0,0}
}; /**< Mission variable lua methods. */
static const luaL_reg var_cond_methods[] = {
   { "peek", var_peek },
   {0,0}
}; /**< Conditional mission variable lua methods. */
/* player */
static int player_getname( lua_State *L );
static int player_shipname( lua_State *L );
static int player_freeSpace( lua_State *L );
static int player_addCargo( lua_State *L );
static int player_rmCargo( lua_State *L );
static int player_pay( lua_State *L );
static int player_msg( lua_State *L );
static int player_modFaction( lua_State *L );
static int player_modFactionRaw( lua_State *L );
static int player_getFaction( lua_State *L );
static int player_getRating( lua_State *L );
static const luaL_reg player_methods[] = {
   { "name", player_getname },
   { "ship", player_shipname },
   { "freeCargo", player_freeSpace },
   { "addCargo", player_addCargo },
   { "rmCargo", player_rmCargo },
   { "pay", player_pay },
   { "msg", player_msg },
   { "modFaction", player_modFaction },
   { "modFactionRaw", player_modFactionRaw },
   { "getFaction", player_getFaction },
   { "getRating", player_getRating },
   {0,0}
}; /**< Player lua methods. */
static const luaL_reg player_cond_methods[] = {
   { "name", player_getname },
   { "ship", player_shipname },
   { "getFaction", player_getFaction },
   { "getRating", player_getRating },
   {0,0}
}; /**< Conditional player lua methods. */
/* hooks */
static int hook_land( lua_State *L );
static int hook_takeoff( lua_State *L );
static int hook_time( lua_State *L );
static int hook_enter( lua_State *L );
static int hook_pilot( lua_State *L );
static const luaL_reg hook_methods[] = {
   { "land", hook_land },
   { "takeoff", hook_takeoff },
   { "time", hook_time },
   { "enter", hook_enter },
   { "pilot", hook_pilot },
   {0,0}
}; /**< Hook lua methods. */
/* pilots */
static int pilot_addFleet( lua_State *L );
static int pilot_rename( lua_State *L );
static const luaL_reg pilot_methods[] = {
   { "add", pilot_addFleet },
   { "rename", pilot_rename },
   {0,0}
}; /**< Pilot lua methods. */


/**
 * @fn int misn_loadLibs( lua_State *L )
 *
 * @brief Registers all the mission libraries.
 *
 *    @param L Lua state.
 *    @return 0 on success.
 */
int misn_loadLibs( lua_State *L )
{
   lua_loadNaev(L);
   lua_loadMisn(L);
   lua_loadVar(L,0);
   lua_loadSpace(L,0);
   lua_loadTime(L,0);
   lua_loadPlayer(L,0);
   lua_loadRnd(L);
   lua_loadTk(L);
   lua_loadHook(L);
   lua_loadPilot(L);
   return 0;
}
/**
 * @fn int misn_loadCondLibs( lua_State *L )
 *
 * @brief Registers all the mission conditional libraries.
 *
 *    @param L Lua state.
 *    @return 0 on success.
 */
int misn_loadCondLibs( lua_State *L )
{
   lua_loadTime(L,1);
   lua_loadSpace(L,1);
   lua_loadVar(L,1);
   lua_loadPlayer(L,1);
   return 0;
}
/*
 * individual library loading
 */
/**
 * @fn int lua_loadMisn( lua_State *L )
 * @brief Loads the mission lua library.
 *    @param L Lua state.
 */
int lua_loadMisn( lua_State *L )
{  
   luaL_register(L, "misn", misn_methods);
   return 0;
}  
/**
 * @fn int lua_loadVar( lua_State *L )
 * @brief Loads the mission variable lua library.
 *    @param L Lua state.
 */
int lua_loadVar( lua_State *L, int readonly )
{
   if (readonly == 0)
      luaL_register(L, "var", var_methods);
   else
      luaL_register(L, "var", var_cond_methods);
   return 0;
}  
/**
 * @fn int lua_loadPlayer( lua_State *L )
 * @brief Loads the player lua library.
 *    @param L Lua state.
 */
int lua_loadPlayer( lua_State *L, int readonly )
{
   if (readonly == 0)
      luaL_register(L, "player", player_methods);
   else
      luaL_register(L, "player", player_cond_methods);
   return 0;
}  
/**
 * @fn int lua_loadHook( lua_State *L )
 * @brief Loads the hook lua library.
 *    @param L Lua state.
 */
int lua_loadHook( lua_State *L )
{
   luaL_register(L, "hook", hook_methods);
   return 0;
}
/**
 * @fn int lua_loadPilot( lua_State *L )
 * @brief Loads the pilot lua library.
 *    @param L Lua state.
 */
int lua_loadPilot( lua_State *L )
{
   luaL_register(L, "pilot", pilot_methods);
   return 0;
}


/**
 * @fn int misn_run( Mission *misn, char *func )
 * 
 * @brief Runs a mission function.
 *
 *    @param misn Mission that owns the function.
 *    @param func Name of the function to call.
 *    @return -1 on error, 1 on misn.finish() call and 0 normally.
 */
int misn_run( Mission *misn, char *func )
{
   int i, ret;
   char* err;

   cur_mission = misn;
   misn_delete = 0;

   lua_getglobal( misn->L, func );
   if ((ret = lua_pcall(misn->L, 0, 0, 0))) { /* error has occured */
      err = (lua_isstring(misn->L,-1)) ? (char*) lua_tostring(misn->L,-1) : NULL;
      if (strcmp(err,"Mission Done")!=0)
         WARN("Mission '%s' -> '%s': %s",
               cur_mission->data->name, func, (err) ? err : "unknown error");
      else ret = 1;
   }

   /* mission is finished */
   if (misn_delete) {
      mission_cleanup( cur_mission );
      for (i=0; i<MISSION_MAX; i++)
         if (cur_mission == &player_missions[i]) {
            memmove( &player_missions[i], &player_missions[i+1],
                  sizeof(Mission) * (MISSION_MAX-i-1) );
            break;
         }
   }

   cur_mission = NULL;

   return ret;
}


/**
 * @fn int var_save( xmlTextWriterPtr writer )
 *
 * @brief Saves the mission variables.
 *
 *    @param writer XML Writer to use.
 *    @return 0 on success.
 */
int var_save( xmlTextWriterPtr writer )
{
   int i;

   xmlw_startElem(writer,"vars");

   for (i=0; i<var_nstack; i++) {
      xmlw_startElem(writer,"var");

      xmlw_attr(writer,"name",var_stack[i].name);

      switch (var_stack[i].type) {
         case MISN_VAR_NIL:
            xmlw_attr(writer,"type","nil");
            break;
         case MISN_VAR_NUM:
            xmlw_attr(writer,"type","num");
            xmlw_str(writer,"%d",var_stack[i].d.num);
            break;
         case MISN_VAR_BOOL:
            xmlw_attr(writer,"type","bool");
            xmlw_str(writer,"%d",var_stack[i].d.b);
            break;
         case MISN_VAR_STR:
            xmlw_attr(writer,"type","str");
            xmlw_str(writer,var_stack[i].d.str);
            break;
      }

      xmlw_endElem(writer); /* "var" */
   }

   xmlw_endElem(writer); /* "vars" */

   return 0;
}


/**
 * @fn int var_load( xmlNodePtr parent )
 *
 * @brief Loads the vars from XML file.
 *
 *    @param parent Parent node containing the variables.
 *    @return 0 on success.
 */
int var_load( xmlNodePtr parent )
{
   char *str;
   xmlNodePtr node, cur;
   misn_var var;

   var_cleanup();

   node = parent->xmlChildrenNode;

   do {
      if (xml_isNode(node,"vars")) {
         cur = node->xmlChildrenNode;
         
         do {
            if (xml_isNode(cur,"var")) {
               xmlr_attr(cur,"name",var.name);
               xmlr_attr(cur,"type",str);
               if (strcmp(str,"nil")==0)
                  var.type = MISN_VAR_NIL;
               else if (strcmp(str,"num")==0) {
                  var.type = MISN_VAR_NUM;
                  var.d.num = atoi( xml_get(cur) );
               }
               else if (strcmp(str,"bool")) {
                  var.type = MISN_VAR_BOOL;
                  var.d.b = atoi( xml_get(cur) );
               }
               else if (strcmp(str,"str")) {
                  var.type = MISN_VAR_STR;
                  var.d.str = strdup( xml_get(cur) );
               }
               else { /* super error checking */
                  WARN("Unknown var type '%s'", str);
                  free(var.name);
                  continue;
               }
               free(str);
               var_add( &var );
            }
         } while (xml_nextNode(cur));
      }
   } while (xml_nextNode(node));

   return 0;
}


/**
 * @fn static int var_add( misn_var *new_var )
 *
 * @brief Adds a var to the stack, strings will be SHARED, don't free.
 *
 *    @param new_var Variable to add.
 *    @return 0 on success.
 */
static int var_add( misn_var *new_var )
{
   int i;

   if (var_nstack+1 > var_mstack) { /* more memory */
      var_mstack += 64; /* overkill ftw */
      var_stack = realloc( var_stack, var_mstack * sizeof(misn_var) );
   }

   /* check if already exists */
   for (i=0; i<var_nstack; i++)
      if (strcmp(new_var->name,var_stack[i].name)==0) { /* overwrite */
         var_free( &var_stack[i] );
         memcpy( &var_stack[i], new_var, sizeof(misn_var) );
         return 0;
      }
   
   memcpy( &var_stack[var_nstack], new_var, sizeof(misn_var) );
   var_nstack++;

   return 0;
}




/**
 * @defgroup MISN Misn Lua bindings
 *
 * @brief Generic mission Lua bindings.
 *
 * Functions should be called like:
 *
 * @code
 * misn.function( parameters )
 * @endcode
 *
 * @{
 */
/**
 * @fn static int misn_setTitle( lua_State *L )
 *
 * @brief setTitle( string title )
 *
 * Sets the current mission title.
 *
 *    @param title Title to use for mission.
 */
static int misn_setTitle( lua_State *L )
{
   NLUA_MIN_ARGS(1);
   if (lua_isstring(L, 1)) {
      if (cur_mission->title) /* cleanup old title */
         free(cur_mission->title);
      cur_mission->title = strdup((char*)lua_tostring(L, 1));
   }
   return 0;
}
/**
 * @fn static int misn_setDesc( lua_State *L )
 *
 * @brief setDesc( string desc )
 *
 * Sets the current mission description.
 *
 *    @param desc Description to use for mission.
 */
static int misn_setDesc( lua_State *L )
{
   NLUA_MIN_ARGS(1);
   if (lua_isstring(L, 1)) {    
      if (cur_mission->desc) /* cleanup old description */
         free(cur_mission->desc);
      cur_mission->desc = strdup((char*)lua_tostring(L, 1));
   }
   else NLUA_INVALID_PARAMETER();
   return 0;
}
/**
 * @fn static int misn_setReward( lua_State *L )
 *
 * @brief setReward( string reward )
 *
 * Sets the current mission reward description.
 *
 *    @param reward Description of the reward to use.
 */
static int misn_setReward( lua_State *L )
{
   NLUA_MIN_ARGS(1);
   if (lua_isstring(L, 1)) {    
      if (cur_mission->reward != NULL) /* cleanup old reward */
         free(cur_mission->reward);
      cur_mission->reward = strdup((char*)lua_tostring(L, 1));
   }
   else NLUA_INVALID_PARAMETER();
   return 0;
}
/**
 * @fn static int misn_setMarker( lua_State *L )
 *
 * @brief setMarker( [system sys )
 *
 * Sets the mission marker on the system.  If no parameters are passed it
 * unsets the current marker.
 *
 *    @param sys System to mark.
 */
static int misn_setMarker( lua_State *L )
{
   LuaSystem *sys;

   /* No parameter clears the marker */
   if (lua_gettop(L)==0) {
      if (cur_mission->sys_marker != NULL)
         free(cur_mission->sys_marker);
      mission_sysMark(); /* Clear the marker */
   }

   /* Passing in a Star System */
   if (lua_issystem(L,1)) {
      sys = lua_tosystem(L,1);
      cur_mission->sys_marker = strdup(sys->s->name);
      mission_sysMark(); /* mark the system */
   }
   else NLUA_INVALID_PARAMETER();

   return 0;
}
/**
 * @fn static int misn_factions( lua_State *L )
 *
 * @brief table factions( nil )
 *
 * Gets the factions the mission is available for.
 *
 *    @return A containing the factions.
 */
static int misn_factions( lua_State *L )
{
   int i;
   MissionData *dat;

   dat = cur_mission->data;

   /* we'll push all the factions in table form */
   lua_newtable(L);
   for (i=0; i<dat->avail.nfactions; i++) {
      lua_pushnumber(L,i+1); /* index, starts with 1 */
      lua_pushnumber(L,dat->avail.factions[i]); /* value */
      lua_rawset(L,-3); /* store the value in the table */
   }
   return 1;
}
/**
 * @fn static int misn_accept( lua_State *L )
 *
 * @brief bool accept( nil )
 *
 * Attempts to accept the mission.
 *
 *    @return true if mission was properly accepted.
 */
static int misn_accept( lua_State *L )
{
   int i, ret;

   ret = 0;

   /* find last mission */
   for (i=0; i<MISSION_MAX; i++)
      if (player_missions[i].data == NULL) break;

   /* no missions left */
   if (i>=MISSION_MAX) ret = 1;
   else { /* copy it over */
      memcpy( &player_missions[i], cur_mission, sizeof(Mission) );
      memset( cur_mission, 0, sizeof(Mission) );
      cur_mission = &player_missions[i];
   }

   lua_pushboolean(L,!ret); /* we'll convert C style return to lua */
   return 1;
}
/**
 * @fn static int misn_finish( lua_State *L )
 *
 * @brief finish( bool properly )
 *
 * Finishes the mission.
 *
 *    @param properly If true and the mission is unique it marks the mission
 *                     as completed.  If false it deletes the mission but
 *                     doesn't mark it as completed.  If the parameter isn't
 *                     passed it just ends the mission.
 */
static int misn_finish( lua_State *L )
{
   int b;

   if (lua_isboolean(L,1)) b = lua_toboolean(L,1);
   else {
      lua_pushstring(L, "Mission Done");
      lua_error(L); /* THERE IS NO RETURN */
      return 0;
   }

   misn_delete = 1;

   if (b && mis_isFlag(cur_mission->data,MISSION_UNIQUE))
      player_missionFinished( mission_getID( cur_mission->data->name ) );

   lua_pushstring(L, "Mission Done");
   lua_error(L); /* shouldn't return */

   return 0;
}
/**
 * @}
 */



/**
 * @defgroup VAR Mission Variable Lua bindings
 *
 * @brief Mission variable Lua bindings.
 *
 * Mission variables are similar to Lua variables, but are conserved for each
 *  player across all the missions.  They are good for storing campaign or
 *  other global values.
 *
 * Functions should be called like:
 *
 * @code
 * var.function( parameters )
 * @endcode
 */
/**
 * @fn int var_checkflag( char* str )
 *
 * @brief Checks to see if a mission var exists.
 *
 *    @param str Name of the mission var.
 *    @return 1 if it exists, 0 if it doesn't.
 */
int var_checkflag( char* str )
{
   int i;

   for (i=0; i<var_nstack; i++)
      if (strcmp(var_stack[i].name,str)==0)
         return 1;
   return 0;
}
/**
 * @fn static int var_peek( lua_State *L )
 * @ingroup VAR
 *
 * @brief misn_var peek( string name )
 *
 * Gets the mission variable value of a certain name.
 *
 *    @param name Name of the mission variable to get.
 *    @return The value of the mission variable which will depend on what type
 *             it is.
 */
static int var_peek( lua_State *L )
{
   NLUA_MIN_ARGS(1);
   int i;
   char *str;

   if (lua_isstring(L,1)) str = (char*) lua_tostring(L,1);
   else {
      NLUA_DEBUG("Trying to peek a var with non-string name");
      return 0;
   }

   for (i=0; i<var_nstack; i++)
      if (strcmp(str,var_stack[i].name)==0) {
         switch (var_stack[i].type) {
            case MISN_VAR_NIL:
               lua_pushnil(L);
               break;
            case MISN_VAR_NUM:
               lua_pushnumber(L,var_stack[i].d.num);
               break;
            case MISN_VAR_BOOL:
               lua_pushboolean(L,var_stack[i].d.b);
               break;
            case MISN_VAR_STR:
               lua_pushstring(L,var_stack[i].d.str);
               break;
         }
         return 1;
      }

   lua_pushnil(L);
   return 1;
}
/**
 * @fn static int var_pop( lua_State *L )
 * @ingroup VAR
 *
 * @brief pop( string name )
 *
 * Pops a mission variable off the stack, destroying it.
 *
 *    @param name Name of the mission variable to pop.
 */
static int var_pop( lua_State *L )
{
   NLUA_MIN_ARGS(1);
   int i;
   char* str;

   if (lua_isstring(L,1)) str = (char*) lua_tostring(L,1);
   else {
      NLUA_DEBUG("Trying to pop a var with non-string name");
      return 0;
   }

   for (i=0; i<var_nstack; i++)
      if (strcmp(str,var_stack[i].name)==0) {
         var_free( &var_stack[i] );
         memmove( &var_stack[i], &var_stack[i+1], sizeof(misn_var)*(var_nstack-i-1) );
         var_stack--;
         return 0;
      } 

   NLUA_DEBUG("Var '%s' not found in stack", str);
   return 0;
}
/**
 * @fn static int var_push( lua_State *L )
 * @ingroup VAR
 *
 * @brief push( string name, value )
 *
 * Creates a new mission variable.
 *
 *    @param name Name to use for the new mission variable.
 *    @param value Value of the new mission variable.  Accepted types are:
 *                  nil, bool, string or number.
 */
static int var_push( lua_State *L )
{
   NLUA_MIN_ARGS(2);
   char *str;
   misn_var var;

   if (lua_isstring(L,1)) str = (char*) lua_tostring(L,1);
   else {
      NLUA_DEBUG("Trying to push a var with non-string name");
      return 0;
   }
   var.name = strdup(str);
   
   /* store appropriate data */
   if (lua_isnil(L,2)) 
      var.type = MISN_VAR_NIL;
   else if (lua_isnumber(L,2)) {
      var.type = MISN_VAR_NUM;
      var.d.num = (double) lua_tonumber(L,2);
   }
   else if (lua_isboolean(L,2)) {
      var.type = MISN_VAR_BOOL;
      var.d.b = lua_toboolean(L,2);
   }
   else if (lua_isstring(L,2)) {
      var.type = MISN_VAR_STR;
      var.d.str = strdup( (char*) lua_tostring(L,2) );
   }
   else {
      NLUA_DEBUG("Trying to push a var of invalid data type to stack");
      return 0;
   }
   var_add( &var );

   return 0;
}
/**
 * @fn static void var_free( misn_var* var )
 *
 * @brief Frees a mission variable.
 *
 *    @param var Mission variable to free.
 */
static void var_free( misn_var* var )
{
   switch (var->type) {
      case MISN_VAR_STR:
         if (var->d.str!=NULL) {
            free(var->d.str);
            var->d.str = NULL;
         }
         break;
      case MISN_VAR_NIL:
      case MISN_VAR_NUM:
      case MISN_VAR_BOOL:
         break;
   }

   if (var->name!=NULL) {
      free(var->name);
      var->name = NULL;
   }
}
/**
 * @fn void var_cleanup (void)
 *
 * @brief Cleans up all the mission variables.
 */
void var_cleanup (void)
{
   int i;
   for (i=0; i<var_nstack; i++)
      var_free( &var_stack[i] );

   if (var_stack!=NULL) free( var_stack );
   var_stack = NULL;
   var_nstack = 0;
   var_mstack = 0;
}



/**
 * @defgroup PLAYER Player Lua bindings
 *
 * @brief Lua bindings to interact with the player.
 *
 * Functions should be called like:
 *
 * @code
 * player.function( parameters )
 * @endcode
 *
 * @{
 */
/**
 * @fn static int player_getname( lua_State *L )
 *
 * @brief string name( nil )
 *
 * Gets the player's name.
 *
 *    @return The name of the player.
 */
static int player_getname( lua_State *L )
{
   lua_pushstring(L,player_name);
   return 1;
}
/**
 * @fn static int player_shipname( lua_State *L )
 *
 * @brief string ship( nil )
 *
 * Gets the player's ship's name.
 *
 *    @return The name of the ship the player is currently in.
 */
static int player_shipname( lua_State *L )
{
   lua_pushstring(L,player->name);
   return 1;
}
/**
 * @fn static int player_freeSpace( lua_State *L )
 *
 * @brief number freeCargo( nil )
 *
 * Gets the free cargo space the player has.
 *
 *    @return The free cargo space in tons of the player.
 */
static int player_freeSpace( lua_State *L )
{
   lua_pushnumber(L, pilot_cargoFree(player) );
   return 1;
}
/**
 * @fn static int player_addCargo( lua_State *L )
 *
 * @brief number addCargo( string cargo, number quantity )
 *
 * Adds some mission cargo to the player.  He cannot sell it nor get rid of it
 *  unless he abandons the mission in which case it'll get eliminated.
 *
 *    @param cargo Name of the cargo to add.
 *    @param quantity Quantity of cargo to add.
 *    @return The id of the cargo which can be used in rmCargo.
 */
static int player_addCargo( lua_State *L )
{
   Commodity *cargo;
   int quantity, ret;

   NLUA_MIN_ARGS(2);

   if (lua_isstring(L,1)) cargo = commodity_get( (char*) lua_tostring(L,1) );
   else NLUA_INVALID_PARAMETER();
   if (lua_isnumber(L,2)) quantity = (int) lua_tonumber(L,2);
   else NLUA_INVALID_PARAMETER();

   ret = pilot_addMissionCargo( player, cargo, quantity );
   mission_linkCargo( cur_mission, ret );

   lua_pushnumber(L, ret);
   return 1;
}
/**
 * @fn static int player_rmCargo( lua_State *L )
 *
 * @brief bool rmCargo( number cargoid )
 *
 * Removes the mission cargo.
 *
 *    @param cargoid Identifier of the mission cargo.
 *    @return true on success.
 */
static int player_rmCargo( lua_State *L )
{
   int ret;
   unsigned int id;

   NLUA_MIN_ARGS(1);

   if (lua_isnumber(L,1)) id = (unsigned int) lua_tonumber(L,1);
   else NLUA_INVALID_PARAMETER();

   ret = pilot_rmMissionCargo( player, id );
   mission_unlinkCargo( cur_mission, id );

   lua_pushboolean(L,!ret);
   return 1;
}
/**
 * @fn static int player_pay( lua_State *L )
 *
 * @brief pay( number amount )
 *
 * Pays the player an amount of money.
 *
 *    @param amount Amount of money to pay the player in credits.
 */
static int player_pay( lua_State *L )
{
   int money;

   NLUA_MIN_ARGS(1);

   if (lua_isnumber(L,1)) money = (int) lua_tonumber(L,1);
   else NLUA_INVALID_PARAMETER();

   player->credits += money;

   return 0;
}
/**
 * @fn static int player_msg( lua_State *L )
 *
 * @brief msg( string message )
 *
 * Sends the player an ingame message.
 *
 *    @param message Message to send the player.
 */
static int player_msg( lua_State *L )
{
   NLUA_MIN_ARGS(1);
   char* str;

   if (lua_isstring(L,-1)) str = (char*) lua_tostring(L,-1);
   else NLUA_INVALID_PARAMETER();

   player_message(str);
   return 0;
}
/**
 * @fn static int player_modFaction( lua_State *L )
 *
 * @brief modFaction( string faction, number mod )
 *
 * Increases the player's standing to a faction by an amount.  This will
 *  affect player's standing with that faction's allies and enemies also.
 *
 *    @param faction Name of the faction.
 *    @param mod Amount to modify standing by.
 */
static int player_modFaction( lua_State *L )
{
   NLUA_MIN_ARGS(2);
   int f, mod;

   if (lua_isstring(L,1)) f = faction_get( lua_tostring(L,1) );
   else NLUA_INVALID_PARAMETER();

   if (lua_isnumber(L,2)) mod = (int) lua_tonumber(L,2);
   else NLUA_INVALID_PARAMETER();

   faction_modPlayer( f, mod );

   return 0;
}
/**
 * @fn static int player_modFactionRaw( lua_State *L )
 *
 * @brief modFactionRaw( string faction, number mod )
 *
 * Increases the player's standing to a faction by a fixed amount without
 *  touching other faction standings.
 *
 *    @param faction Name of the faction.
 *    @param mod Amount to modify standing by.
 */
static int player_modFactionRaw( lua_State *L )
{
   NLUA_MIN_ARGS(2);
   int f, mod;

   if (lua_isstring(L,1)) f = faction_get( lua_tostring(L,1) );
   else NLUA_INVALID_PARAMETER();

   if (lua_isnumber(L,2)) mod = (int) lua_tonumber(L,2);
   else NLUA_INVALID_PARAMETER();

   faction_modPlayerRaw( f, mod );

   return 0;
}
/**
 * @fn static int player_getFaction( lua_State *L )
 *
 * @brief number getFaction( string faction )
 *
 * Gets the standing of the player with a certain faction.
 *
 *    @param faction Faction to get the standing of.
 *    @return The faction standing.
 */
static int player_getFaction( lua_State *L )
{
   NLUA_MIN_ARGS(1);
   int f;

   if (lua_isstring(L,1)) f = faction_get( lua_tostring(L,1) );
   else NLUA_INVALID_PARAMETER();

   lua_pushnumber(L, faction_getPlayer(f));

   return 1;
}
/**
 * @fn static int player_getRating( lua_State *L )
 *
 * @brief number, string getRating( nil )
 *
 * Gets the player's combat rating.
 *
 *    @return Returns the combat rating (in raw number) and the actual
 *             standing in human readable form.
 */
static int player_getRating( lua_State *L )
{
   lua_pushnumber(L, player_crating);
   lua_pushstring(L, player_rating());
   return 2;
}
/**
 * @}
 */



/*
 *   H O O K
 */
static unsigned int hook_generic( lua_State *L, char* stack )
{
   int i;
   char *func;

   NLUA_MIN_ARGS(1);

   /* Last parameter must be function to hook */
   if (lua_isstring(L,-1)) func = (char*)lua_tostring(L,-1);
   else NLUA_INVALID_PARAMETER();

   /* make sure mission is a player mission */
   for (i=0; i<MISSION_MAX; i++)
      if (player_missions[i].id == cur_mission->id)
         break;
   if (i>=MISSION_MAX) {
      WARN("Mission not in stack trying to hook");
      return 0;
   }

   return hook_add( cur_mission->id, func, stack );
}
static int hook_land( lua_State *L )
{
   hook_generic( L, "land" );
   return 0;
}
static int hook_takeoff( lua_State *L )
{
   hook_generic( L, "takeoff" );
   return 0;
}
static int hook_time( lua_State *L )
{
   hook_generic( L, "time" );
   return 0;
}
static int hook_enter( lua_State *L )
{
   hook_generic( L, "enter" );
   return 0;
}
static int hook_pilot( lua_State *L )
{
   NLUA_MIN_ARGS(2);
   unsigned int h,p;
   int type;
   char *hook_type;

   /* First parameter parameter - pilot to hook */
   if (lua_isnumber(L,1)) p = (unsigned int) lua_tonumber(L,1);
   else NLUA_INVALID_PARAMETER();

   /* Second parameter - hook name */
   if (lua_isstring(L,2)) hook_type = (char*) lua_tostring(L,2);
   else NLUA_INVALID_PARAMETER();

   /* Check to see if hook_type is valid */
   if (strcmp(hook_type,"death")==0) type = PILOT_HOOK_DEATH;
   else if (strcmp(hook_type,"board")==0) type = PILOT_HOOK_BOARD;
   else if (strcmp(hook_type,"disable")==0) type = PILOT_HOOK_DISABLE;
   else { /* hook_type not valid */
      NLUA_DEBUG("Invalid pilot hook type: '%s'", hook_type);
      return 0;
   }

   /* actually add the hook */
   h = hook_generic( L, hook_type );
   pilot_addHook( pilot_get(p), type, h );

   return 0;
}



/*
 *   P I L O T
 */
static int pilot_addFleet( lua_State *L )
{
   NLUA_MIN_ARGS(1);
   Fleet *flt;
   char *fltname, *fltai;
   int i, j;
   unsigned int p;
   double a;
   Vector2d vv,vp, vn;
   FleetPilot *plt;

   /* Parse first argument - Fleet Name */
   if (lua_isstring(L,1)) fltname = (char*) lua_tostring(L,1);
   else NLUA_INVALID_PARAMETER();
   
   /* Parse second argument - Fleet AI Override */
   if (lua_isstring(L,2)) fltai = (char*) lua_tostring(L,2);
   else fltai = NULL;

   /* pull the fleet */
   flt = fleet_get( fltname );
   if (flt == NULL) {
      NLUA_DEBUG("Fleet not found!");
      return 0;
   }

   /* this should probable be done better */
   vect_pset( &vp, RNG(MIN_HYPERSPACE_DIST, MIN_HYPERSPACE_DIST*1.5),
         RNG(0,360)*M_PI/180.);
   vectnull(&vn);

   /* now we start adding pilots and toss ids into the table we return */
   j = 0;
   lua_newtable(L);
   for (i=0; i<flt->npilots; i++) {

      plt = &flt->pilots[i];

      if (RNG(0,100) <= plt->chance) {

         /* fleet displacement */
         vect_cadd(&vp, RNG(75,150) * (RNG(0,1) ? 1 : -1),
               RNG(75,150) * (RNG(0,1) ? 1 : -1));

         a = vect_angle(&vp,&vn);
         vectnull(&vv);
         p = pilot_create( plt->ship,
               plt->name,
               flt->faction,
               (fltai != NULL) ? /* Lua AI Override */
                     ai_getProfile(fltai) : 
                     (plt->ai != NULL) ? /* Pilot AI Override */
                        plt->ai : flt->ai,
               a,
               &vp,
               &vv,
               0 );

         /* we push each pilot created into a table and return it */
         lua_pushnumber(L,++j); /* index, starts with 1 */
         lua_pushnumber(L,p); /* value = pilot id */
         lua_rawset(L,-3); /* store the value in the table */
      }
   }
   return 1;
}
static int pilot_rename( lua_State* L )
{
   NLUA_MIN_ARGS(2);
   char *name;
   unsigned int id;
   Pilot *p;

   if (lua_isnumber(L,1)) id = (unsigned int) lua_tonumber(L,1);
   else NLUA_INVALID_PARAMETER();
   if (lua_isstring(L,2)) name = (char*) lua_tostring(L,2);
   else NLUA_INVALID_PARAMETER();

   p = pilot_get( id );
   free(p->name);
   p->name = strdup(name);
   return 0; 
}


